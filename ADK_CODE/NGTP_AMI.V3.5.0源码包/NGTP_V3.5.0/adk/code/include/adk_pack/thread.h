/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/
#ifndef ADK_THREAD_H_
#define ADK_THREAD_H_

#include "event.h"
#include "generic_gc.h"
#include "error_code.h"
#include "generic_arg.h"
#include "object_pool.h"
#include "macros.h"
#include <bitset>

#include <map>
#include <string>
#include <vector>

#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <boost/function.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/preprocessor/repetition/enum_params_with_a_default.hpp>

namespace adk
{

#ifndef ADK_MAX_THREAD_CLASS
#define ADK_MAX_THREAD_CLASS 64
#endif // ADK_MAX_THREAD_CLASS

#ifndef ADK_MAX_THREAD_INSTANCE
#define ADK_MAX_THREAD_INSTANCE 16
#endif  // ADK_MAX_THREAD_INSTANCE

#ifndef ADK_MAX_THREAD_MESSAGES
#define ADK_MAX_THREAD_MESSAGES 1024
#endif  // ADK_MAX_THREAD_MESSAGES

#ifndef ADK_THREAD_MESSAGES_BUDGET
#define ADK_THREAD_MESSAGES_BUDGET 16
#endif  // ADK_THREAD_MESSAGES_BUDGET

#ifndef ADK_THREAD_CACHED_MESSAGES_BUDGET
#define ADK_THREAD_CACHED_MESSAGES_BUDGET (ADK_THREAD_MESSAGES_BUDGET / 2)
#endif  // ADK_THREAD_CACHED_MESSAGES_BUDGET

#ifndef ADK_MAX_THREAD_OOB_MESSAGES
#define ADK_MAX_THREAD_OOB_MESSAGES 128
#endif  // ADK_MAX_THREAD_OOB_MESSAGES

#define ADK_MAX_TOTAL_THREAD_MESSAGES (ADK_MAX_THREAD_MESSAGES + ADK_MAX_THREAD_OOB_MESSAGES)

#ifndef ADK_THREAD_OOB_MESSAGES_BUDGET
#define ADK_THREAD_OOB_MESSAGES_BUDGET 2
#endif  // ADK_THREAD_OOB_MESSAGES_BUDGET

#ifndef ADK_THREAD_MQ_CONT_PROCESS_LIMIT
#define ADK_THREAD_MQ_CONT_PROCESS_LIMIT 16
#endif  // ADK_THREAD_MQ_CONT_PROCESS_LIMIT

#ifndef ADK_THREAD_MESSAGES_POOL_SIZE
#define ADK_THREAD_MESSAGES_POOL_SIZE (8192)
#endif  // ADK_THREAD_MESSAGES_POOL_SIZE

#ifndef ADK_THREAD_MESSAGES_QUEUE_SIZE
#define ADK_THREAD_MESSAGES_QUEUE_SIZE (8192)
#endif  // ADK_THREAD_MESSAGES_QUEUE_SIZE

#ifndef ADK_THREAD_OOB_MESSAGES_QUEUE_SIZE
#define ADK_THREAD_OOB_MESSAGES_QUEUE_SIZE 128
#endif  // ADK_THREAD_OOB_MESSAGES_QUEUE_SIZE

// keep ADK_THREAD_MAX_TIMERS to be power of 2
#ifndef ADK_THREAD_MAX_TIMERS 
#define ADK_THREAD_MAX_TIMERS   16384
#endif // ADK_THREAD_MAX_TIMERS 

#define ADK_THREAD_MAX_MESSAGE_TAG 2048

#define ADK_MESSAGE_BASE_TYPE   0

#define ADK_INVALID_THREAD_INSTANCE_ID  -1

#define ADK_MQ              0
#define ADK_OOB_MQ          1
#define ADK_MQ_PER_THREAD   2

namespace thread
{
/**
 * @brief      ADK线程所支持的配置属性
 */
static GenericArg InstanceNumber("InstanceNumber");             ///< 线程类的实例数量
static GenericArg InitPriority("InitPriority");                 ///< 线程类的初始化优先级
static GenericArg EventMode("EventMode");                       ///< 线程类的事件处理模式
static GenericArg MessageBudget("MessageBudget");               ///< 线程类持续处理普通消息的配额
static GenericArg MessageBudgetOOB("MessageBudgetOOB");         ///< 线程类持续处理带外消息的配额
static GenericArg BackoffLimit("BackoffLimit");                 ///< Polling时的退避次数上线
static GenericArg BusyPollNano("BusyPollNano");                 ///< kInterrupt模式下，线程忙等的时间
static GenericArg ParallelInit("ParallelInit");                 ///< 并行调用OnInit和OnExit
// 增加参数的修改点，请搜索关键字 "AddParameters"
static GenericArg ThreadAffinity("ThreadAffinity");             ///< 配置线程的CPU亲和度
static GenericArg MqContProcessLimit("MqContProcessLimit");     ///< 每递交N条普通消息，进行一次Cahce消息递交
static GenericArg WaitTimeoutNano("WaitTimeoutNano");           ///< 等待消息递交的超时时间，超时后会回调 OnIdle


// "AddParameters"
#define ADK_THREAD_PROPERTY_NUM     9           
#define ADK_THREAD_MESSAGE_HANDLER_NUM     30

constexpr static int32_t kPolling = 0;                          ///< 忙等模式,响应时延低,但会占用较多的CPU资源
constexpr static int32_t kInterrupt = 1;                        ///< 中断模式,响应时延高,占用CPU资源少

inline constexpr int64_t Nanoseconds(const int64_t val) { return val; }
inline constexpr int64_t Microseconds(const int64_t val) { return val * 1000l; }
inline constexpr int64_t Milliseconds(const int64_t val) { return val * 1000l * 1000l; }

static GenericArg place_holder("place_holder");  
constexpr const char* kThreadGCName = "adk_thread_gc";

template<typename T>
T GetClassType(T* a)
{
    return *a;
}
}

extern int32_t AllocMessageType(bool);

template<typename AppMessageType>
class ThreadMessage;

class ThreadGcBase : public IObject, public GCRequest
{
protected:
    ThreadGcBase();

    virtual void DoGC() override;

    void hold_reference();

    void put_reference();

    bool drop_reference();

    const char* message_type_name_;
    int32_t message_type_;
    int32_t ref_counter_;
    int32_t message_tag_;
};

/**
 * @brief      所有线程消息均继承自该类
 *
 * @tparam     AppMessageType  线程消息类型
 */
template<typename AppMessageType>
class ThreadMessageBase : public ThreadGcBase
{
public:
    /**
     * @brief      获取消息所属的分类
     *
     * @return     返回消息分类
     */
    int32_t message_tag() { return message_tag_; }

    /**
     * @brief      设定消息分类
     *
     * @param[in]  tag  消息分类标签
     * 
     * @note       ID取值范围为[0, ADK_THREAD_MAX_MESSAGE_TAG-1],目前仅支持2048个分类
     */
    void set_message_tag(int32_t tag) { message_tag_ = tag; }

    /**
     * @brief      分配一个线程消息对象
     *
     * @return     返回线程消息对象的资源管理对象
     * 
     * @note       该接口线程安全,并且不会失败
     */
    static ThreadMessage<AppMessageType> New(bool gc_imm_start = gc::kImmediateStart)
    {       
        static ObjectPool<AppMessageType>* s_obj_pool = ObjectPool<AppMessageType>::Create(AppMessageType::TypeName(), 
            ADK_THREAD_MESSAGES_POOL_SIZE);

        static GCAgent* s_gc_agent = GenericGC::CreateGCAgent(thread::kThreadGCName, 
                                                              gc::kSharedChannel,
                                                              gc_imm_start);
        s_obj_pool2 = s_obj_pool;
        s_gc_agent2 = s_gc_agent;
        return ThreadMessage<AppMessageType>(s_obj_pool->NewObjectEx());
    }

    /**
     * @brief      分配一个线程消息对象
     *
     * @return     返回线程消息对象的资源管理对象
     * 
     * @note       该接口线程安全,并且不会失败,该接口不能在全局变量的构造函数中使用
     */
    static ThreadMessage<AppMessageType> NewUnsafe()
    {
        assert(s_obj_pool2);       
        return ThreadMessage<AppMessageType>(s_obj_pool2->NewObjectEx());
    }

    int32_t message_type() { return message_type_; }
    
    const char* message_type_name() { return message_type_name_; }

protected:
    ThreadMessageBase() = default;

    void put_reference()
    {
        if (ThreadGcBase::drop_reference())
            s_gc_agent2->PushGCRequest(this);
    }

    static ObjectPool<AppMessageType>* s_obj_pool2;
    static GCAgent* s_gc_agent2;

    template<typename T>
    friend class ThreadMessage;

    template<typename T>
    friend class ObjectPool;

    friend class ThreadBase;
    friend class ThreadManager;
};

template<typename AppMessageType>
GCAgent* ThreadMessageBase<AppMessageType>::s_gc_agent2;

template<typename AppMessageType>
ObjectPool<AppMessageType>* ThreadMessageBase<AppMessageType>::s_obj_pool2;

template<typename AppMessageType>
class ThreadMessage
{
public:
    ThreadMessage(ThreadMessageBase<AppMessageType>* mbase)
        :   message_base_(mbase)
    {
        // if (message_base_ != NULL)
        assert(message_base_);
        message_base_->hold_reference();
    }

    ThreadMessage(AppMessageType* mbase)
        :   message_base_(mbase)
    {
        assert(message_base_);
        message_base_->hold_reference();
    }

    ~ThreadMessage()
    {
        message_base_->put_reference();
    }

    AppMessageType& operator*()
    {
        return *((AppMessageType*)message_base_);
    }

    AppMessageType* operator->()
    {
        return (AppMessageType*)message_base_;
    }

private:
    ThreadMessageBase<AppMessageType>* message_base_;

    ThreadMessageBase<AppMessageType>* message_base() const { return message_base_; }

    friend class ThreadManager;
    friend class ThreadBase;
};

class OnceObject
{
public:
    template<typename callable>
    OnceObject(const callable& cb)
    {
        static bool is_init = false;
        if (!is_init)
        {
            cb();
            is_init = true;
        }
    }
};

#define _ADK_DEFINE_THREAD_MESSAGE(AppThreadMessage, oob)     \
    template<typename T>    \
    class AppThreadMessage##Basic : public adk::ThreadMessageBase<T>  \
    {   \
    public: \
        AppThreadMessage##Basic()   \
        {   \
            static int32_t s_message_type =     \
                    adk::AllocMessageType(oob); \
            adk::ThreadMessageBase<T>::message_type_ = s_message_type; \
            adk::ThreadMessageBase<T>::message_type_name_ = #AppThreadMessage;  \
        }   \
        static int32_t TypeId()     \
        {   \
            AppThreadMessage##Basic obj;    \
            return obj.message_type();   \
        }   \
        static const char* TypeName()   \
        {   \
            AppThreadMessage##Basic obj;    \
            return obj.message_type_name();   \
        }   \
    };  \
    class AppThreadMessage; \
    static adk::OnceObject uniq_##AppThreadMessage([](){ \
        AppThreadMessage##Basic<AppThreadMessage>::New(adk::gc::kDelayStart); \
    }); \
    class AppThreadMessage final: public AppThreadMessage##Basic<AppThreadMessage>

/**
 * @brief      定义一个带外线程消息
 *
 * @param      AppThreadMessage  线程消息的类名
 */
#define ADK_OOB_THREAD_MESSAGE(AppThreadMessage)  \
            _ADK_DEFINE_THREAD_MESSAGE(AppThreadMessage, true)

/**
 * @brief      定义一个纯粹的带外线程消息，即该消息没有自定义字段
 *
 * @param      AppThreadMessage  线程消息的类名
 */
#define ADK_PURE_OOB_THREAD_MESSAGE(AppThreadMessage)   \
            _ADK_DEFINE_THREAD_MESSAGE(AppThreadMessage, true) {}

ADK_PURE_OOB_THREAD_MESSAGE(AsyncSignal);

/**
 * @brief      定义一个普通线程消息
 *
 * @param      AppThreadMessage  线程消息的类名
 */
#define ADK_THREAD_MESSAGE(AppThreadMessage) \
            _ADK_DEFINE_THREAD_MESSAGE(AppThreadMessage, false)

class GenericThread
{
public:
    void OnMessage(void*) {}
    typedef decltype(&GenericThread::OnMessage) HandlerType;

    static HandlerType* DoAllocBuffer(uint32_t nr_entries);

    template<typename T>
    static HandlerType* AllocBuffer()
    {
        static HandlerType* ret = DoAllocBuffer(ADK_MAX_TOTAL_THREAD_MESSAGES);
        return &ret[ADK_MAX_THREAD_OOB_MESSAGES];
    }

    template<typename T>
    static char* AllocMemoryBuffer(uint32_t size)
    {
        static char* ret = new char[size]();
        return ret;
    }

    template<typename T>
    static int32_t AllocThreadTag()
    {
        static int32_t tag = AllocTag();
        return tag;
    }

protected:
    static int32_t AllocTag();
};

#define DEFINE_PARAM(type, name) type name; bool is_default_##name;
#define INIT_PARAM(name, value) name = value; is_default_##name = true
#define COPY_PARAM(name) \
        if (!(other.is_default_##name)) \
        {   \
            name = other.name;  \
            is_default_##name = false;  \
        }

// "AddParameters"
struct ThreadParams
{
    DEFINE_PARAM(int16_t,     msg_budget)
    DEFINE_PARAM(int16_t,     oob_msg_budget)
    DEFINE_PARAM(int16_t,     mq_cont_process_limit)
    DEFINE_PARAM(int32_t,     number_instance)
    DEFINE_PARAM(int32_t,     event_mode)
    DEFINE_PARAM(int32_t,     init_priority)
    DEFINE_PARAM(int32_t,     busy_poll_ns)
    DEFINE_PARAM(uint32_t,    wait_timeout_ns)
    DEFINE_PARAM(int32_t,     backoff_limit)
    DEFINE_PARAM(bool,        parallel_init)
    DEFINE_PARAM(std::string, thread_affinity)

    int32_t     number_create_instance;
    bool        is_default;

    ThreadParams();

    void Reset();

    ThreadParams& operator= (const ThreadParams& other);
};

std::ostream& operator<< (std::ostream& stream, const adk::ThreadParams* params);

class ThreadManager;
class ThreadBase;
typedef boost::function<ThreadBase* (ThreadManager*, 
                                     ThreadParams*,
                                     int32_t,
                                     ThreadParams**)> ThreadCreatorType;
struct ThreadInfo
{
    ThreadCreatorType creator;
    ThreadParams* params;
};

struct LaunchInfoCommon
{
    LaunchInfoCommon()
        :   is_invoke(false),
            ref_counter(0)
    {}

    bool            is_invoke;
    uint32_t        ref_counter;
    boost::mutex    lock;
};

struct LaunchInfo
{
    LaunchInfo(ThreadBase* arg1,
               bool arg2,
               LaunchInfoCommon* arg3)
        :   app_thread(arg1),
            is_launch(arg2),
            common(arg3)
    {}
    ThreadBase*     app_thread;
    bool            is_launch;
    LaunchInfoCommon* common;
};

/**
 * @brief      线程管理类
 */
class ThreadManager
{
public:
    typedef boost::function<void(int32_t error_value,
                                 const std::string& error_message)> ErrorHandlerType;
    static void set_on_init(const boost::function<int32_t()>& on_init);
    static void set_on_idle(const boost::function<void()>& on_idle);
    static void set_on_error(const ErrorHandlerType& on_error);
    static void set_on_exit(const boost::function<void()>& on_exit);

    /**
     * @brief      获取线程管理类的实例
     *
     * @param      ns    线程的命名空间
     *
     * @return     返回线程管理类实例指针
     */
    static ThreadManager* Instance(const std::string& ns);

    /**
     * @brief      获取Default线程管理类实例
     *
     * @return     返回Default线程管理类实例的指针
     * 
     * @note       该接口不能在全局变量的构造函数内使用
     */
    static ThreadManager* Instance() { assert(thr_mana_); return thr_mana_; };

    /**
     * @brief      获取线程类的实例
     *
     * @param[in]  instance_id  实例ID，从0开始连续递增
     *
     * @tparam     ThreadClass  线程类
     *
     * @return     返回线程类的实例指针
     */
    template<typename ThreadClass>
    ThreadClass* ThreadInstance(int32_t instance_id = 0) 
    { 
        return reinterpret_cast<ThreadClass*>(ThreadInstance(ThreadClass::Tag(), instance_id));
    }

    /**
     * @brief      修改线程类的参数配置
     *
     * @param[in]  parameters   key=value模式赋值，key为参数名称，value为具体值
     *
     * @tparam     ThreadClass  线程类
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    template<typename ThreadClass>
    int32_t ChangeParams(BOOST_PP_ENUM_PARAMS_WITH_A_DEFAULT(
                                ADK_THREAD_PROPERTY_NUM,
                                const GenericArg& arg,
                                adk::thread::place_holder)
                        );

    /**
     * @brief      实例化所有被Manager管理的线程对象
     * 
     * @note       没有特殊需求时，应用可直接调用Start()接口，本接口可以忽略不调
     */
    void Init();

    /**
     * @brief      启动所有被管理的线程
     * 
     * @note       InitPriority大的线程类会优先启动
     */
    void Start();

    /**
     * @brief      停止所有被管理的线程
     * 
     * @note       InitPriority小的线程会优先停止, 该接口非阻塞, 该接口返回时可能还有
     *             被管理线程未退出
     */
    void Stop();

    /**
     * @brief      同步终止所有被管理的线程
     * 
     * @note       该接口会阻塞，该接口返回时，所有被管理线程均已退出
     */
    void Finish();

    /**
     * @brief      向线程发送消息
     *
     * @param[in]  msg               线程消息
     * @param[in]  instance_id       线程ID
     *
     * @tparam     ThreadClass       线程类
     * @tparam     isBlock           isBlock send progress, true by default.
     * 
     * @note       该接口线程安全
     *
     * @return     成功时返回kSuccess
     */
    template<typename ThreadClass, bool isBlock = true, typename AppThreadMessage>
    int32_t SendMsg(const ThreadMessage<AppThreadMessage>& msg, int32_t instance_id = 0)
    {
        return SendMsg<isBlock, ThreadClass>(msg.message_base(), instance_id);
    }

    /**
     * @brief      向线程发送消息
     *
     * @param[in]  msg               线程消息
     * @param[in]  instance_id       线程ID
     *
     * @tparam     ThreadClass       线程类
     * @tparam     isBlock           isBlock send progress, true by default.
     *
     * @note       该接口线程安全, 该接口对线程是否存在不做检查，应用需保证线程类被注册
     *
     * @return     成功时返回kSuccess
     */
    template<typename ThreadClass, bool isBlock = true, typename AppThreadMessage>
    int32_t SendMsgUnsafe(const ThreadMessage<AppThreadMessage>& msg, int32_t instance_id = 0)
    {
        return SendMsgUnsafe<isBlock, ThreadClass>(msg.message_base(), instance_id);
    }

    /**
     * @brief      重载接口
     */
    template<bool isBlock = true, typename ThreadClass, typename AppThreadMessage>
    int32_t SendMsg(AppThreadMessage* msg, int32_t instance_id = 0);

    /**
     * @brief      重载接口
     * 
     * @note       该接口线程安全, 该接口对线程是否存在不做检查，应用需保证线程类被注册
     */
    template<bool isBlock = true, typename ThreadClass, typename AppThreadMessage>
    int32_t SendMsgUnsafe(AppThreadMessage* msg, int32_t instance_id = 0);

    /**
     * @brief      释放某一分类的消息
     *
     * @param[in]  tag          消息分类的标签，标签的可选区间为[0,63]
     *
     * @tparam     ThreadClass  线程类
     * 
     * @note       该接口异步执行，因此可以在消息处理句柄之外使用
     */
    template<typename ThreadClass>
    int32_t ReleaseMessageProcess(uint64_t tag, int32_t instance_id = 0);

    /**
     * @brief      获取线程的配置参数
     *
     * @tparam     ThreadClass  线程类
     *
     * @return     返回线程的配置参数
     */
    template<typename ThreadClass>
    ThreadParams*& GetParms()
    {
        return threads_params_[ThreadClass::Tag()];
    }

    void Dump(boost::property_tree::ptree& ptree);

    std::string Dump(bool is_pretty = false);

private:
    ThreadManager();

    ~ThreadManager();

    void AddCreator(const std::string& name,
                    const ThreadCreatorType& creator,
                    ThreadParams* params);

    int32_t AddThread(ThreadBase* app_thread);

    int32_t BuildLauchInfo(ThreadBase* app_thread);

    int32_t ChangeParams(ThreadParams* params, 
                           BOOST_PP_ENUM_PARAMS_WITH_A_DEFAULT(
                                ADK_THREAD_PROPERTY_NUM,
                                const GenericArg& arg,
                                adk::thread::place_holder)
                        );

    ThreadBase* ThreadInstance(int32_t thr_tag, int32_t instance_id);

    ThreadBase** threads_;
    boost::recursive_mutex thr_mana_rlock_;
    std::map<std::string, ThreadInfo> thread_infos_;
    int32_t nr_threads_;
    typedef std::map<int32_t, std::vector<LaunchInfo*> > LaunchInfoMapType;
    LaunchInfoMapType launch_map[2];
    ThreadParams* threads_params_[ADK_MAX_THREAD_CLASS];
    std::map<int32_t, std::vector<ThreadBase*>> exist_threads_;
    bool is_init_;

    static ThreadManager* thr_mana_;

    friend class _RegisterHelper;
    friend class ThreadBase;
    friend class ThreadTimerManager;
};

// ===========================================================================
enum TimerType
{
    kPeriod,
    kOneShot,
};
typedef uint32_t TimerIdType;
typedef uint32_t TimerVersion;
struct TimerHandler
{
    TimerIdType           timer_id;
    TimerVersion          timer_version;
};
typedef boost::function<void(TimerHandler&)> TimerJobType;
typedef int64_t TimeValue;

enum TimerState
{
    kInit,
    kRunning,
    kExpired,
    kCanceling,
    kDone,
    kCancelled,
};

enum TimerOffset
{
    kLast,
    kNow,
};

constexpr TimerIdType kInvalidTimerId = -1;
constexpr TimerHandler kInvalidTimerHandler = {kInvalidTimerId, 0};

class ThreadTimer
{
private:
    ThreadTimer();

    bool                is_used_;
    pthread_spinlock_t  lock_;
    TimerVersion        version_;
    TimerIdType         id_;
    TimerState          state_;
    TimerType           type_;

    TimeValue           interval_;
    TimeValue           expire_time_;
    TimeValue           adjust_expire_time_;
    ThreadBase*         run_timer_thr_;
    TimerJobType        job_;
    friend class ThreadTimerManager;
};

// ===========================================================================

class ThreadUtil
{
public:
    static void* CreateMPSCQueue(const std::string& name, uint32_t entry_payload_size, uint32_t queue_size);

    static void* CreateSPSCQueue(const string& name, uint32_t entry_payload_size);

    static void* CreateEventManage(int32_t busy_poll_ns, int32_t backoff_limit);
};

class TimerSignal;
/**
 * @brief      所有线程类的基类
 */
class ThreadBase
{
public:
    template<typename ThreadType>
    static ThreadBase* Create(ThreadManager* thr_mana,
                              ThreadParams* params, 
                              int32_t instance_id = 0)
    {
        assert(thr_mana);
        assert(params);

        ThreadType* new_thread = new ThreadType();
        new_thread->instance_id_ = instance_id;
        new_thread->thread_params_ = params;
        new_thread->thr_mana_ = thr_mana;
        new_thread->thread_message_queue_[ADK_MQ] = 
            ThreadUtil::CreateMPSCQueue(new_thread->thread_name_, 
                sizeof(void*), ADK_THREAD_MESSAGES_QUEUE_SIZE);

        new_thread->thread_message_queue_[ADK_OOB_MQ] = 
            ThreadUtil::CreateMPSCQueue(std::string("OOB_") + new_thread->thread_name_,
                sizeof(void*), ADK_THREAD_OOB_MESSAGES_QUEUE_SIZE);

        new_thread->release_queue_ = ThreadUtil::CreateSPSCQueue("ReleaseThread", 
            ADK_THREAD_MAX_MESSAGE_TAG * 2);

        new_thread->ev_mana_ = ThreadUtil::CreateEventManage(
            params->busy_poll_ns, params->backoff_limit);

        new_thread->gc_agent_ = GenericGC::CreateGCAgent(thread::kThreadGCName,
            gc::kDedicatedChannel);

        new_thread->InternalInit();

        if (thr_mana->AddThread(new_thread) != ErrorCode::kSuccess)
        {
            delete new_thread;
            new_thread = NULL;
        }
            
        return new_thread;
    }

    virtual ~ThreadBase()
    {}
    
    /**
     * @brief      该接口用于实现该线程类的公共初始化部分
     *
     * @note       对于多个线程类实例也仅初始化一次, 并且在所有线程实例的OnInit之前调用, 
     *             OnInitOnce会在实例线程启动前运行
     *
     * @return     初始化成功时返回 ErrorCode::kSuccess, 初始化失败时返回 ErrorCode::kFailure
     */
    virtual int32_t OnInitOnce() { return ErrorCode::kSuccess; }

    /**
     * @brief      该接口用于实现线程类实例的初始化
     *
     * @note       对于多个线程类实例会各初始化一次, OnInit会在实例线程启动前运行
     *
     * @return     初始化成功时返回 ErrorCode::kSuccess, 初始化失败时返回 ErrorCode::kFailure
     */
    virtual int32_t OnInit();

    /**
     * @brief      用户自定义的线程运行接口, 可实现用户自定义的事件循环
     */
    virtual void OnRun() { }

    /**
     * @brief      框架自带的事件循环中，当线程Idle时调用该接口
     */
    virtual void OnIdle();

    /**
     * @brief      当线程有异常发生时会调用该接口通知应用
     */
    virtual void OnError(int32_t error_value, const std::string& error_message);

    /**
     * @brief      该接口用于实现该线程类的公共退出部分
     *
     * @note       对于多个线程类实例也仅调用一次，OnExitOnce会在OnExit之后调用，OnExitOnce
     *             会在线程实例停止运行之后调用
     */
    virtual void OnExitOnce() {}

    /**
     * @brief      该接口用于实现该线程类的退出部分
     *
     * @note       对于多个线程类实例会分别调用一次，OnExit会在OnExitOnce之前调用，OnExit
     *             会在线程实例停止运行之后调用
     */
    virtual void OnExit();

    void Start();

    void Stop();

    void Finish();

    template<bool isBlock = true, typename AppThreadMessage>
    inline int32_t SendMsg(AppThreadMessage* msg);

    template<bool isBlock = true, typename AppThreadMessage>
    inline int32_t SendMsg(const ThreadMessage<AppThreadMessage>& msg) 
    {
        return ThreadBase::SendMsg<isBlock>(msg.message_base());
    }

    /**
     * @brief      判断线程是否需要退出
     *
     * @return     线程继续运行时返回true，线程需要退出时返回false
     */
    bool is_running() { return is_running_; }

    /**
     * @brief      返回线程实例间的共享数据
     *
     * @tparam     T     共享数据的类型定义
     *
     * @return     返回共享数据指针
     */
    template<typename T>
    T* get_thread_shared() { return (T*)s_thread_shared; }

    /**
     * @brief      设置线程实例间的共享数据
     *
     * @param      shared_buf  共享数据指针
     */
    void set_thread_shared(void* shared_buf) { s_thread_shared = shared_buf; }

    /**
     * @brief      在消息处理回调中使用，获取线程实例的ID
     *
     * @return     返回线程实例的ID，从0开始连续编码
     */
    int32_t instance_id() { return instance_id_; }

    /**
     * @brief      在初始化和退出流程中使用，获取线程实例的数量
     *
     * @return     返回线程实例的数量
     */
    int32_t number_instance() { assert(thread_params_); return thread_params_->number_instance; }

    /**
     * @brief      阻塞某一分类的消息
     *
     * @note       当某一分类的消息被阻塞后，该分类相关的消息会被阻塞，其它消息分类则不受影响，
     *             该接口只能在消息处理句柄中使用。该接口会阻塞句柄中当前正在处理的消息（即句柄
     *             回调参数中的消息）。
     *
     * @param[in]  tag  消息分类的标签，标签的可选区间为[0,63]
     */
    void BlockMessageProcess(uint64_t tag);

    /**
     * @brief      释放某一分类的消息
     *
     * @note       当某一分类的消息被释放后，该分类相关的消息不再被阻塞，该接口只能在消息处理
     *             句柄中使用。调用该接口后，阻塞期间投递给该线程的所有消息会按照当初的投递顺
     *             序再次回调响应的处理句柄
     *      
     *
     * @param[in]  tag  消息分类的标签，标签的可选区间为[0,63]
     */
    void ReleaseMessageProcess(uint64_t tag);

    const char* thread_name() { return thread_name_; }

    void Dump(boost::property_tree::ptree& ptree);

    std::string Dump(bool is_pretty = false);

    void SignalHandler(AsyncSignal*);

    void TimerHandler(TimerSignal*);

    static int32_t error_value();

    static int32_t error_message();

protected:
    ThreadBase() 
    {
        is_running_ = false;
        thread_message_queue_[ADK_OOB_MQ] = NULL;
        thread_message_queue_[ADK_MQ] = NULL;
        message_handlers_ = NULL;
        thread_params_ = NULL;
        thr_mana_ = NULL;
        thread_name_ = NULL;
        thread_li_ = NULL;
        mq_srv_cnt_ = 0;
        oob_mq_srv_cnt_ = 0;
        msg_ptr_ = NULL;
        msg_budget_ = ADK_THREAD_MESSAGES_BUDGET;
        oob_msg_budget_ = ADK_THREAD_OOB_MESSAGES_BUDGET;
        mq_cont_process_limit_ = ADK_THREAD_MQ_CONT_PROCESS_LIMIT;
        wait_timeout_ns_ = 100000;
        ev_mana_ = NULL;
        release_queue_ = NULL;
        nr_release_cache_queues_ = 0;
        is_cached_msg_ = false;
        block_map_.reset();
        cache_map_.reset();
        current_cache_queue_ = NULL;
        nr_cq_srv_cnt_ = 0;
        nr_cq_srv_budget_ = ADK_THREAD_CACHED_MESSAGES_BUDGET;
        gc_agent_ = NULL;
        is_do_once_job_ = false;
    }

    struct CacheQueueInfo
    {
        void*       cache_queue;
        bool        release_alert;
        uint32_t    tag;
        std::string queue_name;

        CacheQueueInfo()
        {
            cache_queue = NULL;
            release_alert = false;
            tag = 0;
            queue_name = "";
        }
    };

    volatile bool is_running_;
    bool        is_cached_msg_;
    int32_t     instance_id_;
    int16_t     mq_srv_cnt_;
    int16_t     oob_mq_srv_cnt_;
    int16_t     msg_budget_;
    int16_t     oob_msg_budget_;
    int16_t     mq_cont_process_limit_;
    uint32_t    wait_timeout_ns_;
    void*       ev_mana_;
    void*       thread_message_queue_[ADK_MQ_PER_THREAD];
    void*       msg_ptr_;
    std::bitset<ADK_THREAD_MAX_MESSAGE_TAG>    cache_map_;
    std::bitset<ADK_THREAD_MAX_MESSAGE_TAG>    block_map_;
    GenericThread::HandlerType* message_handlers_;
    GCAgent*    gc_agent_;
    struct ThreadStats
    {
        uint64_t nr_normal_msg;
        uint64_t nr_oob_msg;
        uint64_t nr_cached_msg;
        uint64_t nr_error_msg;
        char     rsrv[64 - (sizeof(uint64_t) * 4)];
        ThreadStats()
        {
            nr_normal_msg = 0;
            nr_oob_msg = 0;
            nr_cached_msg = 0;
            nr_error_msg = 0;   
        }
    };
    ThreadStats thr_stats_;
    // =============================================================
    void** current_cache_queue_;
    void*  release_queue_;
    int16_t     nr_cq_srv_cnt_;
    int16_t     nr_cq_srv_budget_;
    uint32_t    nr_release_cache_queues_;
    CacheQueueInfo cache_queues_[ADK_THREAD_MAX_MESSAGE_TAG];
	
    // =============================================================
    boost::mutex  thr_lock_;
    boost::thread thread_;  
    ThreadManager* thr_mana_;           // FIXME: rarely used?
    ThreadParams* thread_params_;
    const char* thread_name_;
    std::string thread_desc_;
    int32_t     thread_tag_;
    LaunchInfo* thread_li_;
    bool        is_do_once_job_;

    bool Init(bool is_in_running_thread);
    void Exit(bool is_in_running_thread);
    void set_launch_info(LaunchInfo* li) { thread_li_ = li; }

    void ThreadMain();
    void DeliverMessage();
    void DeliverOOBMessage();
    void DeliverCachedMessage();
    int32_t PollOOBMessage();
    void Accept(void* msg);
    int32_t AcceptNoBlock(void* msg);
    bool PopCachedMessage();
    inline void AllocCacheQueue(int32_t tag)
    {
        if (cache_queues_[tag].cache_queue == NULL)
        {
            DoAllocCacheQueue(tag);
        }
    }
    void DoAllocCacheQueue(int32_t tag);
    void OnMessageDefault(void*);
    void AsyncReleaseMessageProcess(uint64_t tag);
    void DropAllMessages();

    virtual void InternalInit() {};

    static void* s_thread_shared;

    friend class ThreadManager;
    friend class GenericThread;
};

class ThreadTimerManager
{
public:
    ThreadTimerManager();

    ~ThreadTimerManager();

    /**
     * @brief      创建一个线程本地Timer
     *
     * @param[in]  timer_type       Timer类型
     *                              TimerType::kPeriod  周期触发
     *                              TimerType::kOneShot 单次触发
     * @param[in]  timer_job        Timer任务，void (TimerHandler&)
     * @param[in]  duration         Timer的相对当前时间的相对到期时间，目前支持的最小精度为毫秒
     *                              adk::thread::Milliseconds(1000)
     * @param[in]  thread_instance  线程实例ID
     * @param[in]  ns               线程管理类的命名空间
     *
     * @tparam     ThreadClass      线程类
     * 
     * @note   当duration非0时，创建并启动Timer。否则创建Timer
     *
     * @return     成功时返回Timer句柄，失败时返回 adk::kInvalidTimerHandler
     */
    template<typename ThreadClass>
    TimerHandler CreateTimer(TimerType              timer_type,
                             const TimerJobType&    timer_job,
                             TimeValue              duration = 0,
                             uint32_t               thread_instance = 0,
                             const std::string&     ns = "")
    {
        return CreateTimer(timer_type,
                           timer_job,
                           duration,
                           ThreadManager::Instance(ns),
                           ThreadClass::Tag(),
                           thread_instance);
    }

    /**
     * @brief      创建一个全局Timer
     *
     * @param[in]  timer_type  Timer类型
     *                         TimerType::kPeriod  周期触发
     *                         TimerType::kOneShot 单次触发
     * @param[in]  timer_job   Timer任务，void (TimerHandler&)
     * @param[in]  duration    Timer的相对当前时间的相对到期时间，目前支持的最小精度为毫秒
     *                         adk::thread::Milliseconds(1000)
     * 
     * @note   当duration非0时，创建并启动Timer。否则创建Timer
     *
     * @return     成功时返回Timer句柄，失败时返回 adk::kInvalidTimerHandler
     */
    TimerHandler CreateTimer(TimerType timer_type,
                             const TimerJobType& timer_job,
                             TimeValue   duration = 0);

    /**
     * @brief      修改Timer到期时间
     *
     * @param      hdl       Timer句柄
     * @param[in]  duration  新的相对到期时间
     * @param[in]  offset    参考偏移
     *                       TimerOffset::kNow，相对于当前时间
     *                       TimerOffset::kLast，相对于最近的到期时间
     *
     * @note   若在CreateTimer时未指定duration参数，可以掉用该接口启动Timer
     *         注意将Timer到期时间向前修改，可能导致失败
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    int32_t ModifyTimer(TimerHandler& hdl,
                        TimeValue     duration,
                        TimerOffset   offset = TimerOffset::kNow,
                        bool          fail_on_timeout = false);

    int32_t DelayTimer(TimerHandler& hdl,
                       TimeValue     duration,
                       TimerOffset   offset = TimerOffset::kNow)
    {
        return ModifyTimer(hdl, duration, offset, true);
    }

    /**
     * @brief      取消Timer
     *
     * @param      hdl   Timer句柄
     *
     * @return     成功取消timer时返回 ErroCode::kSuccess
     */
    int32_t CancelTimer(TimerHandler& hdl);

    /**
     * @brief      同步停止Timer
     *
     * @param      hdl   Timer句柄
     * 
     * @note     同步停止Timer运行，该接口返回时Timer不再活动。但应用需保证没有第三方线程在该接口
     *           返回后再次启动Timer(ModifyTimer)。在Timer任务内调用ModifyTimer则不受影响
     *           该接口也会发生阻塞，应用需注意所使用的线程上下文
     */
    void SyncCancelTimer(TimerHandler& hdl);

    /**
     * @brief      删除Timer
     *
     * @param      hdl   Timer句柄
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    int32_t DeleteTimer(TimerHandler& hdl);

    /**
     * @brief      启动TimerManger
     * 
     * @note       启动Timer前，需要先启动TimerMnager
     */
    void Start();

    /**
     * @brief      终止TimerManager
     * 
     * @note       该接口返回后，不会有任何Timer运行，也无法再启动Timer，该接口会阻塞
     */
    void Finish();

protected:
    TimerHandler CreateTimer(TimerType timer_type,
                             const TimerJobType& timer_job,
                             TimeValue   duration,
                             ThreadManager* thread_mana,
                             uint32_t thread_tag,
                             uint32_t thread_instance);

private:
    // =============================================================
    pthread_spinlock_t  timer_mana_lock_;
    pthread_spinlock_t  join_lock_;
    volatile bool       is_running_;
    uint32_t            nr_expire_timers_;
    TimerIdType         last_timer_index_;
    TimerHandler        expire_timers_[ADK_THREAD_MAX_TIMERS];
    ThreadTimer         timers_[ADK_THREAD_MAX_TIMERS];
    std::map<TimeValue, TimerHandler> running_timers_;
    boost::thread       timer_thread_;

    void TimerThreadMain();

    void RunTimerCycle();

    TimerIdType AllocTimerIndex();

    int32_t FreeTimerIndex(TimerIdType index);

    void SchedTimerToRun(TimeValue expire_time, TimerHandler& hdl);

    int32_t ReSchedTimer(ThreadTimer& timer, TimerHandler& hdl);

    void RunTimer(TimerHandler& hdl);

    friend class TimerManager;
    friend class TimerSignal;
};

ADK_OOB_THREAD_MESSAGE(TimerSignal)
{
public:
    TimerHandler        timer_hdl;
    ThreadTimerManager* timer_mana;
    void Run();
};

/**
 * @brief      将ThreadTimerManager的方法静态化，方便使用，性能可能稍有下降
 */
class TimerManager
{
public:
    template<typename ThreadClass>
    static TimerHandler CreateTimer(
                             TimerType              timer_type,
                             const TimerJobType&    timer_job,
                             TimeValue              duration = 0,
                             uint32_t               thread_instance = 0,
                             const std::string&     ns = "")
    {
        Instance()->Start();
        return Instance()->CreateTimer(
                           timer_type,
                           timer_job,
                           duration,
                           ThreadManager::Instance(ns),
                           ThreadClass::Tag(),
                           thread_instance);
    }

    static TimerHandler CreateTimer(TimerType timer_type,
                                    const TimerJobType& timer_job,
                                    TimeValue   duration = 0)
    {
        Instance()->Start();
        return Instance()->CreateTimer(timer_type, timer_job, duration);
    }

    static int32_t ModifyTimer(TimerHandler& hdl,
                        TimeValue     duration,
                        TimerOffset   offset = TimerOffset::kNow)
    {
        return Instance()->ModifyTimer(hdl, duration, offset);
    }

    static int32_t DelayTimer(TimerHandler& hdl,
                              TimeValue     duration,
                              TimerOffset   offset = TimerOffset::kNow)
    {
        return Instance()->DelayTimer(hdl, duration, offset);
    }

    static int32_t CancelTimer(TimerHandler& hdl)
    {
        return Instance()->CancelTimer(hdl);
    }

    static void SyncCancelTimer(TimerHandler& hdl)
    {
        return Instance()->SyncCancelTimer(hdl);
    }

    static int32_t DeleteTimer(TimerHandler& hdl)
    {
        return Instance()->DeleteTimer(hdl);   
    }

    static void Start()
    {
        Instance()->Start();
    }

    static void Finish()
    {
        Instance()->Finish();
    }
    
    static ThreadTimerManager* Instance() 
    { 
        static ThreadTimerManager s_thr_timer_mana;
        return &s_thr_timer_mana;
    }
};

template<typename ThreadClass>
int32_t ThreadManager::ChangeParams(
                            BOOST_PP_ENUM_PARAMS(
                                ADK_THREAD_PROPERTY_NUM,
                                const GenericArg& arg)
                            )
{
    if (threads_params_[ThreadClass::Tag()] == NULL)
        threads_params_[ThreadClass::Tag()] = new ThreadParams();

    #define adk_forward_arg(z, n, unused) , arg##n
    return ChangeParams(threads_params_[ThreadClass::Tag()]
                              BOOST_PP_REPEAT(ADK_THREAD_PROPERTY_NUM, adk_forward_arg, ~));
    #undef adk_forward_arg
}

template<bool isBlock, typename AppThreadMessage>
int32_t ThreadBase::SendMsg(AppThreadMessage* msg)
{
    msg->hold_reference();
   if(isBlock)
    {
        Accept(msg);
        return ErrorCode::kSuccess;
    }
    else
    {
        auto result = AcceptNoBlock(msg);
        if(result != kSuccess)
            msg->drop_reference();
        return result;
    }
}

template<bool isBlock, typename ThreadClass, typename AppThreadMessage>
int32_t ThreadManager::SendMsg(AppThreadMessage* msg, int32_t instance_id)
{
    ThreadBase* app_thread = ThreadInstance(ThreadClass::Tag(), instance_id);;
    if (app_thread == NULL)
        return ErrorCode::kOutOfRange;

    msg->hold_reference();
   if(isBlock)
    {
        app_thread->Accept(msg);
        return ErrorCode::kSuccess;
    }
    else
    {
        auto result = app_thread->AcceptNoBlock(msg);
        if(result != kSuccess)
            msg->drop_reference();
        return result;
    }
}

template<bool isBlock, typename ThreadClass, typename AppThreadMessage>
int32_t ThreadManager::SendMsgUnsafe(AppThreadMessage* msg, int32_t instance_id)
{
    // ThreadBase* app_thread = threads_[ThreadClass::Tag()][instance_id];
    ThreadBase* app_thread = threads_[ThreadClass::Tag()* ADK_MAX_THREAD_INSTANCE + instance_id];
    msg->hold_reference();
    if(isBlock)
    {
        app_thread->Accept(msg);
        return ErrorCode::kSuccess;
    }
    else
    {
        auto result = app_thread->AcceptNoBlock(msg);
        if(result != kSuccess)
            msg->drop_reference();
        return result;
    }
}

template<typename ThreadClass>
int32_t ThreadManager::ReleaseMessageProcess(uint64_t tag, int32_t instance_id)
{
    ThreadBase* app_thread = ThreadInstance(ThreadClass::Tag(), instance_id);
    if (app_thread == NULL)
        return ErrorCode::kOutOfRange;
    app_thread->AsyncReleaseMessageProcess(tag);
    return ErrorCode::kSuccess;
}

template<typename ThreadClass>
inline int32_t ReleaseMessageProcess(uint64_t tag, int32_t instance_id = 0)
{
    return ThreadManager::Instance()->ReleaseMessageProcess<ThreadClass>(tag, instance_id);
}

/**
 * @brief      发送线程消息
 *
 * @param[in]  msg               线程消息
 * @param[in]  instance_id       线程ID
 *
 * @tparam     ThreadClass       线程类
 * @tparam     isBlock           isBlock send progress, true by default.
 *
 * @note       该接口线程安全
 *
 * @return     发送成功时返回 ErrorCode::kSuccess
 */
template<typename ThreadClass, bool isBlock = true, typename AppThreadMessage>
inline int32_t SendMsg(const ThreadMessage<AppThreadMessage>& msg, int32_t instance_id = 0)
{
    return ThreadManager::Instance()->SendMsg<ThreadClass, isBlock>(msg, instance_id);
}

/**
 * @brief      发送线程消息
 *
 * @param[in]  msg               线程消息
 * @param[in]  instance_id       线程ID
 *
 * @tparam     ThreadClass       线程类
 * @tparam     isBlock           isBlock send progress, true by default.
 *
 * @note       该接口线程安全，但该接口不检查线程是否存在，应用需保证线程已注册
 *
 * @return     发送成功时返回 ErrorCode::kSuccess
 */
template<typename ThreadClass, bool isBlock = true, typename AppThreadMessage>
inline int32_t SendMsgUnsafe(const ThreadMessage<AppThreadMessage>& msg, int32_t instance_id = 0)
{
    return ThreadManager::Instance()->SendMsgUnsafe<ThreadClass, isBlock>(msg, instance_id);
}

/**
 * @brief      定义线程类
 *
 * @param      ThreadClass  线程类名
 * @param      Desc         线程类描述
 */
#define ADK_DEFINE_THREAD(ThreadClass, Desc)     \
    class ThreadClass##Fake {}; \
    template<typename FakeType = int>   \
    class ThreadClass##Basic : public adk::ThreadBase  \
    {   \
    public: \
        ThreadClass##Basic()   \
        {   \
            s_thread_tag =     \
                    adk::GenericThread::AllocThreadTag<ThreadClass##Fake>(); \
            thread_tag_ = s_thread_tag; \
            thread_name_ = #ThreadClass;  \
            thread_desc_ = Desc;    \
            message_handlers_ = adk::GenericThread::AllocBuffer<ThreadClass##Fake>(); \
            RegisterMessageHandler<adk::AsyncSignal>(   \
                            (adk::GenericThread::HandlerType)(&adk::ThreadBase::SignalHandler)); \
            RegisterMessageHandler<adk::TimerSignal>(   \
                            (adk::GenericThread::HandlerType)(&adk::ThreadBase::TimerHandler)); \
        }   \
        \
        static int32_t Tag()    \
        {   \
            return s_thread_tag;    \
        }   \
        \
        template<typename MessageType>  \
        static void RegisterMessageHandler(const adk::GenericThread::HandlerType hdl)    \
        {   \
            s_message_handlers = adk::GenericThread::AllocBuffer<ThreadClass##Fake>();  \
            assert(MessageType::TypeId() < ADK_MAX_THREAD_MESSAGES); \
            assert(MessageType::TypeId() > -(ADK_MAX_THREAD_OOB_MESSAGES));  \
            s_message_handlers[MessageType::TypeId()] = hdl;    \
        }   \
    private:    \
        static int32_t s_thread_tag;    \
        static adk::GenericThread::HandlerType* s_message_handlers;  \
    };  \
    template<typename FakeType> \
    int32_t ThreadClass##Basic<FakeType>::s_thread_tag;  \
    template<typename FakeType> \
    adk::GenericThread::HandlerType* ThreadClass##Basic<FakeType>::s_message_handlers;  \
    static adk::OnceObject ADK_CONCATENATE(uniq_##ThreadClass, __LINE__)(   \
            [](){ ThreadClass##Basic<> unused; }  \
        ); \
    class ThreadClass : public ThreadClass##Basic<>

// #define ADK_DEFINE_MESSAGE_HANDLER(ClassName, MessageType)    
    // void OnMessage(MessageType*); 
    // class ADK_CONCATENATE(Uniq, __LINE__) {   
    // public: 
        // ADK_CONCATENATE(Uniq, __LINE__)() 
        // {   
            // void (ClassName::*mptr)(MessageType*) = &ClassName::OnMessage; 
            // ClassName##Basic<>::RegisterMessageHandler<MessageType>(  
                                    // (adk::GenericThread::HandlerType)(mptr)); 
        // }   
    // } ADK_CONCATENATE(Uniq, __LINE__);

#define NTRIM(a,b) void a(b*)
#define BTRIM(a) NTRIM a

#define ADK_DEFINE_MESSAGE_HANDLER_1(a) BTRIM(a);
#define ADK_DEFINE_MESSAGE_HANDLER_2(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_1(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_3(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_2(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_4(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_3(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_5(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_4(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_6(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_5(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_7(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_6(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_8(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_7(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_9(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_8(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_10(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_9(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_11(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_10(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_12(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_11(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_13(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_12(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_14(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_13(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_15(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_14(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_16(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_15(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_17(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_16(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_18(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_17(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_19(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_18(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_20(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_19(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_21(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_20(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_22(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_21(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_23(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_22(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_24(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_23(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_25(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_24(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_26(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_25(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_27(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_26(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_28(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_27(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_29(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_28(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_30(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_29(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_31(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_30(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_32(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_31(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_33(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_32(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_34(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_33(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_35(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_34(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_36(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_35(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_37(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_36(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_38(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_37(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_39(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_38(__VA_ARGS__)
#define ADK_DEFINE_MESSAGE_HANDLER_40(a, ...) BTRIM(a) ; ADK_DEFINE_MESSAGE_HANDLER_39(__VA_ARGS__)

#define ADK_DEFINE_MESSAGE_HANDLER_(N, ...) ADK_CONCATENATE(ADK_DEFINE_MESSAGE_HANDLER_, N)(__VA_ARGS__)

/**
 * @brief      定义线程消息处理句柄
 * 
 * ADK_DEFINE_MESSAGE_HANDLER(
 *      (HandlerName,       ThreadMessageType*),
 *      (OnMessage,         Task*),
 *      (OnMessage,         Foo*),
 *      (OnOrderMessage,    Order*)
 * )
 */
#define ADK_DEFINE_MESSAGE_HANDLER(...) ADK_DEFINE_MESSAGE_HANDLER_(ADK_GET_NARG(__VA_ARGS__), __VA_ARGS__)    \
                                        public: \
                                        virtual void InternalInit()     \
                                        {   \
                                            typedef decltype(this) this_type;   \
                                            this_type a = this; \
                                            typedef decltype(adk::thread::GetClassType(a)) biz_gateway_class_type;   \
                                            THREAD_REGISTER_HANDLER(__VA_ARGS__);   \
                                        }\

#define THREAD_DO_REGISTER_HANDLER(a, b)    \
            {   \
                void (biz_gateway_class_type::*mptr)(b*) = &biz_gateway_class_type::a;  \
                RegisterMessageHandler<b>((adk::GenericThread::HandlerType)(mptr));  \
            }

#define THREAD_REGISTER_HANDLER_1(a) THREAD_DO_REGISTER_HANDLER a;
#define THREAD_REGISTER_HANDLER_2(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_1(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_3(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_2(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_4(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_3(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_5(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_4(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_6(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_5(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_7(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_6(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_8(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_7(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_9(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_8(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_10(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_9(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_11(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_10(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_12(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_11(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_13(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_12(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_14(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_13(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_15(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_14(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_16(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_15(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_17(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_16(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_18(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_17(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_19(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_18(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_20(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_19(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_21(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_20(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_22(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_21(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_23(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_22(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_24(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_23(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_25(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_24(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_26(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_25(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_27(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_26(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_28(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_27(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_29(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_28(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_30(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_29(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_31(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_30(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_32(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_31(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_33(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_32(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_34(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_33(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_35(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_34(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_36(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_35(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_37(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_36(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_38(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_37(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_39(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_38(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER_40(a, ...) THREAD_DO_REGISTER_HANDLER a ; THREAD_REGISTER_HANDLER_39(__VA_ARGS__)

#define THREAD_REGISTER_HANDLER_(N, ...) ADK_CONCATENATE(THREAD_REGISTER_HANDLER_, N)(__VA_ARGS__)
#define THREAD_REGISTER_HANDLER(...) THREAD_REGISTER_HANDLER_(ADK_GET_NARG(__VA_ARGS__), __VA_ARGS__);

struct CreatorInfo
{
    CreatorInfo(const ThreadCreatorType& arg1,
                const std::string& arg2)
        :   creator(arg1),
            creator_name(arg2)
    {}

    ThreadCreatorType creator;
    std::string creator_name;
};

class RegisterHelper;
class _RegisterHelper
{
public: 
    _RegisterHelper(RegisterHelper* reg_base)
        :   reg_base_(reg_base)
    {}
    _RegisterHelper& operator()(
               const CreatorInfo& creator_info,
               BOOST_PP_ENUM_PARAMS_WITH_A_DEFAULT(
                    ADK_THREAD_PROPERTY_NUM,
                    const GenericArg& arg,
                    adk::thread::place_holder)
               );
    
    void set_is_reg(bool value) { is_reg_ = value; }

private:
    bool is_reg_ = false;
    RegisterHelper* reg_base_;
};

class RegisterHelper
{
public:
    RegisterHelper()
        :   ns_("Default")
    {}

    _RegisterHelper& operator()(const std::string& arg_ns)
    {
        ns_ = arg_ns;
        static std::map<std::string, _RegisterHelper*> reg_map;
        auto it = reg_map.find(ns_);
        if (it != reg_map.end())
        {
            it->second->set_is_reg(true);
            return *(it->second);
        }

        auto& reg_helper = reg_map[ns_];
        reg_helper = new _RegisterHelper(this);
        return *reg_helper;
    }

    std::string& ns() { return ns_; }

private:
    std::string ns_;
};

#define ASSING_PARAM(obj, name, value) \
        obj->name = value;   \
        obj->is_default_##name = false;  \
        obj->is_default = false;

// "AddParameters"
inline void ArgAssigment(ThreadParams* params, const GenericArg& arg)
{
    #define AssignLT0(field) {   \
        int value = arg.any_cast<int>();    \
        if (value > 0)  \
        {   \
            ASSING_PARAM(params, field, value); \
        }   \
        return; \
    }

    #define GenericAssign(field, type) {   \
        type value = arg.any_cast<type>();    \
        ASSING_PARAM(params, field, value); \
        return; \
    }
    
    if (arg == "InstanceNumber")
        AssignLT0(number_instance)
    else if (arg == "EventMode")
    {
        int32_t value = arg.any_cast<int32_t>();
        if (value != thread::kInterrupt && value != thread::kPolling)
            return;
        ASSING_PARAM(params, event_mode, value);
    }
    else if (arg == "InitPriority")
        AssignLT0(init_priority)
    else if (arg == "MessageBudget")
        AssignLT0(msg_budget)
    else if (arg == "MessageBudgetOOB")
        AssignLT0(oob_msg_budget)
    // MqCntProcessLimit为错误拼写。为兼容过去版本所以保留
    else if ((arg == "MqCntProcessLimit") || (arg == "MqContProcessLimit"))
        AssignLT0(mq_cont_process_limit)
    else if (arg == "BackoffLimit")
        AssignLT0(backoff_limit)
    else if (arg == "BusyPollNano")
        AssignLT0(busy_poll_ns)
    else if (arg == "WaitTimeoutNano")
        AssignLT0(wait_timeout_ns)
    else if (arg == "ParallelInit")
        GenericAssign(parallel_init, bool)
    else if (arg == "ThreadAffinity")
        GenericAssign(thread_affinity, std::string)

    #undef AssignLT0
    #undef GenericAssign
}

inline _RegisterHelper& register_of(const std::string& ns)
{
    static RegisterHelper reg_helper;
    return reg_helper(ns);
}

/**
 * @brief      声明开始注册线程
 * @param  ns  线程类的命名空间
 */
#define ADK_REGISTER_NS_THREAD_BEGIN(ns)   \
            static adk::_RegisterHelper ADK_CONCATENATE(ns, __LINE__) = adk::register_of(#ns)

/**
 * @brief      声明开始注册线程
 */
#define ADK_REGISTER_THREAD_BEGIN()   \
            static adk::_RegisterHelper ADK_CONCATENATE(Default, __LINE__) = adk::register_of("Default")

/**
 * @brief      注册线程类
 */
// a little bit memory leak here!
#define ADK_THREAD_CLASS(AppThread) \
            adk::CreatorInfo([](adk::ThreadManager* mana, adk::ThreadParams* params,    \
                                int32_t id, adk::ThreadParams** out_params)->adk::ThreadBase* { \
                                    if (out_params != NULL) \
                                    {   \
                                        *out_params = mana->GetParms<AppThread>();    \
                                        return NULL; \
                                    }   \
                                    \
                                    mana->GetParms<AppThread>() = params; \
                                    return adk::ThreadBase::Create<AppThread>(mana, params, id);    \
                        },   \
                        #AppThread)

/**
 * @brief      声明结束注册线程
 */
#define ADK_REGISTER_THREAD_END()   ;

} // adk

#endif // ADK_THREAD_H_
