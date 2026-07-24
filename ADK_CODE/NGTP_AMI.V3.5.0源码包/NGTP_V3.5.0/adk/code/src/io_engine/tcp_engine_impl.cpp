#include "last_error.h"
#include "drive_engine.h"
#include "acceptor_impl.h"
#include "endpoint_impl.h"
#include "tcp_engine_impl.h"
#include "tcp_verbs/tcp_socket.h"
#if defined(__x86_64__)
#include "tcp_verbs/tcp_direct_zf.h"
#elif defined(__aarch64__)
#include "tcp_verbs/tcp_direct_zf_arm.h"
#endif

#include <time.h>
#include <stdlib.h>

#include <boost/format.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <adk/arch/generic.h>
#include <adk/io_engine/config_key.h>

#ifndef ENV_TX_LOW_LATENCY
#define ENV_TX_LOW_LATENCY  "TCP_TX_LOW_LATENCY"
#endif

#ifndef ENV_RX_LOW_LATENCY
#define ENV_RX_LOW_LATENCY  "TCP_RX_LOW_LATENCY"
#endif

namespace adk_impl
{

namespace verbs
{

class TcpEndpointBlank final : public ITcpEndpoint
{
public:
    bool SetOption(OptionType option_type, int32_t option_value) override
    {
        return false;
    }

    int32_t Bind(uint16_t local_port) override
    {
        return -1;
    }

    int32_t Connect(const std::string& remote_ip, uint16_t remote_port) override
    {
        errno = EBADF;
        return -1;
    }

    bool EPollResult(uint32_t events) override
    {
        return false;
    }

    ssize_t Send(const void* buffer, size_t len) override
    {
        errno = EBADF;
        return -1;
    }

    ssize_t Send(const struct iovec *iov, size_t iovcnt) override
    {
        errno = EBADF;
        return -1;
    }

    ssize_t Recv(char* buffer, size_t len) override
    {
        errno = EBADF;
        return -1;
    }

    ssize_t Recv(const struct iovec *iov, size_t iovcnt) override
    {
        errno = EBADF;
        return -1;
    }

    std::string LastError() const override
    {
        return std::string("Endpoint is closed");
    }

protected:
    bool Open(ITcpStack* tcp_stack, bool reuse_addr, bool reuse_port) override
    {
        return false;
    }

    void Close() override
    {
    }
};

static TcpEndpointBlank s_tcp_endpoint_blank;

}

namespace io_engine
{

using boost::format;

TcpEngineImpl::TcpEngineImpl()
{
    tcp_stack_ = nullptr;
    drive_engine_ = nullptr;
    endpoint_headers_ = nullptr;
    recycle_eps_queue_ = nullptr;
    endpoint_header_queue_ = nullptr;

    tx_low_latency_ = 0xFFu;
    rx_low_latency_ = 0xFFu;
    rx_memory_block_size_ = default_value::kRxMemoryBlockSize;

    local_ports_register_ = nullptr;
    blank_endpoint_header_ = nullptr;
}

int32_t TcpEngineImpl::Init(const Property& engine_props)
{
    Exit();

    srandom(time(nullptr));

    const auto message_ip = engine_props.GetValue(config::kMessageIp, std::string());

    tcp_stack_ = verbs::ITcpStack::Create(message_ip);
    if (ADK_UNLIKELY(nullptr == tcp_stack_))
    {
        // current version can not be failed
        return ErrorCode::kFailure;
    }

    if (ADK_UNLIKELY(ErrorCode::kSuccess != IoEngineBase::Init(engine_props)))
    {
        return ErrorCode::kFailure;
    }

    const uint32_t max_connection = engine_props.GetValue(config::kMaxConnections, 
                                                          default_value::kMaxConnections);

    recycle_eps_queue_ = RecycleEpsQueue::Create("recycle endpoints queue", max_connection);
    assert(recycle_eps_queue_);

    endpoint_header_queue_ = EndpointHeaderQueue::Create("endpoint header queue", max_connection);
    assert(endpoint_header_queue_);

    const uint32_t payload_size = ADK_ROUND_UP(sizeof(struct EndpointHeader), 
                                               ADK_CACHE_LINE_SIZE);
    const uint32_t queue_capacity = endpoint_header_queue_->capacity();
    const uint32_t memory_size = payload_size * queue_capacity;

    endpoint_headers_ = (EndpointHeader*)aligned_malloc(ADK_PAGE_SIZE, memory_size);
    assert(endpoint_headers_);

    EndpointHeader* current_header = endpoint_headers_;
    for (uint32_t index = 0; index < queue_capacity; ++index)
    {
        new (current_header) EndpointHeader();

        current_header->tcp_engine_impl = this;
        endpoint_header_queue_->Push(current_header);
        current_header = ptr_add(current_header, payload_size);
    }

    if (engine_props.HasValue(config::kLocalPortRangeLow)
        || engine_props.HasValue(config::kLocalPortRangeHigh))
    {
        const auto range_low = engine_props.GetValue(config::kLocalPortRangeLow, 
                                                     default_value::kLocalPortRangeLow);
        const auto range_high = engine_props.GetValue(config::kLocalPortRangeHigh, 
                                                      default_value::kLocalPortRangeHigh);

        local_ports_register_ = new LocalPorts(range_low, range_high);
    }

    if (!engine_props.HasValue(config::kIsTxLowLatency))
    {
        std::string tx_low_env = engine_name_.empty() 
                               ? ENV_TX_LOW_LATENCY 
                               : engine_name_ + "_" + ENV_TX_LOW_LATENCY;
        const char* tx_low_latency = std::getenv(tx_low_env.c_str());
        if (nullptr != tx_low_latency)
        {
            tx_low_latency_ = (('Y' == *tx_low_latency) || ('y' == *tx_low_latency));
        }
    }
    else
    {
        tx_low_latency_ = engine_props.GetValue(config::kIsTxLowLatency, 
                                                default_value::kIsTxLowLatency);
    }

    if (!engine_props.HasValue(config::kIsRxLowLatency))
    {
        std::string rx_low_env = engine_name_.empty() 
                               ? ENV_RX_LOW_LATENCY
                               : engine_name_ + "_" + ENV_RX_LOW_LATENCY;
        const char* rx_low_latency = std::getenv(rx_low_env.c_str());
        if (nullptr != rx_low_latency)
        {
            rx_low_latency_ = (('Y' == *rx_low_latency) || ('y' == *rx_low_latency));
        }
    }
    else
    {
        rx_low_latency_ = engine_props.GetValue(config::kIsRxLowLatency,
                                                default_value::kIsRxLowLatency);
    }

    rx_memory_block_size_ = engine_props.GetValue(config::kRxMemoryBlockSize,
                                                  default_value::kRxMemoryBlockSize);

    blank_endpoint_header_ = new EndpointHeader;
    blank_endpoint_header_->tx_status = TxStatus::kRelease;

    auto* const tx_message_queue = TxMessageQueue::Create("blank tx message queue", 0);
    assert(tx_message_queue);

    tx_message_queue->set_release_alert();
    while (ErrorCode::kSuccess == tx_message_queue->TryPush(nullptr));

    blank_endpoint_header_->tx_message_queue = tx_message_queue;
    blank_endpoint_header_->tcp_endpoint = &verbs::s_tcp_endpoint_blank;
    blank_endpoint_header_->heartbeat_handler = blank_heartbeat_handler();
    blank_endpoint_header_->tcp_engine_impl = this;
    blank_endpoint_header_->rx_status = RxStatus::kRelease;
    blank_endpoint_header_->message_handler = blank_message_handler();
    blank_endpoint_header_->decode_template = blank_decode_template();
    blank_endpoint_header_->phy_status = PhyStatus::kRelease;
    blank_endpoint_header_->rx_cork_stt = EpRxCorkStat::kRxCorkLck;
    drive_engine_ = new DriveEngine(this);
    int32_t result = drive_engine_->Init(engine_props);
    if (ADK_UNLIKELY(ErrorCode::kSuccess != result))
    {
        goto error_exit;
    }

    return ErrorCode::kSuccess;

error_exit:
    Exit();
    return result;
}

void TcpEngineImpl::Exit()
{
    if (nullptr != drive_engine_)
    {
        drive_engine_->Exit();
        delete drive_engine_;
        drive_engine_ = nullptr;
    }

    do 
    {
        lock_guard<mutex> sgtn_lock(singleton_ep_lock_);
        singleton_ep_map_.clear();
    } while (false);

    do 
    {
        lock_guard<mutex> acpt_lock(acceptor_lock_);
        for (const auto& acpt_pair : acceptor_map_)
        {
            TcpAcceptor::Destroy(acpt_pair.second);
        }

        acceptor_map_.clear();
    } while (false);

    {
        lock_guard<mutex> _(phy_eps_lock_);
        for (auto& endpoint_header : phy_eps_set_)
        {
            assert(endpoint_header);
            auto* const tx_message_queue = endpoint_header->tx_message_queue;
            assert(tx_message_queue);

            tx_message_queue->set_release_alert();
        }

        phy_eps_set_.clear();
    }

    if (nullptr != endpoint_header_queue_)
    {
        assert(endpoint_headers_);

        const uint32_t payload_size = ADK_ROUND_UP(sizeof(EndpointHeader), ADK_CACHE_LINE_SIZE);
        const uint32_t queue_capacity = (uint32_t)(endpoint_header_queue_->capacity());

        EndpointHeader* current_header = endpoint_headers_;
        for (uint32_t index = 0; index < queue_capacity; ++index)
        {
            current_header->~EndpointHeader();
            current_header = ptr_add(current_header, payload_size);
        }

        aligned_free(endpoint_headers_);
        endpoint_headers_ = nullptr;

        EndpointHeaderQueue::Delete(endpoint_header_queue_);
        endpoint_header_queue_ = nullptr;
    }

    if (nullptr != recycle_eps_queue_)
    {
        TcpEndpoint* tcp_endpoint;
        while (ErrorCode::kSuccess == recycle_eps_queue_->TryPop(tcp_endpoint))
        {
            delete tcp_endpoint;
        }

        RecycleEpsQueue::Delete(recycle_eps_queue_);
        recycle_eps_queue_ = nullptr;
    }

    if (nullptr != tcp_stack_)
    {
        verbs::ITcpStack::Destroy(tcp_stack_);
        tcp_stack_ = nullptr;
    }

    if (nullptr != local_ports_register_)
    {
        delete local_ports_register_;
        local_ports_register_ = nullptr;
    }

    if (nullptr != blank_endpoint_header_)
    {
        auto* const tx_message_queue = blank_endpoint_header_->tx_message_queue;
        assert(tx_message_queue);

        TxMessageQueue::Delete(tx_message_queue);

        delete blank_endpoint_header_;
        blank_endpoint_header_ = nullptr;
    }

    IoEngineBase::Exit();
}

Acceptor* TcpEngineImpl::ToAccept(const Property& accept_props)
{
    using namespace config::acceptor;

    const string listen_ip = accept_props.GetValue(kListenIp, string());
    const uint16_t listen_port = accept_props.GetValue(kListenPort, default_value::kInvalidPort);
    /*
    if (ADK_UNLIKELY(default_value::kInvalidPort == listen_port))
    {
        SetErrorInfo((boost::format("listen port <%1%> is invalid") % listen_port).str());
        return nullptr;
    }*/

    auto event_handler = accept_props.GetValue(kEventHandler, Pointer()).as_ptr<EventHandler*>();
    if (ADK_UNLIKELY(nullptr == event_handler))
    {
        if (ADK_UNLIKELY(nullptr == default_event_handler_))
        {
            SetErrorInfo("event handler is null");
            return nullptr;
        }

        event_handler = default_event_handler_;
    }

    auto accept_handler = accept_props.GetValue(kAcceptHandler, Pointer()).as_ptr<AcceptHandler*>();
    if (ADK_UNLIKELY(nullptr == accept_handler))
    {
        if (ADK_UNLIKELY(nullptr == default_accept_handler_))
        {
            SetErrorInfo("accept handler is null");
            return nullptr;
        }

        accept_handler = default_accept_handler_;
    }

    TcpAcceptor* acceptor_impl = nullptr;
    {
        lock_guard<mutex> _(acceptor_lock_);
        if (ADK_UNLIKELY(acceptor_map_.end() != acceptor_map_.find(listen_port)))
        {
            SetErrorInfo((boost::format("acceptor listening at port <%1%> is already exist")
                          % listen_port).str());
            return nullptr;
        }

        acceptor_impl = CreateAcceptor(accept_props, event_handler, accept_handler);
        if (nullptr != acceptor_impl)
        {
            acceptor_map_[acceptor_impl->listen_port()] = acceptor_impl;
        }
        else
        {
            return nullptr;
        }
    }

    AcceptTask* accept_task = new AcceptTask;
    accept_task->task_type = TaskType::kAsynAccept;
    accept_task->acceptor_impl = acceptor_impl;
    const auto result = drive_engine_->AddTaskAccept(accept_task);
    if (ADK_UNLIKELY(ErrorCode::kSuccess != result))
    {
        {
            lock_guard<mutex> _(acceptor_lock_);
            acceptor_map_.erase(acceptor_impl->listen_port());
        }

        TcpAcceptor::Destroy(acceptor_impl);
        delete accept_task;

        SetErrorInfo("the number of acceptor reached the upper limit");
        return nullptr;
    }

    return static_cast<Acceptor*>(acceptor_impl);
}

Endpoint* TcpEngineImpl::ToConnect(const Property& connect_props)
{
    using namespace config::endpoint;

    const auto remote_ip = connect_props.GetValue(kRemoteIp, string());
    const auto remote_port = connect_props.GetValue(kRemotePort, 
                                                    default_value::kInvalidPort);

    if (ADK_UNLIKELY(remote_ip.empty() || (default_value::kInvalidPort == remote_port)))
    {
        SetErrorInfo((boost::format("remote address <%1%:%2%> is invalid")
                      % remote_ip % remote_port).str());
        return nullptr;
    }

    auto* event_handler = connect_props.GetValue(kEventHandler, Pointer()).as_ptr<EventHandler*>();
    if (ADK_UNLIKELY(nullptr == event_handler))
    {
        if (ADK_UNLIKELY(nullptr == default_event_handler_))
        {
            SetErrorInfo("event handler is null");
            return nullptr;
        }

        event_handler = default_event_handler_;
    }

    auto* connect_handler = connect_props.GetValue(kConnectHandler, Pointer()).as_ptr<ConnectHandler*>();
    if (ADK_UNLIKELY(nullptr == connect_handler))
    {
        if (ADK_UNLIKELY(nullptr == default_connect_handler_))
        {
            SetErrorInfo("connect handler is null");
            return nullptr;
        }

        connect_handler = default_connect_handler_;
    }

    void* const share_ctx = connect_props.GetValue(kShareContext, Pointer()).as_ptr<void*>();
    void* const private_ctx = connect_props.GetValue(kPrivateContext, Pointer()).as_ptr<void*>();
    const bool is_singleton = connect_props.GetValue(kIsSingleton, default_value::kIsSingleton);

    TcpEndpoint* endpoint_impl = nullptr;
    if (is_singleton)
    {
        lock_guard<mutex> _1(singleton_ep_lock_);
        EndpointHeader*& endpoint_header = singleton_ep_map_[std::make_pair(remote_ip, remote_port)];
        if (nullptr != endpoint_header)
        {
            bool is_phy_alive = false;
            EndpointChain& sub_endpoints = endpoint_header->sub_endpoints;

            lock_guard<mutex> _2(endpoint_header->lock);

            /**
             * check has alive sub-endpoint protected by endpoint_header->lock
             */
            for (auto& endpoint : sub_endpoints)
            {
                if (endpoint->is_alive())
                {
                    is_phy_alive = true;
                    break;
                }
            }

            if (is_phy_alive)
            {
                endpoint_impl = TcpEndpoint::Duplicate(endpoint_header, 
                                                       connect_props, 
                                                       event_handler, 
                                                       connect_handler);
                if (ADK_UNLIKELY(endpoint_header->is_running()))
                {
                    int32_t result;
                    endpoint_impl->add_reference();
                    do
                    {
                        // the physical endpoint is normal, direct to deliver immediate task
                        result = drive_engine_->AddImmediateTask(endpoint_impl, TaskType::kImmConnect);
                    } while (ADK_UNLIKELY(ErrorCode::kSuccess != result));
                }

                if (nullptr != share_ctx)
                {
                    endpoint_impl->set_share_ctx(share_ctx);
                }

                if (nullptr != private_ctx)
                {
                    endpoint_impl->set_private_ctx(private_ctx);
                }

                return (Endpoint*)endpoint_impl;
            }
        }

        endpoint_impl = CreateEndpoint(connect_props, event_handler, connect_handler);
        if (nullptr != endpoint_impl)
        {
            // record the new singleton header
            endpoint_header = endpoint_impl->endpoint_header();
        }
    }
    else
    {
        endpoint_impl = CreateEndpoint(connect_props, event_handler, connect_handler);
    }
    
    if (ADK_UNLIKELY(nullptr == endpoint_impl))
    {
        return nullptr;
    }

    if (nullptr != share_ctx)
    {
        endpoint_impl->set_share_ctx(share_ctx);
    }

    if (nullptr != private_ctx)
    {
        endpoint_impl->set_private_ctx(private_ctx);
    }

    RegisterEndpoint(endpoint_impl->endpoint_header());

    ConnectTask* const connect_task = new ConnectTask;
    connect_task->task_type = TaskType::kAsynConnect;
    connect_task->endpoint_header = endpoint_impl->endpoint_header();
    connect_task->remote_ip = remote_ip;
    connect_task->remote_port = remote_port;
    connect_task->retry_times = 0;
    connect_task->limit_times = connect_props.GetValue(config::endpoint::kRetryConnectTimes,
                                                       default_value::kRetryConnectTimes);
    /**
     * limit_times must larger than zero
     */
    connect_task->limit_times = std::max<uint32_t>(connect_task->limit_times, 1);

    connect_task->interval_us = connect_props.GetValue(config::endpoint::kConnectIntervalMilli,
                                                       default_value::kConnectIntervalMilli) * 1000;
    connect_task->interval_us = std::max<int64_t>(100 * 1000, connect_task->interval_us);

    int32_t result;
    do 
    {
        result = drive_engine_->AddTaskConnect(connect_task);
    } while (ADK_UNLIKELY(ErrorCode::kSuccess != result));

    return (Endpoint*)endpoint_impl;
}

TcpAcceptor* TcpEngineImpl::CreateAcceptor(const Property& aptr_props, 
                                           EventHandler* const event_handler, 
                                           AcceptHandler* const accept_handler)
{
    TcpAcceptor* const acceptor_impl = new TcpAcceptor(this);
    assert(acceptor_impl);

    if (ADK_UNLIKELY(ErrorCode::kSuccess != acceptor_impl->Init(aptr_props)))
    {
        TcpAcceptor::Destroy(acceptor_impl);
        return nullptr;
    }

    acceptor_impl->set_event_handler(event_handler);
    acceptor_impl->set_accept_handler(accept_handler);

    return acceptor_impl;
}

TcpEndpoint* TcpEngineImpl::CreateEndpoint()
{
    assert(endpoint_header_queue_);

    EndpointHeader* endpoint_header;
    if (ADK_UNLIKELY(ErrorCode::kSuccess != endpoint_header_queue_->TryPop(endpoint_header)))
    {
        SetErrorInfo((boost::format("connection resource has reach the upper limit <%1%>")
                      % endpoint_header_queue_->capacity()).str());
        return nullptr;
    }

    endpoint_header->version_alloc();

    TcpEndpoint* endpoint_impl = new TcpEndpoint(endpoint_header);
    return endpoint_impl;
}

TcpEndpoint* TcpEngineImpl::CreateEndpoint(const Property& ep_props, 
                                           EventHandler* const event_hdl, 
                                           ConnectHandler* const connect_hdl)
{
    TcpEndpoint* const endpoint_impl = CreateEndpoint();
    if (ADK_UNLIKELY(nullptr == endpoint_impl))
    {
        return nullptr;
    }

    if (ErrorCode::kSuccess != endpoint_impl->Init(ep_props, event_hdl, connect_hdl))
    {
        SetErrorInfo(strerror(errno));
        DestroyCreatingEndpoint(endpoint_impl);
        return nullptr;
    }

    return endpoint_impl;
}

TcpEndpoint* TcpEngineImpl::CreateEndpoint(const Property& ep_props, 
                                           EventHandler* const event_hdl, 
                                           ITcpEndpoint* const tcp_endpoint)
{
    TcpEndpoint* const endpoint_impl = CreateEndpoint();
    if (ADK_UNLIKELY(nullptr == endpoint_impl))
    {
        return nullptr;
    }

    if (ErrorCode::kSuccess != endpoint_impl->Init(ep_props, event_hdl, tcp_endpoint))
    {
        DestroyCreatingEndpoint(endpoint_impl);
        return nullptr;
    }

    return endpoint_impl;
}

void TcpEngineImpl::DestroyCreatingEndpoint(TcpEndpoint* const endpoint_impl)
{
    assert(endpoint_impl);

    EndpointHeader* const endpoint_header = endpoint_impl->endpoint_header();
    assert(endpoint_header);
    
    TcpEndpoint::Destroy(endpoint_impl);

    endpoint_header->sub_endpoints.clear();
    TcpEndpoint::Exit(endpoint_header);

    __attribute__((unused)) const auto ec = endpoint_header_queue_->Push(endpoint_header);
    assert(ErrorCode::kSuccess == ec);
}

void TcpEngineImpl::ToCloseEndpoint(TcpEndpoint* const endpoint_impl)
{
    int result;
    do
    {
        result = drive_engine_->AddDeleteTask(endpoint_impl);
    } while (ADK_UNLIKELY(ErrorCode::kSuccess != result));
}

void TcpEngineImpl::ToCloseAcceptor(TcpAcceptor* const acceptor_impl)
{
    assert(acceptor_impl);

    lock_guard<mutex> acpt_lock(acceptor_lock_);
    const uint16_t listen_port = acceptor_impl->listen_port();
    const auto iter = acceptor_map_.find(listen_port);
    assert(acceptor_map_.end() != iter);
    assert(acceptor_impl == iter->second);
    acceptor_map_.erase(iter);
}

int32_t TcpEngineImpl::DestroyEndpoint(TcpEndpoint* const endpoint_impl)
{
    assert(endpoint_impl);

    auto* const endpoint_header = endpoint_impl->endpoint_header();
    assert(endpoint_header);

    bool to_delete_phy = true;
    EndpointChain& sub_endpoints = endpoint_header->sub_endpoints;
    if (endpoint_header->is_singleton)
    {
        const string& remote_ip = endpoint_header->remote_ip();
        const uint16_t remote_port = endpoint_header->remote_port();

        lock_guard<mutex> _1(singleton_ep_lock_);
        const auto sgtn_iter = singleton_ep_map_.find(std::make_pair(remote_ip, remote_port));

        lock_guard<mutex> _2(endpoint_header->lock);
        if (ADK_UNLIKELY(endpoint_impl->reference() > 1))
        {
            return ErrorCode::kFailure;
        }

        /**
         * remove sub-endpoints here to make sure only the last virtual endpoint trige to delete endpoint header
         *
         * do this process protected by endpoint_header->lock
         */
        for (auto iter = sub_endpoints.begin(); iter != sub_endpoints.end(); ++iter)
        {
            if (endpoint_impl == ((TcpEndpoint*)(*iter)))
            {
                sub_endpoints.erase(iter);
                break;
            }
        }

        if (0 == sub_endpoints.size())
        {
            if ((singleton_ep_map_.end() != sgtn_iter) && (endpoint_header == sgtn_iter->second))
            {
                singleton_ep_map_.erase(sgtn_iter);
            }
        }
        else
        {
            to_delete_phy = false;
        }
    }
    else
    {
        // call in io control
        lock_guard<mutex> _(endpoint_header->lock);
        if (ADK_UNLIKELY(endpoint_impl->reference() > 1))
        {
            return ErrorCode::kFailure;
        }

        sub_endpoints.clear();
    }

    if (to_delete_phy)
    {
        assert(drive_engine_);

        /**
         * send thread 
         *      -> call PreSendHandler
         * 
         * recv thread
         *      -> call PreRecvHandler
         * 
         * Application may use endpoint resource in PreXHandler callback
         * 
         * So before delete endpint, must wait io thread drop phy endpoint
         * 
         * 1. set phy endpoint to delete flag, notify io thread to drop phy endpoint
         * 
         * 2. Based on the PreXHandler usage, try to unreg the phy endpoint
         * 
         * 3. Check io thread has dropped the phy endpoint
         * 
         * 4. Deliver Endpoint closed event
         * 
         * 5. Close the endpoint
         */
        endpoint_header->set_to_delete();
        if (nullptr != pre_send_handler())
        {
            if (!endpoint_header->is_tx_release())
            {
                drive_engine_->UnregTxEndpoint(endpoint_header);
                if (!endpoint_header->is_tx_release())
                {
                    return ErrorCode::kFailure;
                }
            }
        }

        if (nullptr != pre_recv_handler())
        {
            if (!endpoint_header->is_rx_release())
            {
                drive_engine_->UnregRxEndpoint(endpoint_header);
                if (!endpoint_header->is_rx_release())
                {
                    return ErrorCode::kFailure;
                }
            }
        }

        UnregisterEndpoint(endpoint_header);

        int result;
        do
        {
            result = drive_engine_->AddDeleteTask(endpoint_header);
        } while (ADK_UNLIKELY(ErrorCode::kSuccess != result));
    }

    EventEpClosed ep_closed;
    endpoint_impl->OnEvent(&ep_closed);

    TcpEndpoint::Destroy(endpoint_impl);

    return ErrorCode::kSuccess;
}

void TcpEngineImpl::DestroyEndpoint(EndpointHeader* const endpoint_header)
{
    TcpEndpoint::Exit(endpoint_header);
    ADK_ASSERT_SUCCESS(endpoint_header_queue_->Push(endpoint_header));
}

void TcpEngineImpl::DestroyInproEndpoint(EndpointHeader* const endpoint_header, 
                                         const string& remote_ip,
                                         uint16_t remote_port)
{
    assert(endpoint_header);
    assert(endpoint_header->phy_status < PhyStatus::kStatusMax);

    bool to_delete = false;
    EventEpClosed event_ep_closed;

    if (endpoint_header->is_singleton)
    {
        lock_guard<mutex> sgtn_lock(singleton_ep_lock_);
        const auto iter = singleton_ep_map_.find(std::make_pair(remote_ip, remote_port));
        if ((singleton_ep_map_.end() != iter) && (endpoint_header == iter->second))
        {
            singleton_ep_map_.erase(iter);
        }

        EndpointChain sub_endpoints;
        do
        {
            lock_guard<mutex> share_lock(endpoint_header->lock);
            endpoint_header->sub_endpoints.swap(sub_endpoints);
        } while (false);

        for (auto sub_iter = sub_endpoints.begin(); sub_iter != sub_endpoints.end(); ++sub_iter)
        {
            TcpEndpoint* const endpoint_impl_del = (TcpEndpoint*)(*sub_iter);
            endpoint_impl_del->OnEvent(&event_ep_closed);

            TcpEndpoint::Destroy(endpoint_impl_del);
            to_delete = true;
        }
    }
    else
    {
        // call in io control
        if (0 != endpoint_header->sub_endpoints.size())
        {
            assert(1 == endpoint_header->sub_endpoints.size());
            TcpEndpoint* const endpoint_impl_del = *(endpoint_header->sub_endpoints.begin());
            endpoint_impl_del->OnEvent(&event_ep_closed);

            TcpEndpoint::Destroy(endpoint_impl_del);
            to_delete = true;

            lock_guard<mutex> share_lock(endpoint_header->lock);
            endpoint_header->sub_endpoints.clear();
        }
    }

    // avoid to repeat delete
    if (to_delete)
    {
        UnregisterEndpoint(endpoint_header);

        TcpEndpoint::Exit(endpoint_header);
        __attribute__((unused)) const auto ec = endpoint_header_queue_->Push(endpoint_header);
        assert(ErrorCode::kSuccess == ec);
    }
}

void TcpEngineImpl::RecycleEndpoint(TcpEndpoint* endpoint_impl)
{
    assert(endpoint_impl);
    endpoint_impl->set_endpoint_header(blank_endpoint_header_);

    assert(recycle_eps_queue_);
    while (ADK_UNLIKELY(ErrorCode::kSuccess != recycle_eps_queue_->TryPush(endpoint_impl)))
    {
        TcpEndpoint* recycle_endpoint;
        if (ErrorCode::kSuccess == recycle_eps_queue_->TryPop(recycle_endpoint))
        {
            delete recycle_endpoint;
        }
    }
}

void TcpEngineImpl::RegisterEndpoint(EndpointHeader* const endpoint_header)
{
    lock_guard<mutex> _(phy_eps_lock_);
    phy_eps_set_.insert(endpoint_header);
}

void TcpEngineImpl::UnregisterEndpoint(EndpointHeader* const endpoint_header)
{
    lock_guard<mutex> _(phy_eps_lock_);
    phy_eps_set_.erase(endpoint_header);
}

int32_t TcpEngineImpl::CollectIndicator(std::string& indicator)
{
    boost::property_tree::ptree indicator_ptree;

    assert(drive_engine_);

    ControlActor* const control_actor = drive_engine_->control_actor();
    assert(control_actor);

    boost::property_tree::ptree& acceptor_pt = indicator_ptree.add_child("Acceptor",
        boost::property_tree::ptree());

    control_actor->AcceptorIndicator(acceptor_pt);
    {
        lock_guard<mutex> _(acceptor_lock_);
        acceptor_pt.put("working_nr", acceptor_map_.size());
    }

    boost::property_tree::ptree& connect_pt = indicator_ptree.add_child("Connect",
        boost::property_tree::ptree());
    control_actor->ConnectIndicator(connect_pt);

    boost::property_tree::ptree& endpoint_pt = indicator_ptree.add_child("Endpoint",
        boost::property_tree::ptree());

    endpoint_pt.put("closed_nr", endpoint_header_queue_->last_push_sqn() - endpoint_header_queue_->capacity());

    std::lock_guard<mutex> _(phy_eps_lock_);
    endpoint_pt.put("working_nr", phy_eps_set_.size());
    for (auto& ep_header : phy_eps_set_)
    {
        const ITcpEndpoint* const tcp_endpoint = ep_header->tcp_endpoint;
        const string endpoint_name = (boost::format("%1%:%2%-%3%:%4%")
                                                     % tcp_endpoint->local_ip() 
                                                     % tcp_endpoint->local_port() 
                                                     % tcp_endpoint->remote_ip() 
                                                     % tcp_endpoint->remote_port()).str();

        boost::property_tree::ptree& sub_ep_pt = endpoint_pt.add_child(boost::property_tree::ptree::path_type(endpoint_name, '|'),
            boost::property_tree::ptree());

        static string s_ep_status[] = { "Connecting", "Established", "ToDelete", "Release" };
        auto phy_status = static_cast<int32_t>(ep_header->phy_status);
        if (phy_status < (int32_t)(sizeof(s_ep_status) / sizeof(string)))
        {
            sub_ep_pt.put("status", s_ep_status[phy_status]);
        }

        static string s_tx_status[] = { "Init", "Idle", "Active", "Block", "Release", "DirectSend" };
        auto tx_status = static_cast<uint16_t>(ep_header->tx_status);
        if (tx_status < sizeof(s_tx_status) / sizeof(string))
        {
            sub_ep_pt.put("tx_status", s_tx_status[tx_status]);
        }

        static string s_rx_status[] = { "Init", "Active", "Idle", "Release" };
        auto rx_status = static_cast<uint16_t>(ep_header->rx_status);
        if (rx_status < sizeof(s_rx_status) / sizeof(string))
        {
            sub_ep_pt.put("rx_status", s_rx_status[rx_status]);
        }

        const auto tx_message_queue = ep_header->tx_message_queue;
        sub_ep_pt.put("tx_msg_qlen", tx_message_queue->length());
        sub_ep_pt.put("tx_msg_nr", tx_message_queue->last_pop_sqn());
        sub_ep_pt.put("tx_msg_bytes", ep_header->tx_message_bytes);
        sub_ep_pt.put("tx_dispatch_nr", ep_header->tx_dispatch1_nr);

        sub_ep_pt.put("rx_msg_bytes", ep_header->rx_message_bytes);
        const auto deliver_message_nr = ACCESS_ONCE(ep_header->deliver_message_nr);
        sub_ep_pt.put("rx_deliver_nr", (deliver_message_nr + 1) >> 1);
        sub_ep_pt.put("rx_process_nr", deliver_message_nr >> 1);
        sub_ep_pt.put("rx_dispatch_nr", ep_header->rx_dispatch1_nr);
    }

    std::ostringstream oss;
    boost::property_tree::json_parser::write_json(oss, indicator_ptree);
    indicator = oss.str();
    return ErrorCode::kSuccess;
}

int32_t TcpEngineImpl::Pause()
{
    assert(drive_engine_);
    return drive_engine_->PauseRxActor();
}

void TcpEngineImpl::Resume()
{
    assert(drive_engine_);
    drive_engine_->ResumeRxActor();
}

void TcpEngineImpl::UninstallHandler(EndpointHeader* const endpoint_header)
{
    assert(endpoint_header);
    TxMessageQueue* const tx_message_queue = endpoint_header->tx_message_queue;
    assert(tx_message_queue);

    tx_message_queue->set_release_alert();

    auto* const decode_template = endpoint_header->decode_template;
    auto* const message_handler = endpoint_header->message_handler;

    endpoint_header->decode_template = blank_decode_template();
    endpoint_header->message_handler = blank_message_handler();
    endpoint_header->heartbeat_handler = blank_heartbeat_handler();

    if ((decode_template != endpoint_header->decode_template)
        || (message_handler != endpoint_header->message_handler))
    {
        auto* const actor_arena = IOActor::current_actor_arena();
        if ((nullptr != actor_arena) && actor_arena->delivering)
        {
            return;
        }

        ///> rx actor read-copy update
        assert(drive_engine_);
        drive_engine_->RxActorRCU(actor_arena);
    }
}

}

}
