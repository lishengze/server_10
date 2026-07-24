#ifndef ADK_IMPL_THREAD_POOL_H_
#define ADK_IMPL_THREAD_POOL_H_

#include "util.h"
#include "error_code.h"
#include "arch/generic.h"
#include "arch/synchronize.h"
#include "high_performance_clock.h"

#include <time.h>
#include <assert.h>

#include <vector>

#include <boost/asio.hpp>
#include <boost/function.hpp>
#include <boost/thread/thread.hpp>

#define ADK_THREAD_POOL_UNLIMITED   -1

namespace adk_impl
{

using namespace boost::asio;

class MPSCQueue;
class ThreadPool;

#define JOB_RESCHED_DELAY_SEC(delay_sec) JOB_RESCHED_DELAY(delay_sec * 1000UL*1000*1000UL)

#define JOB_RESCHED_DELAY(delay_ns) do  \
{   \
    ::clock_gettime(CLOCK_MONOTONIC_RAW, &delay_end_); \
    delay_end_.tv_sec +=  \
                adk_impl::tick::div_rem(delay_end_.tv_nsec + delay_ns,    \
                                   1000UL*1000*1000UL,    \
                                   (uint64_t*)&delay_end_.tv_nsec); \
    is_delay_ = true;    \
    return adk_impl::IJob::JobRetType::kIdle; \
} while (false)

class IJob
{
public:
    enum JobRetType
    {
        kSuccess,
        kFailure,
        kResched,
        kIdle,
        kDone,
    };

    IJob()
        :   thr_pool_(NULL)
    {
        delay_end_.tv_sec = 0;
        delay_end_.tv_nsec = 0;
        is_delay_ = false;
    }

    virtual ~IJob() {}

    virtual IJob::JobRetType Init() { return IJob::JobRetType::kSuccess; }

    virtual IJob::JobRetType Run() = 0;

    virtual void Exit() {}

    bool is_delay() 
    { 
        if (is_delay_)
        {
            struct timespec current_time;
            ::clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
            if (time_before(current_time, IJob::delay_end_))
            {
                return true;
            }
            is_delay_ = false;
        }
        return false;
    }

    struct timespec delay_end_;
    bool   is_delay_;

private:
    ThreadPool* thr_pool_;

    void set_thread_pool(ThreadPool* thr_pool) 
    {
        thr_pool_ = thr_pool;
    }

    friend class ThreadPool;
};

class ThreadPool
{
public:
    ThreadPool();

    ~ThreadPool();

    struct Service
    {
        MPSCQueue*          job_queue_;
        MPSCQueue*          idle_job_queue_;
        uint64_t            nr_resched_jobs;
        uint32_t            nr_new_jobs;
        volatile bool       is_running;
        volatile bool       one_shot_done;
        boost::thread       worker_thread;
    };

    typedef boost::function<void ()> DispatcherType;
    typedef boost::function<void (const DispatcherType&)> RestartHandlerType;
    
    int32_t Init();

    int32_t Schedule(IJob* job);

    int32_t TrySchedule(IJob* job);

    void Start();

    // int32_t Start(io_service& srv);

    void Start(const RestartHandlerType& restart_handler)
    {
        assert(dispatch_queue_ != NULL);
        is_running_ = true;
        is_multiplexer_mode_ = true;
        restart_handler_ = restart_handler;
        restart_handler_(dispatch_processor_);
    }

    void Stop();

    void Join();

    void Suspend();

    void Resume();

    void set_nr_init_thr(int32_t nr_init_thr) 
    { 
        assert(nr_init_thr > 0);
        nr_init_thr_ = nr_init_thr; 
    }

    void set_nr_max_thr(int32_t nr_max_thr) 
    { 
        assert(nr_max_thr > 0 || nr_max_thr == ADK_THREAD_POOL_UNLIMITED);
        nr_max_thr_ = nr_max_thr; 
    }

    void set_nr_max_cc_jobs(int32_t nr_max_cc_jobs) 
    { 
        assert(nr_max_cc_jobs > 0);
        nr_max_cc_jobs_ = nr_max_cc_jobs; 
    }

    void set_worker_reap_nrounds(int32_t worker_reap_nrounds) 
    { 
        assert(worker_reap_nrounds > 0);
        worker_reap_nrounds_ = worker_reap_nrounds; 
    }

    void set_idle_delay_us(int32_t idle_delay_us) 
    { 
        assert(idle_delay_us >= 0);
        idle_delay_us_ = idle_delay_us; 
    }

private:
    volatile bool               is_running_;
    bool                        is_multiplexer_mode_;
    IJob*                       new_job_;
    MPSCQueue*                  dispatch_queue_;
    int32_t                     nr_init_thr_;
    int32_t                     nr_max_thr_;
    int32_t                     nr_max_cc_jobs_;
    int32_t                     cc_job_per_srv_;
    int32_t                     worker_reap_nrounds_;
    int32_t                     idle_delay_us_;
    uint32_t                    dispatch_threshold_;
    std::vector<Service*>       services_;
    boost::thread               dispatcher_thread_;
    DispatcherType              dispatch_processor_;
    RestartHandlerType          restart_handler_;
    Semaphore                   stop_sem_;

    void DispatcherMain();
    void WorkerMain(Service* service);
    void NewService(IJob* job = NULL, bool reuse = false);
    bool GetNewJob();
};

} // adk

#endif // ADK_THREAD_POOL_H_
