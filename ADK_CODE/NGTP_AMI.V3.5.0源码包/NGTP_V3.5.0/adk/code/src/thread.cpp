#include <boost/property_tree/ptree.hpp>
#include <boost/preprocessor/repetition.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/exception/diagnostic_information.hpp>

#include <adk/util.h>
#include <adk/thread.h>
#include <adk/scoped_lock.h>
#include <adk/entry_wrapper.h>
#include <adk/arch/synchronize.h>

#define ADK_IS_OOB_MSG(msg_ptr) (((adk_impl::ThreadMessageBase<void*>*)msg_ptr)->message_type() < 0)

namespace adk_impl
{

struct ThreadContext
{
    void*       msg;
    int32_t     tag;
    int32_t     instance;
    GenericThread::HandlerType* message_handlers;
    const char* name;
};
__thread ThreadContext* thread_context;
__thread TimerIdType tls_timer_id = -1;

std::ostream& operator<< (std::ostream& stream, const ThreadParams* params)
{
    if (params != NULL)
    {
        boost::property_tree::ptree ptree;
        ptree.put("instance_number",    params->number_instance);
        ptree.put("init_priority",      params->init_priority);
        ptree.put("event_mode",         params->event_mode);
        ptree.put("busy_poll_ns",       params->busy_poll_ns);
        ptree.put("wait_timeout_ns",    params->wait_timeout_ns);
        ptree.put("backoff_limit",      params->backoff_limit);
        ptree.put("msg_budget",         params->msg_budget);
        ptree.put("oob_msg_budget",     params->oob_msg_budget);
        ptree.put("parallel_init",      params->parallel_init);
        boost::property_tree::json_parser::write_json(stream, ptree, false);
    }
    else
        stream << "NULL pointer";
    return stream;
}

static int32_t g_oob_msg_type_current = ADK_MESSAGE_BASE_TYPE + 1;
static int32_t g_msg_type_current = ADK_MESSAGE_BASE_TYPE + 1;

int32_t AllocMessageType(bool is_oob)
{
    int32_t ret = atomic_inc(is_oob ? g_oob_msg_type_current : g_msg_type_current) 
                  * (is_oob ? -1 : 1);
    assert(ret < ADK_MAX_THREAD_MESSAGES);
    assert(ret > -(ADK_MAX_THREAD_OOB_MESSAGES));
    return ret;
}

int32_t GenericThread::AllocTag()
{
    static int32_t current_tag = 0;
    return current_tag++;
}

ThreadManager* ThreadManager::thr_mana_;


int32_t ThreadManager::ChangeParams(ThreadParams* params, 
                                      BOOST_PP_ENUM_PARAMS(
                                            ADK_THREAD_PROPERTY_NUM,
                                            const GenericArg& arg)
                                    )
{
    #define arg_assigment(z, n, unused)   \
            ArgAssigment(params, arg##n);

    BOOST_PP_REPEAT(ADK_THREAD_PROPERTY_NUM, arg_assigment, ~)

    #undef arg_assigment
    return ErrorCode::kSuccess;
}

ThreadManager* ThreadManager::Instance(const std::string& ns)
{
    static boost::mutex lock;
    static std::map<std::string, ThreadManager*> s_thread_mana_map;

    boost::mutex::scoped_lock lock_guard(lock);

    if (thr_mana_ == NULL)
    {
        thr_mana_ = new ThreadManager();
        s_thread_mana_map["Default"] = thr_mana_;
    }

    auto it = s_thread_mana_map.find(ns);
    if (it != s_thread_mana_map.end())
    {
        return it->second;
    }

    if (ns.empty())
    {
        return s_thread_mana_map["Default"];
    }

    auto& thr_mana = s_thread_mana_map[ns];
    thr_mana = new ThreadManager();
    return thr_mana;
}

int32_t ThreadManager::AddThread(ThreadBase* app_thread)
{
    boost::recursive_mutex::scoped_lock lock_guard(thr_mana_rlock_);
    threads_[app_thread->thread_tag_][app_thread->instance_id_] = app_thread;
    exist_threads_[app_thread->thread_tag_].push_back(app_thread);
    return BuildLauchInfo(app_thread);
}

void ThreadManager::AddCreator(const std::string& name,
                               const ThreadCreatorType& creator,
                               ThreadParams* params)
{
    boost::recursive_mutex::scoped_lock lock_guard(thr_mana_rlock_);
    auto& thread_info = thread_infos_[name];
    thread_info.creator = creator;
    thread_info.params = params;
}

#define ADK_START_THREAD  0
#define ADK_FINISH_THREAD 1

int32_t ThreadManager::BuildLauchInfo(ThreadBase* app_thread)
{
    auto init_priority = app_thread->thread_params_->init_priority;
    auto& thread_set = launch_map[ADK_START_THREAD][init_priority];
    LaunchInfoCommon* common = NULL;
    for (auto& li : thread_set)
    {
        if (li->app_thread->thread_tag_ == app_thread->thread_tag_)
        {
            common = li->common;
            if (li->app_thread->instance_id_ == app_thread->instance_id_)
                return ErrorCode::kFailure;
        }
    }
    common = (common ? common : new LaunchInfoCommon());
    LaunchInfo* li = new LaunchInfo(app_thread, false, common);
    thread_set.push_back(li);
    launch_map[ADK_FINISH_THREAD][-(init_priority)].push_back(li);

    app_thread->set_launch_info(li);
    return ErrorCode::kSuccess;
}

void ThreadManager::Init()
{
    boost::recursive_mutex::scoped_lock lock_guard(thr_mana_rlock_);
    if (is_init_)
        return;

    is_init_ = true;
    for (auto& thread_info : thread_infos_)
    {
        ThreadParams* changed_params = NULL;
        (thread_info.second.creator)(this, NULL, 0, &changed_params);
        if (changed_params != NULL 
            && !(changed_params->is_default))
        {
            *(thread_info.second.params) = *changed_params;
            changed_params->Reset();
        }

        auto* params = thread_info.second.params;
        for (int32_t i = params->number_create_instance; i < params->number_instance; ++i)
        {
            (thread_info.second.creator)(this, params, i, NULL);
            ++(params->number_create_instance);
        }
    }
}

void ThreadManager::Start()
{
    ThreadManager::LaunchInfoMapType launch_map_temp;
    {
        boost::recursive_mutex::scoped_lock lock_guard(thr_mana_rlock_);

        Init();

        for (auto& lis : launch_map[ADK_START_THREAD])
        {
            launch_map_temp[lis.first] = lis.second;
        }
    }

    for (auto& lis : launch_map_temp)
    {
        for (const auto& li : lis.second)
        {
            li->app_thread->Start();
        }
    }
}

void ThreadManager::Stop()
{
    std::vector<LaunchInfo*> li_vec;
    li_vec.reserve(16);
    {
        boost::recursive_mutex::scoped_lock lock_guard(thr_mana_rlock_);
        for (auto& lis : launch_map[ADK_FINISH_THREAD])
        {
            for (const auto& li : lis.second)
            {
                li_vec.push_back(li);
            }
        }
    }
    
    for (auto li : li_vec)
    {
        li->app_thread->Stop();    
    }
}

void ThreadManager::Finish()
{
    LaunchInfoMapType launch_map_temp;
    {
        boost::recursive_mutex::scoped_lock lock_guard(thr_mana_rlock_);
        for (auto& lis : launch_map[ADK_FINISH_THREAD])
        {
            launch_map_temp[lis.first] = lis.second;
        }
    }
    
    for (auto& lis : launch_map_temp)
    {
        for (const auto& li : lis.second)
        {
            li->app_thread->Finish();
        }
    }

    // this method is thread safe!
    GenericGC::Finish(thread::kThreadGCName);
}

static boost::function<int32_t()> g_on_init = [](){ return ErrorCode::kSuccess; };
static boost::function<void()> g_on_idle = [](){};
// FIXME: using signal/slot mode!
static ThreadManager::ErrorHandlerType g_on_error = [](int32_t error_value,
                                                       const std::string& error_message){};
static boost::function<void()> g_on_exit = [](){};

void ThreadManager::set_on_init(const boost::function<int32_t()>& on_init)
{
    g_on_init = on_init;
}

void ThreadManager::set_on_idle(const boost::function<void()>& on_idle)
{
    g_on_idle = on_idle;
}

void ThreadManager::set_on_error(const ThreadManager::ErrorHandlerType& on_error)
{
    g_on_error = on_error;
}

void ThreadManager::set_on_exit(const boost::function<void()>& on_exit)
{
    g_on_exit = on_exit;   
}

void* ThreadBase::s_thread_shared;

void ThreadBase::SignalHandler(AsyncSignal* sig_msg)
{
    ReleaseMessageProcess(sig_msg->message_tag());
}

int32_t ThreadBase::OnInit()
{
    return g_on_init();
}

void ThreadBase::OnIdle()
{
    g_on_idle();   
}

void ThreadBase::OnError(int32_t error_value, const std::string& error_message)
{}

void ThreadBase::OnExit()
{
    g_on_exit(); 
}

void ThreadBase::Accept(void* msg_ptr)
{
    assert(thread_message_queue_[ADK_OOB_MQ]);
    assert(thread_message_queue_[ADK_MQ]);
    assert(ev_mana_);

    ev_mana_->Notify([this, msg_ptr](){
            return thread_message_queue_[ADK_IS_OOB_MSG(msg_ptr)]->Push(msg_ptr);
    });
}

int32_t ThreadBase::AcceptNoBlock(void* msg_ptr)
{
    assert(thread_message_queue_[ADK_OOB_MQ]);
    assert(thread_message_queue_[ADK_MQ]);
    assert(ev_mana_);

    auto ret = thread_message_queue_[ADK_IS_OOB_MSG(msg_ptr)]->Push(msg_ptr);
    if(ret == kSuccess)
        ev_mana_->Notify([](){return kSuccess;});
    return ret;
}

GenericThread::HandlerType* GenericThread::DoAllocBuffer(uint32_t nr_entries)
{
    HandlerType* ret = new HandlerType [nr_entries]; // new HandlerType [nr_entries]()
    for (uint32_t i = 0; i < nr_entries; ++i)
    {
        ret[i] = (GenericThread::HandlerType)(&ThreadBase::OnMessageDefault);
    }
    return ret;
}

void ThreadBase::OnMessageDefault(void* msg)
{
    ++thr_stats_.nr_error_msg;
    static bool is_ignore = false;
    if (!is_ignore)
    {
        ThreadMessageBase<void*>* message_base = (ThreadMessageBase<void*>*)msg;
        OnError(ErrorCode::kUnknownMessge, 
                (boost::format("message %1% is received by thread %2%, instance %3%")
                               % message_base->message_type()
                               % thread_name_
                               % instance_id()).str()
               );
        is_ignore = true;
    }
}

int32_t ThreadBase::PollOOBMessage()
{
    if (oob_mq_srv_cnt_ < oob_msg_budget_)
    {
        if (thread_message_queue_[ADK_OOB_MQ]->Pop(msg_ptr_)
            == ErrorCode::kSuccess)
        {
            ++oob_mq_srv_cnt_;
            DeliverOOBMessage();
            return ErrorCode::kSuccess;
        }

        // no oob message
    }
    oob_mq_srv_cnt_ = 0;
    mq_srv_cnt_ = 0;
    return ErrorCode::kWouldblock;
}

void ThreadBase::DoAllocCacheQueue(int32_t tag)
{
    #ifdef __ADK_THREAD_TEST__
    std::cout << "Information: " << thread_name_ << " new cache queue, tag = " << tag << std::endl;
    #endif

    cache_queues_[tag].tag = tag;
    cache_queues_[tag].queue_name = "class_" + boost::lexical_cast<std::string>(tag);
    cache_queues_[tag].cache_queue = SPSCUnboundedQueue<void*>::Create(cache_queues_[tag].queue_name);
}

void ThreadBase::DeliverOOBMessage()
{
    ++thr_stats_.nr_oob_msg;
    ThreadMessageBase<void*>* message_base = (ThreadMessageBase<void*>*)msg_ptr_;
    assert(message_base->message_type() < 0);
    
    (((GenericThread*)(this))->*(message_handlers_[message_base->message_type()]))(msg_ptr_);

    if (message_base->drop_reference())
    {
        gc_agent_->PushGCRequest(message_base);
    }
}

void ThreadBase::DeliverMessage()
{
    ++thr_stats_.nr_normal_msg;
    ThreadMessageBase<void*>* message_base = (ThreadMessageBase<void*>*)msg_ptr_;
    auto tag = message_base->message_tag();

    if (ADK_UNLIKELY(cache_map_[tag]))
    {
        AllocCacheQueue(tag);

        assert(cache_queues_[tag].cache_queue != NULL);
        cache_queues_[tag].cache_queue->Push(msg_ptr_);
        ++thr_stats_.nr_cached_msg;

        #ifdef __ADK_THREAD_TEST__
        std::cout << "Information: " << thread_name_ << " DeliverMessage, cache message, "
                  << msg_ptr_ << " tag = " << tag  << ", cache queue "
                  << cache_queues_[tag].cache_queue << std::endl;
        #endif
        return;
    }

    (((GenericThread*)(this))->*(message_handlers_[message_base->message_type()]))(msg_ptr_);

    if (ADK_UNLIKELY(cache_map_[tag]))
    {
        AllocCacheQueue(tag);

        #ifdef __ADK_THREAD_TEST__
        std::cout << "Information: " << thread_name_ << " DeliverMessage, recache message, "
                  << msg_ptr_ << " tag = " << tag << ", cache queue "
                  << cache_queues_[tag].cache_queue << std::endl;
        #endif

        ++thr_stats_.nr_cached_msg;
        cache_queues_[tag].cache_queue->Push(msg_ptr_);
        return;
    }

    if (message_base->drop_reference())
    {
        gc_agent_->PushGCRequest(message_base);
    }
}

void ThreadBase::DeliverCachedMessage()
{
    ThreadMessageBase<void*>* message_base = (ThreadMessageBase<void*>*)msg_ptr_;

    auto tag = message_base->message_tag();

    assert(!(block_map_[tag]));

    (((GenericThread*)(this))->*(message_handlers_[message_base->message_type()]))(msg_ptr_);

    if (ADK_UNLIKELY(block_map_[tag]))
    {
        #ifdef __ADK_THREAD_TEST__
        std::cout << "Information: " << thread_name_ << " DeliverCachedMessage, recache message, tag = " << tag << std::endl;
        #endif

        return;
    }

    assert(cache_queues_[tag].cache_queue != NULL);
    cache_queues_[tag].cache_queue->Pop();

    if (message_base->drop_reference())
    {
        gc_agent_->PushGCRequest(message_base);
    }
}

bool ThreadBase::PopCachedMessage()
{
    if (current_cache_queue_ == NULL)
    {
        release_queue_->Pop(current_cache_queue_);
        nr_cq_srv_cnt_ = 0;
    }
    
    ++nr_cq_srv_cnt_;
    void** head_ptr = (*current_cache_queue_)->Head();
    if (head_ptr != NULL)
    {
        msg_ptr_ = *head_ptr;
        #ifdef __ADK_THREAD_TEST__
        CacheQueueInfo* cqi = ADK_CONTAINER_OF(current_cache_queue_, 
                                               CacheQueueInfo,
                                               cache_queue);
        std::cout << "Information: " << thread_name_ << " PopCachedMessage success " << msg_ptr_ 
                  << ", queue " << (*current_cache_queue_) << ", "
                  << "tag " << cqi->tag  << ", "
                  << cqi->queue_name << std::endl;
        #endif

        if (nr_cq_srv_cnt_ >= nr_cq_srv_budget_)
        {
            release_queue_->Push(current_cache_queue_);
            current_cache_queue_ = NULL;
            nr_cq_srv_cnt_ = 0;
        }
        return true;
    }

    --nr_release_cache_queues_;
    CacheQueueInfo* cqi = ADK_CONTAINER_OF(current_cache_queue_, 
                                           CacheQueueInfo,
                                           cache_queue);
    cqi->release_alert = false;
    cache_map_[cqi->tag] = false;
    current_cache_queue_ = NULL;

    #ifdef __ADK_THREAD_TEST__
    std::cout << "Information: " << thread_name_ << " all cached messages are clean out " 
              << cqi->tag << ", " << cqi->queue_name << std::endl;
    #endif
    return false;
}

void ThreadBase::AsyncReleaseMessageProcess(uint64_t tag)
{
    auto sig_msg = AsyncSignal::New();
    sig_msg->set_message_tag(tag);
    this->SendMsg(sig_msg);
}

void ThreadBase::ReleaseMessageProcess(uint64_t tag)
{ 
    #ifdef __ADK_THREAD_TEST__
    std::cout << "Information: " << thread_name_ << " release message class " << tag << std::endl;
    #endif

    AllocCacheQueue(tag);

    if (block_map_[tag])
    {
        block_map_[tag] = false;

        if (!(cache_queues_[tag].release_alert))
        {
            release_queue_->Push(&(cache_queues_[tag].cache_queue));
            ++nr_release_cache_queues_;
            cache_queues_[tag].release_alert = true;

            #ifdef __ADK_THREAD_TEST__
            std::cout << "Information: " << thread_name_ << " push cache queue " << tag 
                      << " " << cache_queues_[tag].cache_queue << std::endl;
            #endif
        }
    }
}

void ThreadBase::BlockMessageProcess(uint64_t tag) 
{ 
    #ifdef __ADK_THREAD_TEST__
    std::cout << "Information: " << thread_name_ << " block message class " << tag << std::endl;
    #endif
    AllocCacheQueue(tag);

    cache_map_[tag] = true; 
    block_map_[tag] = true;

    if (cache_queues_[tag].release_alert)
    {
        SPSCUnboundedQueue<void*>** cache_queue = NULL;
        CacheQueueInfo* cqi = NULL;
        if (current_cache_queue_ != NULL)
        {
            cqi = ADK_CONTAINER_OF(current_cache_queue_, 
                                   CacheQueueInfo,
                                   cache_queue);
            if (cqi->tag == tag)
            {
                current_cache_queue_ = NULL;
                #ifdef __ADK_THREAD_TEST__
                std::cout << "Information: " << thread_name_ << " reset current_cache_queue_ " 
                          << tag << ", " << cqi->queue_name <<  std::endl;
                #endif
                goto find;
            }
        }

        while (release_queue_->Pop(cache_queue) == ErrorCode::kSuccess)
        {
            cqi = ADK_CONTAINER_OF(cache_queue, 
                                   CacheQueueInfo,
                                   cache_queue);
            if (cqi->tag == tag)
            {
                goto find;
            }
            release_queue_->Push(cache_queue);
        }

        assert(false);

        find:
        --nr_release_cache_queues_;
        cache_queues_[tag].release_alert = false;

        #ifdef __ADK_THREAD_TEST__
        std::cout << "Information: " << thread_name_ << " pop cache queue " << tag << std::endl;
        #endif
    }
}

#define ADK_MAKE_TIMER_ID(index, version)   \
    (((index) << ADK_THREAD_TIMER_SHIFT) + (version))

#define ADK_GET_TIMER_INDEX(id) ((id) >> ADK_THREAD_TIMER_SHIFT)
#define ADK_GET_TIMER_VERSION(id) ((id) & ADK_THREAD_TIMER_MASK)


void ThreadTimerManager::SchedTimerToRun(TimeValue expire_time, TimerHandler& hdl)
{
    ScopedSpinlock mana_lock_guard(timer_mana_lock_);
    running_timers_.insert(std::make_pair(expire_time, hdl));
    #ifdef __ADK_THREAD_TEST__
    std::cout << __FUNCTION__ << ", insert timer, timer_id = " << hdl.timer_id << " , running_timers_.size() = " 
              << running_timers_.size() << std::endl;
    #endif
}

TimerIdType ThreadTimerManager::AllocTimerIndex()
{
    ScopedSpinlock mana_lock_guard(timer_mana_lock_);
    TimerIdType index = last_timer_index_;
    uint32_t counter = 0;
    for (; counter < ADK_THREAD_MAX_TIMERS; ++counter)
    {
        ++index;
        if (index == ADK_THREAD_MAX_TIMERS)
            index = 0;

        auto& timer = timers_[index];
        if (timer.is_used_)
            continue;
        break;
    }

    if (counter & ADK_THREAD_MAX_TIMERS)
        return -1u;

    last_timer_index_ = index;
    timers_[index].is_used_ = true;
    return index;
}


TimerHandler ThreadTimerManager::CreateTimer(TimerType timer_type,
                                             const TimerJobType& timer_job,
                                             TimeValue   duration)
{
    return CreateTimer(timer_type, timer_job, duration, NULL, 0, 0);
}

TimerHandler ThreadTimerManager::CreateTimer(TimerType              timer_type,
                                             const TimerJobType&    timer_job,
                                             TimeValue              duration,
                                             ThreadManager*         thread_mana,
                                             uint32_t               thread_tag,
                                             uint32_t               thread_instance)
{
    auto time_value_now = timespec_now();
    TimerIdType index = AllocTimerIndex();
    if (index == -1u)
        return kInvalidTimerHandler;

    auto& timer = timers_[index];
    ScopedSpinlock timer_lock_guard(timer.lock_);

    ++timer.version_;
    timer.id_ = index;
    timer.type_ = timer_type;
    timer.job_ = timer_job;
    if (timer_type == TimerType::kPeriod)
        timer.interval_ = duration;
    if (thread_mana != NULL)
        timer.run_timer_thr_ = thread_mana->ThreadInstance(thread_tag, thread_instance);
    else
        timer.run_timer_thr_ = NULL;

    timer.expire_time_ = time_value_now + duration;
    timer.adjust_expire_time_ = 0;
    timer.state_ = TimerState::kInit;
    TimerHandler ret_hdl = { timer.id_, timer.version_ };

    if (duration != 0)
    {
        timer.state_ = TimerState::kRunning;
        SchedTimerToRun(timer.expire_time_, ret_hdl);
    }
    return ret_hdl;
}

int32_t ThreadTimerManager::FreeTimerIndex(TimerIdType index)
{
    auto& timer = timers_[index];
    ScopedSpinlock mana_lock_guard(timer_mana_lock_);
    if (!timer.is_used_)
    {
        return ErrorCode::kFailure;
    }
    timer.is_used_ = false;
    return ErrorCode::kSuccess;
}

int32_t ThreadTimerManager::DeleteTimer(TimerHandler& hdl)
{
    auto& timer = timers_[hdl.timer_id];
    ScopedSpinlock timer_lock_guard(timer.lock_);

    if (hdl.timer_version != timer.version_)
    {
        return ErrorCode::kFailure;
    }

    {
        ++timer.version_;
        if (FreeTimerIndex(hdl.timer_id) != ErrorCode::kSuccess)
        {
            --timer.version_;
            return ErrorCode::kFailure;
        }
    }
    return ErrorCode::kSuccess;
}

int32_t ThreadTimerManager::ReSchedTimer(ThreadTimer& timer, TimerHandler& hdl)
{
    ScopedSpinlock mana_lock_guard(timer_mana_lock_);
    auto it = running_timers_.find(timer.expire_time_);
    if (it != running_timers_.end())
    {
        // timer is counting
        running_timers_.erase(it);
        #ifdef __ADK_THREAD_TEST__
        std::cout << __FUNCTION__ << ", delete timer, running_timers_.size() = " << running_timers_.size() << ", "
                  << ", expire_time = " << timer.expire_time_ << std::endl;
        #endif

        timer.expire_time_ = timer.adjust_expire_time_;
        running_timers_.insert(std::make_pair(timer.expire_time_, hdl));
        timer.adjust_expire_time_ = 0;

        #ifdef __ADK_THREAD_TEST__
        std::cout << __FUNCTION__ << ", insert timer, running_timers_.size() = " << running_timers_.size() << ", " 
                  << ", expire_time = " << timer.expire_time_ << std::endl;
        #endif

        return ErrorCode::kSuccess;
    }
    else
    {
        // timer is waiting to execute
        timer.adjust_expire_time_ = 0;
        return ErrorCode::kFailure;
    }
}

int32_t ThreadTimerManager::ModifyTimer(
                        TimerHandler& hdl,
                        TimeValue     duration,
                        TimerOffset   offset,
                        bool          fail_on_timeout)
{
    if (duration <= 0)
        return ErrorCode::kInvalidParameters;

    auto& timer = timers_[hdl.timer_id];
    ScopedSpinlock timer_lock_guard(timer.lock_);

    if (ADK_UNLIKELY(hdl.timer_version != timer.version_))
    {
        return ErrorCode::kFailure;
    }

    if (timer.type_ == TimerType::kOneShot)
    {
        timer.adjust_expire_time_ = ((offset == TimerOffset::kNow) ? 
                                      timespec_now() 
                                      : ((timer.adjust_expire_time_ == 0) ? 
                                            timer.expire_time_
                                             : timer.adjust_expire_time_))
                                    + duration;
        if (timer.state_ == TimerState::kRunning)
        {
            if (ADK_UNLIKELY(timer.adjust_expire_time_ < timer.expire_time_))
            {
                #ifdef __ADK_THREAD_TEST__
                std::cout << __FUNCTION__ << ", modify timer to earlier time, timer_id = " << timer.id_ << std::endl;
                #endif

                if (ReSchedTimer(timer, hdl) != ErrorCode::kSuccess)
                    return ErrorCode::kFailure;
            }

            #ifdef __ADK_THREAD_TEST__
            std::cout << __FUNCTION__ << ", modify timer to later time, timer_id = " << timer.id_ << ", "
                      << "adjust_expire_time_ = " << timer.adjust_expire_time_ << std::endl;
            #endif

            // success;
            // defer the job when timer expired
            // FIXME: check if adjust_expire_time_ == expire_time_
        }
        else if (timer.state_ == TimerState::kExpired)
        {
            // in 3rd thread
            if (ADK_UNLIKELY((fail_on_timeout
                              && tls_timer_id != timer.id_)))
            {
                timer.adjust_expire_time_ = 0;
                return ErrorCode::kFailure;
            }

            // in timer job
            if (ADK_UNLIKELY(timer.adjust_expire_time_ <= timer.expire_time_))
            {
                timer.adjust_expire_time_ = 0;
                return ErrorCode::kFailure;
            }

            #ifdef __ADK_THREAD_TEST__
            std::cout << __FUNCTION__ << ", modify timer to later time, in timer job, timer_id = " << timer.id_ << ", "
                      << "adjust_expire_time_ = " << timer.adjust_expire_time_ << std::endl;
            #endif
            // success;
        }
        else if (timer.state_ == TimerState::kCanceling)
        {
            timer.adjust_expire_time_ = 0;
            return ErrorCode::kFailure;
        }
        else
        {
            if (ADK_UNLIKELY((fail_on_timeout
                              && timer.state_ == TimerState::kDone
                              && tls_timer_id != timer.id_)))
            {
                timer.adjust_expire_time_ = 0;
                return ErrorCode::kFailure;
            }

            // init/cancelled

            #ifdef __ADK_THREAD_TEST__
            std::cout << __FUNCTION__ << ", modify timer to run, timer_id = " << timer.id_ << std::endl;
            #endif

            timer.state_ = TimerState::kRunning;
            timer.expire_time_ = timer.adjust_expire_time_;
            timer.adjust_expire_time_ = 0;
            SchedTimerToRun(timer.expire_time_, hdl);
        }
    }
    else
    {
        if (timer.state_ == kRunning
            || timer.state_ == kExpired)
        {
            timer.interval_ = duration;
        }
        else if (timer.state_ == TimerState::kInit
                 || timer.state_ == TimerState::kCancelled)
        {
            timer.interval_ = duration;
            timer.state_ = TimerState::kRunning;
            timer.expire_time_ = timespec_now() + duration;
            timer.adjust_expire_time_ = 0;
            SchedTimerToRun(timer.expire_time_, hdl);
        }
        else 
        {
            assert(timer.state_ == TimerState::kCanceling);
            return ErrorCode::kFailure;        
        }
    }
    
    return ErrorCode::kSuccess;
}

int32_t ThreadTimerManager::CancelTimer(TimerHandler& hdl)
{
    auto& timer = timers_[hdl.timer_id];
    ScopedSpinlock timer_lock_guard(timer.lock_);
    if (timer.version_ != hdl.timer_version)    // FIXME: lock and compare in a single operation!
    {
        return ErrorCode::kInvalidParameters;
    }

    if (ADK_UNLIKELY(timer.state_ == TimerState::kExpired))
    {
        #ifdef __ADK_THREAD_TEST__
        std::cout << __FUNCTION__ << ", cancel failed, timer expired, timer_id = " 
                  << hdl.timer_id << std::endl;
        #endif
        return ErrorCode::kFailure;
    }
    else if (ADK_UNLIKELY(timer.state_ == TimerState::kCanceling))
    {
        return ErrorCode::kFailure;
    }

    timer.state_ = TimerState::kCancelled;
    return ErrorCode::kSuccess;
}

void ThreadTimerManager::SyncCancelTimer(TimerHandler& hdl)
{
    auto& timer = timers_[hdl.timer_id];
    do {

        {
            ScopedSpinlock timer_lock_guard(timer.lock_);
            if (timer.version_ != hdl.timer_version)
            {
                return ;
            }
                
            if (timer.state_ == TimerState::kExpired
                || timer.state_ == TimerState::kCanceling)
            {
                timer.state_ = TimerState::kCanceling; 
                continue;
            }

            // kRunning/kInit/kCancelled
            timer.state_ = TimerState::kCancelled;
            return ;
        }

        usleep(0);  // FIXME: do optimizing
        
    } while(true);
}

// run every 1 milliseconds
void ThreadTimerManager::RunTimerCycle()
{
    auto time_val_now = timespec_now();
    {
        ScopedSpinlock mana_lock_guard(timer_mana_lock_);
        nr_expire_timers_ = 0;
        auto it = running_timers_.begin();              // FIXME: optimizing with pure C (indexing) rbtree
        for ( ; it != running_timers_.end(); )
        {
            auto expire_time = it->first;
            auto timer_hdl = it->second;
            ADK_NOTUSE(timer_hdl);
            if (time_val_now >= expire_time)
            {
                // pick expired timers
                expire_timers_[nr_expire_timers_] = it->second;
                it = running_timers_.erase(it);

                #ifdef __ADK_THREAD_TEST__
                std::cout << __FUNCTION__ << ", delete timer timer_id = " << timer_hdl.timer_id <<  ", running_timers_.size() = " << running_timers_.size() << ", "
                          << "expire_time = " << expire_time << std::endl;
                #endif

                ++nr_expire_timers_;
                if (ADK_UNLIKELY(nr_expire_timers_ >= ADK_THREAD_MAX_TIMERS))
                    break;

                continue;
            }
            break;
        }
    }

    if (ADK_UNLIKELY(nr_expire_timers_ == 0))
        return ;

    #ifdef __ADK_THREAD_TEST__
    std::cout << __FUNCTION__ << " nr_expire_timers_ = " << nr_expire_timers_ << std::endl;
    #endif

    uint32_t i = -1;
    do {
        if ((++i) >= nr_expire_timers_)
            break;

        auto& timer_hdl = expire_timers_[i];
        auto& timer = timers_[timer_hdl.timer_id];
        // SendMsg to the binding thread

        {
            ScopedSpinlock timer_lock_guard(timer.lock_);
            if (timer.version_ != timer_hdl.timer_version)
            {
                // deleted
                continue;
            }

            if (timer.state_ != TimerState::kRunning)
            {
                assert(timer.state_ == TimerState::kCancelled);
                continue;
            }

            if (timer.adjust_expire_time_ != 0)
            {
                // timer is rescheduled
                if (timer.run_timer_thr_ != NULL)
                {
                    assert(timer.state_ == TimerState::kRunning);
                    timer.expire_time_ = timer.adjust_expire_time_;
                    timer.adjust_expire_time_ = 0;
                    SchedTimerToRun(timer.expire_time_, timer_hdl);
                }
                continue;
            }

            timer.state_ = TimerState::kExpired;
        }

        try 
        {
            if (timer.run_timer_thr_ != NULL)
            {
                #ifdef __ADK_THREAD_TEST__
                std::cout << __FUNCTION__ << " send msg to thread " << timer.run_timer_thr_->thread_name()
                          <<  ", timer_id = " << timer_hdl.timer_id << std::endl;
                #endif

                auto timer_sig = TimerSignal::New();
                timer_sig->timer_hdl = timer_hdl;
                timer_sig->timer_mana = this;
                timer.run_timer_thr_->SendMsg(timer_sig);
                continue;
            }

            tls_timer_id = timer_hdl.timer_id;
            (timer.job_)(timer_hdl);
            tls_timer_id = -1;
        }
        catch (...)
        {
            // FIXME: boost::current_exception_diagnostic_information();
        }
    } while (true);

    time_val_now = timespec_now();
    i = -1;
    do {
        if ((++i) >= nr_expire_timers_)
            break;

        auto& timer_hdl = expire_timers_[i];
        auto& timer = timers_[timer_hdl.timer_id];

        if (timer.run_timer_thr_ != NULL)
            continue;

        ScopedSpinlock timer_lock_guard(timer.lock_);

        if (timer.version_ != timer_hdl.timer_version)
        {
            // deleted
            continue;
        }

        if (timer.state_ == TimerState::kCancelled
            || timer.state_ == TimerState::kCanceling)
        {
            timer.state_ = TimerState::kCancelled;
            continue;
        }

        timer.state_ = TimerState::kDone;

        if (timer.type_ == TimerType::kPeriod)
        {
            timer.adjust_expire_time_ = timer.expire_time_ + timer.interval_;
        }

        if (timer.adjust_expire_time_ != 0)
        {
            timer.state_ = TimerState::kRunning;
            timer.expire_time_ = timer.adjust_expire_time_;
            timer.adjust_expire_time_ = 0;
            SchedTimerToRun(timer.expire_time_, timer_hdl);
            continue;
        }
    } while (true);
    nr_expire_timers_ = 0;
}

void ThreadTimerManager::RunTimer(TimerHandler& timer_hdl)
{
    auto& timer = timers_[timer_hdl.timer_id];

    try 
    {
        tls_timer_id = timer_hdl.timer_id;
        (timer.job_)(timer_hdl);
        tls_timer_id = -1;
    }
    catch (...)
    {
        // FIXME: boost::current_exception_diagnostic_information();
    }

    ScopedSpinlock timer_lock_guard(timer.lock_);

    if (timer.version_ != timer_hdl.timer_version)
    {
        return;
    }

    if (timer.state_ == TimerState::kCancelled
        || timer.state_ == TimerState::kCanceling)
    {
        timer.state_ = TimerState::kCancelled;
        return;
    }

    timer.state_ = TimerState::kDone;

    if (timer.type_ == TimerType::kPeriod)
    {
        timer.adjust_expire_time_ = timer.expire_time_ + timer.interval_;
    }

    if (timer.adjust_expire_time_ != 0)
    {
        #ifdef __ADK_THREAD_TEST__
        std::cout << __FUNCTION__ << " schedule timer, timer_id = " << timer_hdl.timer_id << std::endl;
        #endif

        timer.state_ = TimerState::kRunning;
        timer.expire_time_ = timer.adjust_expire_time_;
        timer.adjust_expire_time_ = 0;
        SchedTimerToRun(timer.expire_time_, timer_hdl);
    }
}

void TimerSignal::Run()
{
    timer_mana->RunTimer(timer_hdl);
}

void ThreadBase::TimerHandler(TimerSignal* timer_sig)
{
    timer_sig->Run();
}

void ThreadTimerManager::TimerThreadMain()
{
    int64_t run_begin = timespec_now();
    uint64_t counter = 0;
    while (is_running_)
    {
        ++counter;
        int64_t time_to_sleep = run_begin + (1000000 * counter) - timespec_now();
        if (time_to_sleep > 0)
            usleep(time_to_sleep / 1000);
        RunTimerCycle();
    }
}

void ThreadTimerManager::Start()
{
    ScopedSpinlock join_lock_guard(join_lock_);
    {
        ScopedSpinlock mana_lock_guard(timer_mana_lock_);
        if (is_running_)
            return;

        is_running_ = true;
    }

    timer_thread_ = boost_thread("adk-threadtimer", "timer thread", std::bind(&ThreadTimerManager::TimerThreadMain, this));
}

void ThreadTimerManager::Finish()
{
    ScopedSpinlock join_lock_guard(join_lock_);
    {
        ScopedSpinlock mana_lock_guard(timer_mana_lock_);
        if (!is_running_)
            return;

        is_running_ = false;
    }

    if (timer_thread_.joinable())
        timer_thread_.join();
}

ThreadTimerManager::ThreadTimerManager()
{
    last_timer_index_ = 0;
    nr_expire_timers_ = 0;
    SpinlockInit(timer_mana_lock_);
    SpinlockInit(join_lock_);
}

ThreadTimerManager::~ThreadTimerManager()
{}

void ThreadBase::ThreadMain()
{
    SetCpuAffinity(thread_params_->thread_affinity);

    thread_context = new ThreadContext();
    thread_context->tag = thread_tag_;
    thread_context->instance = instance_id_;
    thread_context->message_handlers = message_handlers_;

    msg_budget_ = thread_params_->msg_budget;
    oob_msg_budget_ = thread_params_->oob_msg_budget;
    mq_cont_process_limit_ = thread_params_->mq_cont_process_limit;
    wait_timeout_ns_ = thread_params_->wait_timeout_ns;
    mq_srv_cnt_ = 0;
    oob_mq_srv_cnt_ = 0;
    msg_ptr_ = NULL;

    {
        // lock is not necessary.
        Init(true);
    }

    // FIXME: ADD Synchronize Point!!!!!!

    try 
    {
        OnRun();

        uint32_t mq_cont_deal_cnt = 0;
        while (is_running_)
        {
            if (ev_mana_->Wait([this, &mq_cont_deal_cnt](){
                    if (ADK_UNLIKELY(mq_srv_cnt_ >= msg_budget_))
                    {
                        if (PollOOBMessage() == ErrorCode::kSuccess)
                            return ErrorCode::kSuccess;
                    }

                    ++mq_srv_cnt_;
                    if ((mq_cont_deal_cnt < static_cast<uint32_t>(mq_cont_process_limit_))
                        && (thread_message_queue_[ADK_MQ]->Pop(msg_ptr_)
                            == ErrorCode::kSuccess))
                    {
                        DeliverMessage();
                        ++mq_cont_deal_cnt;
                        return ErrorCode::kSuccess;
                    }

                    mq_cont_deal_cnt = 0;
                    if (nr_release_cache_queues_ && PopCachedMessage())
                    {
                        is_cached_msg_ = true;
                        DeliverCachedMessage();
                        return ErrorCode::kSuccess;
                    }

                    mq_srv_cnt_ = msg_budget_;
                    msg_ptr_ = NULL;
                    return ErrorCode::kWouldblock;
                },

                [this](){
                    OnIdle();
                },
                wait_timeout_ns_) == adk_impl::ErrorCode::kSuccess)
            {
                #ifdef __ADK_THREAD_TEST__
                std::cout << "Information: " << thread_name_ << " receive messages, is_cached = " 
                          << is_cached_msg_ << " " << msg_ptr_ << std::endl;
                is_cached_msg_ = false;
                #endif

                continue;
            }
        }
    }
    catch (...)
    {
        OnError(ErrorCode::kThreadException,
                boost::current_exception_diagnostic_information());
        
        // FIXME: dump exception messages!
        
        g_on_error(ErrorCode::kThreadException,
                   boost::current_exception_diagnostic_information());
        while (is_running_)
        {
            DropAllMessages();
        }
    }
}

void ThreadBase::DropAllMessages()
{
    if (thread_message_queue_[ADK_MQ]->Pop(msg_ptr_) != ErrorCode::kSuccess)
        usleep(1);

    thread_message_queue_[ADK_OOB_MQ]->Pop(msg_ptr_);
}

void ThreadBase::Dump(boost::property_tree::ptree& ptree)
{
    ptree.put("nr_error_msg",   thr_stats_.nr_error_msg);
    ptree.put("nr_normal_msg",  thr_stats_.nr_normal_msg);
    ptree.put("nr_oob_msg",     thr_stats_.nr_oob_msg);
    ptree.put("nr_cached_msg",  thr_stats_.nr_cached_msg);
    thread_message_queue_[ADK_MQ]->Dump(ptree);
}

std::string ThreadBase::Dump(bool is_pretty)
{
    boost::property_tree::ptree ptree_tmp;
    Dump(ptree_tmp);
    return PtreeToString(ptree_tmp, is_pretty);
}

void ThreadManager::Dump(boost::property_tree::ptree& ptree)
{
    boost::recursive_mutex::scoped_lock lock_guard(thr_mana_rlock_);
    boost::property_tree::ptree& root_tree = ptree.add_child("Threads", boost::property_tree::ptree());
    for (auto& node_pair : exist_threads_)
    {
        if (node_pair.second.empty())
            continue;

        boost::property_tree::ptree& class_tree = root_tree.push_back(
                    boost::property_tree::ptree::value_type("", boost::property_tree::ptree()))->second;
        class_tree.put("Name", (*node_pair.second.begin())->thread_name());

        boost::property_tree::ptree& instance_array = class_tree.add_child("Instance", boost::property_tree::ptree());
        int32_t counter = 0;
        for (auto one_thread : node_pair.second)
        {
            boost::property_tree::ptree& instance_tree = instance_array.push_back(
                    boost::property_tree::ptree::value_type("", boost::property_tree::ptree()))->second;
            instance_tree.put("Id", counter);

            one_thread->Dump(instance_tree);
            ++counter;
        }
    }
}

std::string ThreadManager::Dump(bool is_pretty)
{
    boost::property_tree::ptree ptree_tmp;
    Dump(ptree_tmp);
    return PtreeToString(ptree_tmp, is_pretty);
}

template<typename CodeBlock, typename OnException>
bool HandleExceptions(const CodeBlock& code_block, const OnException& on_exception)
{
    try 
    {
        code_block();
        return false;
    }
    catch (...)
    {
        try 
        {
            on_exception(boost::current_exception_diagnostic_information());
        }
        catch(...) {}
    }
    return true;
}

#define THREAD_BASE_ERROR_HANDLER [this](const std::string& desc){   \
                                OnError(ErrorCode::kThreadException, desc); \
                            }

bool ThreadBase::Init(bool is_in_running_thread)
{
    bool ret = HandleExceptions(
        [this, is_in_running_thread](){
            {
                boost::mutex::scoped_lock lock_guard(thread_li_->common->lock);
                if (!(thread_li_->common->is_invoke))
                {
                    assert(thread_li_->common->ref_counter == 0);
                    atomic_inc(thread_li_->common->ref_counter);
                    is_do_once_job_ = true;
                    OnInitOnce();
                    thread_li_->common->is_invoke = true;
                }
            }

            if (!is_in_running_thread && thread_params_->parallel_init)
                return;

            if (!(thread_li_->is_launch))    
            {
                if (!is_do_once_job_)
                    atomic_inc(thread_li_->common->ref_counter);
                OnInit();
                thread_li_->is_launch = true;
            }
        },
        THREAD_BASE_ERROR_HANDLER
    );
    return ret;
}

void ThreadBase::Start()
{
    assert(thread_li_);
    assert(thr_mana_);

    boost::mutex::scoped_lock lock_guard(thr_lock_);

    bool has_exception = Init(false);
    if (has_exception)
        return;

    if (is_running_)
        return;
   
    is_running_ = true;
    std::string thread_fullname = thread_name_;
    thread_fullname += "_";
    thread_fullname += instance_id_;
    thread_ = boost_thread("adk-threadmain", thread_fullname.data(), std::bind(&ThreadBase::ThreadMain, this));
}

void ThreadBase::Stop()
{
    if (!is_running_)
        return;

    is_running_ = false;
}

void ThreadBase::Exit(bool is_in_running_thread)
{
    HandleExceptions(
        [this](){
            if (thread_li_->is_launch)    
            {
                OnExit();
                thread_li_->is_launch = false;
            }

            {
                boost::mutex::scoped_lock lock_guard(thread_li_->common->lock);
                if (atomic_dec(thread_li_->common->ref_counter) == 1
                    && thread_li_->common->is_invoke)
                {
                    OnExitOnce();
                    thread_li_->common->is_invoke = false;
                }
            }
        },
        THREAD_BASE_ERROR_HANDLER
    );
}

void ThreadBase::Finish()
{
    assert(thread_li_);
    assert(thr_mana_);
    assert(ev_mana_);

    boost::mutex::scoped_lock lock_guard(thr_lock_);

    if (thread_.joinable())
    {
        is_running_ = false;
        ev_mana_->ReleaseWaitThread();   // FIXME: add reset method
        thread_.join();
    }
    assert(is_running_ == false);

    Exit(false);
}

_RegisterHelper& _RegisterHelper::operator()(
               const CreatorInfo& creator_info,
               BOOST_PP_ENUM_PARAMS(
                    ADK_THREAD_PROPERTY_NUM,
                    const GenericArg& arg)
               )
{
    if (!is_reg_)
    {
        ThreadParams* params = new ThreadParams();

        #define arg_assigment(z, n, unused)   \
            ArgAssigment(params, arg##n);

        BOOST_PP_REPEAT(ADK_THREAD_PROPERTY_NUM, arg_assigment, ~)

        #undef arg_assigment

        ThreadManager::Instance(reg_base_->ns())
                            ->AddCreator(creator_info.creator_name,
                                         creator_info.creator,
                                         params);
    }
    return *this;
}
} // adk

