#ifndef ADK_IMPL_IO_ENGINE_CTRL_WORKER_H_
#define ADK_IMPL_IO_ENGINE_CTRL_WORKER_H_

#include "endpoint_impl.h"

#include <set>
#include <mutex>
#include <thread>

#include <adk/io_engine/event.h>
#include <adk/io_engine/endpoint.h>
#include <adk/lock_free_queue_variant.h>

namespace adk_impl
{

class Property;

namespace io_engine
{

using variant::SPSCQueue;
using variant::MPSCQueue;
using variant::VariantEntry;
using variant::ThreadLocalQueue;

struct AsyncEventInfo
{
    EndpointHeader* endpoint_header;
    Event*          event;
    bool            rx_release_action;
};

struct HeartbeatNode
{
    EndpointHeader* endpoint_header;
    uint64_t        last_rx_bytes;
    uint64_t        last_check_timepoint;
};

class Endpoint;
class DriveEngine;
class TcpEndpoint;

constexpr uint64_t kDeleteCheckIntervalus = 1000;
constexpr uint32_t kEstablishSelectTimeoutus = 1000000;
constexpr uint32_t kEndpointHeaderDestroyDelayMicro = 1000000;

enum TaskType : uint32_t
{
    kImmConnect = 0,
    kAsynConnect,
    kAsynReconnect,
    kAsynAccept
};

struct TaskBase
{
    uint32_t      task_type;
};

struct ImmediateTask : public TaskBase
{
    TcpEndpoint*  endpoint_impl;
};

struct ConnectTask : public TaskBase
{
    EndpointHeader* endpoint_header;
    std::string     remote_ip;
    uint16_t        remote_port;
    uint32_t        limit_times;
    int64_t         interval_us;
    int64_t         retry_times;

    void retry_forward()
    {
        assert(interval_us > 0);
        interval_us = -interval_us;
    }

    bool is_retry_forward()
    {
        if (interval_us < 0)
        {
            interval_us = -interval_us;
            return true;
        }

        return false;
    }

    void complete()
    {
        retry_times += limit_times;
    }

    bool is_complete() const
    {
        return (retry_times >= limit_times);
    }
};

struct AcceptTask : public TaskBase
{
    TcpAcceptor* acceptor_impl;
};

class ControlActor
{
public:
    ControlActor(DriveEngine* const drive_egine);
    ~ControlActor() = default;

    int32_t Init(const Property& props);

    void Start();

    void Stop();

    void Exit();

    int32_t ToAccept(AcceptTask* const accept_task);

    int32_t ToConnect(ConnectTask* const connect_task);

    int32_t ToDelete(TcpEndpoint* const endpoint_impl);

    int32_t ToDelete(EndpointHeader* const endpoint_header);

    int32_t ToCallbackImmediate(TcpEndpoint* const endpoint, uint32_t task_type);

    void AcceptorIndicator(boost::property_tree::ptree& acceptor_ptree);

    void ConnectIndicator(boost::property_tree::ptree& connect_ptree);

    template<bool kRxDirect>
    inline int32_t ToDeliverEvent(EndpointHeader* endpoint_header, Event* event)
    {
        assert(async_event_infos_);

        VariantEntry* entry_ptr;
        const auto ec = async_event_infos_->TryAllocEntry(&entry_ptr);
        if (ADK_UNLIKELY(ErrorCode::kSuccess != ec))
        {
            return ec;
        }

        if (event->level() > EventLevel::kWarn)
        {
            if (kRxDirect)
            {
                endpoint_header->tx_valid = false;
            }
            else
            {
                endpoint_header->rx_valid = false;
            }
        }
        
        char* const buffer = entry_ptr->buffer;
        ((AsyncEventInfo*)buffer)->endpoint_header = endpoint_header;
        ((AsyncEventInfo*)buffer)->event = event;
        ((AsyncEventInfo*)buffer)->rx_release_action = kRxDirect;
        async_event_infos_->PostEntry(entry_ptr);

        return ErrorCode::kSuccess;
    }

    std::thread::id this_actor_id() const
    {
        return thread_hdl_.get_id();
    }

    inline void set_pause()
    {
        is_paused_ = true;
    }

    inline void set_resume()
    {
        is_paused_ = false;
    }

private:

    void ActorThread();

    // discard
    void WorkerThread();

    void OnAccept(TcpAcceptor* const acceptor_impl, TcpEndpoint* const endpoint_impl);

    void OnConnect(EndpointHeader* const endpoint_header);

    void PerformTask(ImmediateTask* const imm_task)
    {
        switch (imm_task->task_type)
        {
        case kImmConnect:
        {
            TcpEndpoint* const endpoint_impl = imm_task->endpoint_impl;
            endpoint_impl->OnConnect();
            endpoint_impl->sub_reference();
        }
        break;
        }
    }

    void ToMonitorEndpoint(EndpointHeader* const endpoint_header)
    {
        assert(endpoint_header);

        HeartbeatNode heartbeat_node;
        heartbeat_node.endpoint_header = endpoint_header;
        heartbeat_node.last_rx_bytes = 0;

        int32_t result;
        do
        {
            heartbeat_node.last_check_timepoint = endpoint_header->GetTimepoint();
            result = heartbeat_monitor_->Push(heartbeat_node);
        } while (ADK_UNLIKELY(ErrorCode::kSuccess != result));
    }

    void DoEpsDelete();

    void DoDeliverEvent(std::set<EndpointHeader*>& droped_eps_set);

    void DoEpsDestroy();

    void DoForceEpsDestroy();

    void DiagnoseHeartbeat(const std::set<EndpointHeader*>& droped_eps_set);

    inline void DropEndpoint(EndpointHeader* const endpoint_header)
    {
        endpoint_header->set_phy_release();
        endpoint_header->set_rx_release();
        endpoint_header->set_tx_release();
    }

    inline void DropCtrlEndpoint(EndpointHeader* const endpoint_header)
    {
        endpoint_header->set_phy_release();
    }

    bool         is_running_;
    bool         is_paused_;
    std::string  name_;
    std::thread  thread_hdl_;
    
    DriveEngine* drive_engine_;
    MPSCQueue<AcceptTask*>*   accept_tasks_;
    MPSCQueue<ConnectTask*>*  connect_tasks_;

    MPSCQueue<TcpEndpoint*>*  to_delete_eps_;
    MPSCQueue<ImmediateTask>* immediate_task_;

    SPSCQueue<EndpointHeader*>* to_destroy_eps_;
    MPSCQueue<HeartbeatNode>*   heartbeat_monitor_;
    MPSCQueue<AsyncEventInfo>*  async_event_infos_;

    ///> indicator
    uint64_t  accepted_eps_nr_;

    uint64_t  connect_cancel_nr_;
    uint64_t  connect_failed_nr_;
    uint64_t  connected_eps_nr_;
};

}

}

#endif
