#ifndef ADK_IMPL_IO_ENGINE_IO_WORKER_H_
#define ADK_IMPL_IO_ENGINE_IO_WORKER_H_

#include "event_impl.h"
#include "message_impl.h"
#include "endpoint_impl.h"

#include <mutex>
#include <deque>
#include <thread>

#include <adk/constant.h>
#include <adk/error_code.h>
#include <adk/arch/generic.h>
#include <adk/arch/synchronize.h>
#include <adk/io_engine/endpoint.h>
#include <adk/lock_free_queue_variant.h>

namespace adk_impl
{

class Property;

namespace io_engine
{

class Endpoint;
class DriveEngine;

using std::mutex;
using std::deque;
using variant::ThreadLocalQueue;
using TxActiveEpQue = variant::MPSCQueue<EndpointHeader*>;
using RxActiveEpQue = variant::SPSCQueue<EndpointHeader*>;

constexpr uint32_t kBackOffLoop = 32;
constexpr uint32_t kBackOffMask = kBackOffLoop - 1;

constexpr uint32_t kTxDecayAcitve0Loop = 32;
constexpr uint32_t kRxDecayAcitve0Loop = 32;
constexpr uint32_t kTxDecayAcitve1Loop = 64 + kTxDecayAcitve0Loop;
constexpr uint32_t kRxDecayAcitve1Loop = 64 + kRxDecayAcitve0Loop;

constexpr uint32_t kRxActiveLoadWBits = 4;
constexpr uint32_t kTxActiveLoadWBits = 4;
constexpr uint32_t kRxActive0LoadWBits = 2;
constexpr uint32_t kTxActive0LoadWBits = 2;

constexpr uint32_t kActorRxActive0Eps = 2;
constexpr uint32_t kActorTxActive0Eps = 4;

constexpr uint32_t kDuplexRxActive0Eps = 2;
constexpr uint32_t kDuplexTxActive0Eps = 2;

constexpr uint32_t kTxBatchSize = 128;

constexpr uint32_t kCpuRelaxLoopCounter = 64;
constexpr uint32_t kMaxIdleLoopCounter = 1024;

///> local storage (1 + 7) * 8
constexpr uint32_t kRxActiveQSize = 7;
constexpr uint32_t kTxActiveQSize = 7;

///> local storage (1 + 7) * 8
constexpr uint32_t kDuplexRxActiveQSize = 7;
constexpr uint32_t kDuplexTxActiveQSize = 7;

struct RecvActorArena
{
    RecvActorArena(RxMessagePool* message_pool);

    bool            delivering;
    bool            is_running;
    uint32_t        length;
    char*           orig_app_data;
    uint64_t        actor_counter;
    uint64_t        actor_rcu_counter;
    RxMessagePool*  rx_message_pool;

    void push_message(MessageImpl* message_impl)
    {
        length = message_impl->capacity();
        orig_app_data = message_impl->data();
    }

    void pop_message(MessageImpl* message_impl)
    {
        message_impl->set_capacity(length);
        message_impl->set_app_data(orig_app_data);
    }

    void reset()
    {
        length = 0;
        orig_app_data = nullptr;
    }
};

class IOActor
{
public:
    IOActor(DriveEngine* const drive_engine);
    virtual ~IOActor() = default;
    
    int32_t Init(const Property& props);

    void Start();

    void Stop();

    void Exit();

    template<bool kIsLowLatency>
    inline void OnActorIdle(uint32_t& idle_counter)
    {
        if (!kIsLowLatency)
        {
            struct timespec timeout = { 0, 1000000 };

            futex_event_ = 1;
            FutexWaitPrivate(&futex_event_, 1, &timeout);
            futex_event_ = 0;
            return;
        }

        for (uint32_t index = 0; index < kCpuRelaxLoopCounter; ++index)
        {
            ADK_PAUSE();
        }
    }

    static inline RecvActorArena* current_actor_arena()
    {
        return s_actor_arena_;
    }

    static inline void message_deliver_begin(EndpointHeader* const endpoint_header)
    {
        assert(s_actor_arena_);
        while (ADK_UNLIKELY(!ACCESS_ONCE(s_actor_arena_->is_running)))
        {
            ++s_actor_arena_->actor_counter;
            usleep(0);
        }

        s_actor_arena_->delivering = true;
    }

    static inline void message_deliver_end(EndpointHeader* const endpoint_header)
    {
        assert(s_actor_arena_);
        s_actor_arena_->delivering = false;
    }

    /*
    uint32_t this_actor_load() const
    {
        return ACCESS_ONCE(actor_load_);
    }*/

protected:

    template<typename ActiveEpQue>
    void AddJob(ActiveEpQue* active_endpoints, EndpointHeader* endpoint_header)
    {
        ADK_ASSERT_SUCCESS(active_endpoints->Push(endpoint_header));
        if (ADK_UNLIKELY(futex_event_))
        {
            FutexWakePrivate(&futex_event_);
        }
    }

    static thread_local RecvActorArena* s_actor_arena_;

    bool           is_running_;
    ///record the real-time load of the actor
    //uint64_t     loop_cost_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    //uint32_t       actor_load_;
    std::thread    thread_hdl_;
    DriveEngine*   drive_engine_;
    std::string    actor_cpu_list_;
    std::string    name_;

    int32_t        futex_event_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
};

class SendActor : public IOActor
{
public:
    SendActor(DriveEngine* const drive_engine);
    virtual ~SendActor() = default;

    int32_t Init(const Property& props);

    void Start(bool multithread);

    void Stop();

    void Exit();

    void AddJob(EndpointHeader* endpoint_header)
    {
        IOActor::AddJob(active_endpoints_, endpoint_header);
    }

protected:
    template<bool kInMultiThread>
    void Start();

    template<bool kIsLowLatency, bool kPreSendEnable, typename EndpointType, bool kInMultiThread>
    void ActorThread();

    TxActiveEpQue* active_endpoints_;
};

class RecvActor : public IOActor
{
public:
    RecvActor(DriveEngine* const drive_engine);
    virtual ~RecvActor() = default;

    int32_t Init(const Property& props);

    void Start();

    void Stop();

    void Exit();

    void AddJob(EndpointHeader* endpoint_header)
    {
        endpoint_header->set_rx_active();
        IOActor::AddJob(active_endpoints_, endpoint_header);
    }

private:

    template<bool kIsLowLatency, bool kPreRecvEnable, typename EndpointType>
    void ActorThread();

    RxMessagePool  rx_message_pool_;
    RxActiveEpQue* active_endpoints_;
};

class DuplexActor : public SendActor
{
public:
    DuplexActor(DriveEngine* const drive_engine);
    virtual ~DuplexActor() = default;

    int32_t Init(const Property& props);

    void Start(bool multithread);

    void Stop();

    void Exit();

    inline void AddJob(EndpointHeader* endpoint_header)
    {
        endpoint_header->set_rx_active();
        IOActor::AddJob(active_rxendpoints_, endpoint_header);
    }

private:
    template<bool kInMultiThread>
    void Start();

    template<bool kIsLowLatency, bool kPreSendEnable, bool kPreRecvEnable, typename EndpointType, bool kInMultiThread>
    void ActorThread();

    RxMessagePool  rx_message_pool_;
    RxActiveEpQue* active_rxendpoints_;
};

}

}

#endif
