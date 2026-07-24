#include <boost/format.hpp>

#include <adk/util.h>
#include <adk/error_code.h>
#include <adk/thread_pool.h>
#include <adk/entry_wrapper.h>
#include <adk/lock_free_msg_queue.h>

namespace adk_impl
{

ThreadPool::ThreadPool()
    :   is_running_(false),
        is_multiplexer_mode_(false),
        nr_init_thr_(1),
        nr_max_thr_(4),
        nr_max_cc_jobs_(4096)
{
    cc_job_per_srv_ = 8;
    dispatch_threshold_ = cc_job_per_srv_ - 3;
    new_job_ = NULL;
    dispatch_processor_ = boost::bind(&ThreadPool::DispatcherMain, this);
    SemaphoreInit(stop_sem_, SyncDomain::kInterThread, 0);
    idle_delay_us_ = 100;
    worker_reap_nrounds_ = 600000;
}

ThreadPool::~ThreadPool()
{}

void ThreadPool::NewService(IJob* job, bool reuse)
{
    Service* service;
    if (reuse == true)
    {
        service = NULL;
        for (auto service_it : services_)
        {
            if (service_it->one_shot_done)
            {
                service = service_it;
                if (service->is_running && job != NULL)
                {
                    service->one_shot_done = false;
                    service->job_queue_->Push(job);
                    return;
                }

                if (service->worker_thread.joinable())
                {
                    service->worker_thread.join();
                }
                break;
            }
        }
        if (service == NULL)
        {
            goto new_service;
        }
    }
    else
    {
        new_service:
        service = new Service;
        service->job_queue_ = MPSCQueue::Create(
                                (boost::format("thr_pool_srv_%1%") % services_.size()).str(),
                                sizeof(IJob*),
                                cc_job_per_srv_);
        service->idle_job_queue_ = MPSCQueue::Create(
                                    (boost::format("thr_pool_srv_%1%_idle") % services_.size()).str(),
                                    sizeof(IJob*),
                                    cc_job_per_srv_);
        services_.push_back(service);
    }
    
    service->nr_resched_jobs = 0;
    service->nr_new_jobs = 0;
    service->is_running = true;
    service->one_shot_done = false;
    service->worker_thread = boost_thread("adk-thrdpoolworker", "worker thread", boost::bind(&ThreadPool::WorkerMain, this, service));
    

    if (job != NULL)
    {
        service->job_queue_->Push(job);
    }
    else
    {
        service->one_shot_done = true;
    }
}

int32_t ThreadPool::Init()
{
    if (nr_max_thr_ != ADK_THREAD_POOL_UNLIMITED)
    {
        cc_job_per_srv_ = nr_max_cc_jobs_ / nr_max_thr_;
        cc_job_per_srv_ = cc_job_per_srv_ > 0 ? cc_job_per_srv_ : 8;
        dispatch_threshold_ = cc_job_per_srv_ - 2;
    }
    
    for (int32_t i = 0; i < nr_init_thr_; ++i)
    {
        NewService();
    }

    dispatch_queue_ = MPSCQueue::Create("dispatch_queue", sizeof(IJob*), nr_max_cc_jobs_);
    return ErrorCode::kSuccess;
}

void ThreadPool::Start()
{
    assert(dispatch_queue_ != NULL);
    if (is_running_)
    {
        return;
    }

    is_running_ = true;
    dispatcher_thread_ = boost_thread("adk-thrdpooldisp", "dispatcher thread", boost::bind(&ThreadPool::DispatcherMain, this));
}

int32_t ThreadPool::TrySchedule(IJob* job)
{
    assert(dispatch_queue_ != NULL);
    return dispatch_queue_->Push(job);
}


int32_t ThreadPool::Schedule(IJob* job)
{
    assert(dispatch_queue_ != NULL);

    // FIXME: using MPSC unbounded queue
    while (dispatch_queue_->Push(job) != ErrorCode::kSuccess
           && is_running_)
    {
        usleep(idle_delay_us_);
    }
    return ErrorCode::kSuccess;
}

void ThreadPool::Stop()
{
    if (is_running_)
    {
        is_running_ = false;
        SemaphoreWait(stop_sem_, -1U);
        for (auto service: services_)
        {
            service->is_running = false;   
        }
    }
}

void ThreadPool::Join()
{
    assert(is_running_ == false);
    if (!is_multiplexer_mode_)
    {
        if (dispatcher_thread_.joinable())
        {
            dispatcher_thread_.join();
        }
    }

    for (auto service: services_)
    {
        if (service->worker_thread.joinable())
        {
            service->worker_thread.join();
        } 
    }
}

#define ADK_THR_POOL_CONTINUE(is_idle)   if (is_multiplexer_mode_)    \
                                {   \
                                    restart_handler_(dispatch_processor_);  \
                                    break; \
                                }   \
                                else    \
                                {   \
                                    if (is_idle)    \
                                    {   \
                                        usleep(idle_delay_us_);    \
                                    }   \
                                    continue;   \
                                }

#define ADK_JOB_DISPATCHED()     new_job_ = NULL;    \
                                 is_idle = false;

bool ThreadPool::GetNewJob()
{
    get_new_job:
    if (dispatch_queue_->Pop(new_job_) == ErrorCode::kSuccess)
    {
        if (ADK_UNLIKELY(new_job_->Init() != IJob::JobRetType::kSuccess))
        {
            new_job_->Exit();
            new_job_ = NULL;
            goto get_new_job;
        }
        return true;
    }
    return false;
}

void ThreadPool::DispatcherMain()
{
    uint32_t idle_counter = 0;
    std::vector<Service*> reap_threads;
    while (is_running_)
    {
        bool is_idle = true;
        if (new_job_ != NULL || GetNewJob())
        {
            new_job_->set_thread_pool(this);

            if (nr_max_thr_ == ADK_THREAD_POOL_UNLIMITED)
            {
                NewService(new_job_, true);

                ADK_JOB_DISPATCHED();
                ADK_THR_POOL_CONTINUE(false);
            }

            uint64_t total_cc_jobs = 0;
            uint32_t min_load_service = 0;
            uint32_t min_load = -1U;
            uint32_t index = 0;
            for (auto& job_service : services_)
            {
                uint32_t service_load = job_service->job_queue_->length() 
                                        + job_service->idle_job_queue_->length();
                total_cc_jobs += service_load;
                if (service_load < min_load)
                {
                    min_load_service = index;
                    min_load = service_load;
                }
                ++index;
            }

            if (total_cc_jobs >= services_.size() * 4)
            {
                NewService(new_job_);
                ADK_JOB_DISPATCHED();
            }
            else
            {
                if (min_load < dispatch_threshold_ 
                    && (services_[min_load_service]->job_queue_->Push(new_job_)
                        == ErrorCode::kSuccess))
                {
                    ADK_JOB_DISPATCHED();
                }
            }
        }

        if (is_idle)
        {
            if ((++idle_counter) == (uint32_t)worker_reap_nrounds_) // by default delay configuration, 1 min
            {
                idle_counter = 0;
                reap_threads.clear();
                for (auto job_service : services_)
                {
                    if (job_service->one_shot_done 
                        && job_service->is_running
                        && services_.size() - reap_threads.size() > (uint32_t)nr_init_thr_)
                    {
                        reap_threads.push_back(job_service);
                    }
                }

                for (auto service : reap_threads)
                {
                    service->is_running = false;
                    if (service->worker_thread.joinable())
                    {
                        service->worker_thread.join();
                    }
                }
            }
        }
        else
        {
            idle_counter = 0;
        }

        ADK_THR_POOL_CONTINUE(is_idle);
    }

    if (!is_running_)
    {
        SemaphorePost(stop_sem_);
    }
}

static void DrainIdleQueue(MPSCQueue* job_queue, MPSCQueue* idle_job_queue)
{
    IJob* job;
    while (idle_job_queue->Pop(job) == ErrorCode::kSuccess)
    {
        auto ec = job_queue->Push(job);
        assert(ec == ErrorCode::kSuccess);
        ADK_NOTUSE(ec);
    }
}

void ThreadPool::WorkerMain(Service* service)
{
    assert(service != NULL);
    assert(service->job_queue_ != NULL);

    auto* job_queue = service->job_queue_;
    auto* idle_job_queue = service->idle_job_queue_;
    uint32_t nr_job_processed = 0;

    while (service->is_running)
    {
        IJob* job = NULL;
        if (job_queue->Pop(job) == ErrorCode::kSuccess)
        {
            IJob::JobRetType ret;

            if (job->is_delay())
            {
                goto job_idle;
            }

            ret = job->Run();
            if (ret == IJob::JobRetType::kDone)
            {
                ++nr_job_processed;
                job->Exit();
                if (nr_max_thr_ == ADK_THREAD_POOL_UNLIMITED)
                {
                    // FIXME: optimize, do not exit!!
                    service->one_shot_done = true;
                }
                continue;
                // note: Do not use this job any more.
            }
            else if (ret == IJob::JobRetType::kIdle)
            {
                job_idle:
                idle_job_queue->Push(job);
            }
            else
            {
                job_queue->Push(job);
                ++nr_job_processed;
                if (ADK_UNLIKELY(nr_job_processed == 128))
                {
                    DrainIdleQueue(job_queue, idle_job_queue);
                    nr_job_processed = 0;
                }
            }
        }
        else
        {
            usleep(idle_delay_us_);
            DrainIdleQueue(job_queue, idle_job_queue);
            nr_job_processed = 0;
        }
    }
}

} // adk
