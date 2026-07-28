#include "last_error.h"
#include "drive_engine.h"
#include "endpoint_impl.h"
#include "endpoint_register.h"

#include <adk/io_engine/config_key.h>

namespace adk_impl
{

namespace io_engine
{

EndpointHeader::EndpointHeader()
{
    Reset();
    tx_message_queue = nullptr;
    tcp_endpoint = nullptr;
    token_bucket = nullptr;
    deliver_message = nullptr;
    version = 0;
}

void EndpointHeader::Reset()
{
    tx_status = TxStatus::kInit;
    tx_status_lock = false;
    tx_valid = true;
    tx_min_resident = default_value::kTxMinResidentMicro;
    tx_sch_time = 0;
    send_actor_impl = nullptr;
    tx_message_bytes = 0;

    heartbeat_handler = nullptr;

    tx_dispatch1_nr = 0;
    tx_dispatch2_nr = 0;
    tx_consume_flag = 0;

    rx_status = RxStatus::kInit;
    rx_valid = true;
    rx_min_resident = default_value::kRxMinResidentMicro;
    rx_sch_time = 0;
    heartbeat_timeout = kuint64Max;

    rx_message_bytes = 0;
    deliver_message_nr = 0;
    message_handler = nullptr;
    decode_template = nullptr;
    share_ctx = nullptr;

    rx_dispatch1_nr = 0;
    rx_dispatch2_nr = 0;

    is_singleton = false;
    sub_endpoints.clear();
    distributor = 0;
    phy_status = PhyStatus::kStatusMax;
    phy_time = 0;
    rx_cork_stt = EpRxCorkStat::kRxCorkNot;
}

bool EndpointHeader::SetRxCorkPre()
{
    auto* const actor_arena = IOActor::current_actor_arena();
    if ((nullptr != actor_arena) && actor_arena->delivering)
    {
        if (rx_cork_stat() != EpRxCorkStat::kRxCorkLck)
        {
            rx_cork_stt = EpRxCorkStat::kRxCorkPre;
            return true;
        }
    }
    return false;
} 

bool EndpointHeader::SetRxUncork()
{
    auto drv = tcp_engine_impl->drive_engine();
    assert(drv);

    if (__sync_bool_compare_and_swap(&rx_cork_stt,
                                    EpRxCorkStat::kRxCorkSet,
                                    EpRxCorkStat::kRxCorkNot))
    {
        drv->DispatchRxEndpoint(this);
        return true;
    }
    else
    {
        return false;
    }
}

TcpEndpoint::TcpEndpoint(EndpointHeader* const endpoint_header)
{
    sub_index_ = 0;
    private_ctx_ = nullptr;
    endpoint_header_ = endpoint_header;

    ep_status_ = EpStatus::kInit;
    reference_ = 1;
    event_handler_ = nullptr;
    connect_handler_ = nullptr;

    EndpointRegister::RegisterEndpoint(this);
}

TcpEndpoint::~TcpEndpoint()
{
    EndpointRegister::UnregisterEndpoint(this);
}

int32_t TcpEndpoint::Init(const Property& props)
{
    assert(endpoint_header_);
 
    using namespace config::endpoint;
    /**
     * resize message queue size, avoid overflow when batch send
     */
    const auto tx_queue_size = std::max<uint32_t>(
        props.GetValue(kTxMessageQueueSize, default_value::kTxMessageQueueSize), kTxBatchSize * 2);

    auto* tx_message_queue = TxMessageQueue::Create("tx_msg_queue", tx_queue_size);
    if (ADK_UNLIKELY(nullptr == tx_message_queue))
    {
        tx_message_queue = TxMessageQueue::Create("tx_msg_queue", default_value::kTxMessageQueueSize);
        if (ADK_UNLIKELY(nullptr == tx_message_queue))
        {
            SetErrorInfo("create TxMessageQueue failed");
            return ErrorCode::kNoMemory;
        }
    }

    assert(tx_message_queue);
    endpoint_header_->tx_message_queue = tx_message_queue;
 
    auto* const tcp_engine_impl = endpoint_header_->tcp_engine_impl;
    assert(tcp_engine_impl);

    auto* const deliver_message = RxMessagePool::NewMessage<false>(tcp_engine_impl->rx_memory_block_size(), 
                                                                   endpoint_header_);
    if (ADK_UNLIKELY(nullptr == deliver_message))
    {
        SetErrorInfo("allocate rx memory block failed");
        return ErrorCode::kNoMemory;
    }

    deliver_message->set_data_more(0);
    endpoint_header_->deliver_message = deliver_message;

    endpoint_header_->sub_endpoints.push_back(this);
    endpoint_header_->phy_status = PhyStatus::kConnecting;

    sub_index_ = endpoint_header_->allocate_index();

    endpoint_header_->heartbeat_timeout = props.GetValue(kHeartbeatTimeoutMilli, kuint64Max);
    if (kuint64Max != endpoint_header_->heartbeat_timeout)
    {
        endpoint_header_->heartbeat_timeout *= 1000;
    }

    endpoint_header_->message_handler = props.GetValue(kMessageHandler, Pointer())
                                             .as_ptr<MessageHandler*>();
    endpoint_header_->heartbeat_handler = props.GetValue(kHeartbeatHandler, Pointer())
                                               .as_ptr<HeartbeatHandler*>();

    uint32_t rate_control_kbyte = props.GetValue(kRateControlKBytes,
                                                 default_value::kRateControlKBytes);

    auto* const token_bucket = RateControl::GetInstance<rate_unit::Second>(rate_control_kbyte * 1024);
    assert(token_bucket);

    endpoint_header_->token_bucket = token_bucket;

    properties_ = props;
    return ErrorCode::kSuccess;
}

ITcpEndpoint* TcpEndpoint::CreateITcpEndpoint()
{
    using namespace config::endpoint;

    const auto reuse_addr = properties_.GetValue(kReuseAddr, default_value::kReuseAddr);
    const auto reuse_port = properties_.GetValue(kReusePort, default_value::kReusePort);
    auto* const tcp_endpoint = ITcpEndpoint::Create(tcp_engine_impl()->tcp_stack(), 
                                                    reuse_addr, 
                                                    reuse_port);
    if (ADK_UNLIKELY(nullptr == tcp_endpoint))
    {
        SetErrorInfo(errno);
        return nullptr;
    }

    uint16_t local_port = properties_.GetValue(kLocalPort, default_value::kInvalidPort);
    LocalPorts* const local_ports_register = tcp_engine_impl()->local_ports_register();
    if (nullptr != local_ports_register)
    {
        if (0 == local_port)
        {
            do
            {
                local_port = local_ports_register->AllocatePort();
                const auto bind_result = tcp_endpoint->Bind(local_port);
                if (static_cast<int32_t>(ITcpEndpoint::BindResult::kSuccess) == bind_result)
                {
                    local_ports_register->BindSuccess(local_port, tcp_endpoint->endpoint_id());
                    break;
                }

                if (static_cast<int32_t>(ITcpEndpoint::BindResult::kAddrInUse) == bind_result)
                {
                    local_ports_register->BindFail(local_port);
                }
                else
                {
                    std::string error_info = "bind <" + tcp_endpoint->local_ip() + ":" + std::to_string(local_port) + "> failed";
                    if (0 != errno)
                    {
                        SetErrorInfo(error_info + ", " + strerror(errno));
                    }
                    else
                    {
                        SetErrorInfo(error_info);
                    }

                    ITcpEndpoint::Destroy(tcp_endpoint);
                    return nullptr;
                }
            } while (true);
        }
        else
        {
            local_ports_register->ReservePort(local_port, tcp_endpoint->endpoint_id());
            goto specific_port_bind;
        }
    }
    else
    {
    specific_port_bind:
        const auto bind_result = tcp_endpoint->Bind(local_port);
        if (ADK_UNLIKELY(static_cast<int32_t>(ITcpEndpoint::BindResult::kAddrInUse) == bind_result))
        {
            SetErrorInfo("bind local port " + std::to_string(local_port) + " failed");
            ITcpEndpoint::Destroy(tcp_endpoint);
            return nullptr;
        }
        else if (ADK_UNLIKELY(static_cast<int32_t>(ITcpEndpoint::BindResult::kFailure) == bind_result))
        {
            std::string error_info = "bind <" + tcp_endpoint->local_ip() + ":" + std::to_string(local_port) + "> failed";
            if (0 != errno)
            {
                SetErrorInfo(error_info + ", " + strerror(errno));
            }
            else
            {
                SetErrorInfo(error_info);
            }

            ITcpEndpoint::Destroy(tcp_endpoint);
            return nullptr;
        }
    }

    return tcp_endpoint;
}

int32_t TcpEndpoint::Init(const Property& props, 
                          EventHandler* const event_handler, 
                          ConnectHandler* const connect_handler)
{
    const auto ec = Init(props);
    if (ADK_UNLIKELY(ErrorCode::kSuccess != ec))
    {
        return ec;
    }

    auto* const tcp_endpoint = CreateITcpEndpoint();
    if (ADK_UNLIKELY(nullptr == tcp_endpoint))
    {
        return ErrorCode::kFailure;
    }

    endpoint_header_->is_singleton = props.GetValue(config::endpoint::kIsSingleton, 
                                                    default_value::kIsSingleton);
    event_handler_ = event_handler;
    connect_handler_ = connect_handler;

    UpdateTcpProps<true>(tcp_endpoint, props);
    endpoint_header_->tcp_endpoint = tcp_endpoint;

    return ErrorCode::kSuccess;
}

int32_t TcpEndpoint::Init(const Property& props, 
                          EventHandler* const event_handler, 
                          ITcpEndpoint* const tcp_endpoint)
{
    const auto ec = Init(props);
    if (ADK_UNLIKELY(ErrorCode::kSuccess != ec))
    {
        return ec;
    }

    event_handler_ = event_handler;

    UpdateTcpProps<true>(tcp_endpoint, props);
    endpoint_header_->tcp_endpoint = tcp_endpoint;

    return ErrorCode::kSuccess;
}

TcpEndpoint* TcpEndpoint::Duplicate(EndpointHeader* const endpoint_header, 
                                    const Property& props,
                                    EventHandler* const event_handler, 
                                    ConnectHandler* const connect_handler)
{
    TcpEndpoint* endpoint_impl = new TcpEndpoint(endpoint_header);
    assert(endpoint_impl);

    endpoint_header->sub_endpoints.push_back(endpoint_impl);
    endpoint_impl->sub_index_ = endpoint_header->allocate_index();
    endpoint_impl->properties_ = props;

    endpoint_impl->event_handler_ = event_handler;
    endpoint_impl->connect_handler_ = connect_handler;

    return endpoint_impl;
}

void TcpEndpoint::Exit(EndpointHeader* const endpoint_header)
{
    assert(endpoint_header);
    endpoint_header->version_free();

    TcpEngineImpl* const tcp_engine_impl = endpoint_header->tcp_engine_impl;
    assert(tcp_engine_impl);

    TxMessageQueue* const tx_message_queue = endpoint_header->tx_message_queue;
    if (nullptr != tx_message_queue)
    {
        Message* message = nullptr;
        while (ErrorCode::kSuccess == tx_message_queue->Pop(message))
        {
            auto impl = (MessageImpl*)message;
            if (impl->is_last_reference())
            {
                IoEngineBase::DeleteTxMessage(impl);
            }
        }

        TxMessageQueue::Delete(tx_message_queue);
        endpoint_header->tx_message_queue = nullptr;
    }

    TokenBucket* const token_bucket = endpoint_header->token_bucket;
    if (nullptr != token_bucket)
    {
        token_bucket->Release();
        endpoint_header->token_bucket = nullptr;
    }

    ITcpEndpoint* const tcp_endpoint = endpoint_header->tcp_endpoint;
    if (nullptr != tcp_endpoint)
    {
        LocalPorts* const local_ports_register = tcp_engine_impl->local_ports_register();
        if (nullptr != local_ports_register)
        {
            local_ports_register->FreePort(tcp_endpoint->endpoint_id());
        }

        ITcpEndpoint::Destroy(tcp_endpoint);
        endpoint_header->tcp_endpoint = nullptr;
    }

    auto* const deliver_message = endpoint_header->deliver_message;
    if (nullptr != deliver_message)
    {
        IoEngineBase::DeleteRxMessage(deliver_message);
        endpoint_header->deliver_message = nullptr;
    }

    //从DestroyInproEndpoint调用Exit时候rx_cork_stat() == EpRxCorkStat::kRxCorkNot
    //assert(endpoint_header->rx_cork_stat() == EpRxCorkStat::kRxCorkLck);
    endpoint_header->Reset();
}

void TcpEndpoint::Destroy(TcpEndpoint* const endpoint_impl)
{
    auto* const endpoint_header = endpoint_impl->endpoint_header_;
    assert(endpoint_header);

    auto* const tcp_engine_impl = endpoint_header->tcp_engine_impl;
    assert(tcp_engine_impl);

    tcp_engine_impl->RecycleEndpoint(endpoint_impl);
}

template<bool kDefaultInit>
void TcpEndpoint::UpdateTcpProps(ITcpEndpoint* const tcp_endpoint,const Property& props)
{
    using namespace config::endpoint;

    assert(tcp_endpoint);
    if (kDefaultInit || props.HasValue(kTcpNoDelay))
    {
        tcp_endpoint->SetOption(ITcpEndpoint::OptionType::kTcpNoDelay,
                                props.GetValue(kTcpNoDelay, 
                                               default_value::kTcpNoDelay));
    }

    if (kDefaultInit || props.HasValue(kSocketSendBufferKBytes))
    {
        tcp_endpoint->SetOption(ITcpEndpoint::OptionType::kSendBuffer,
                                1024 * props.GetValue(kSocketSendBufferKBytes, 
                                                      default_value::kSocketSendBufferKBytes));
    }

    if (kDefaultInit || props.HasValue(kSocketRecvBufferKBytes))
    {
        tcp_endpoint->SetOption(ITcpEndpoint::OptionType::kRecvBuffer,
                                1024 * props.GetValue(kSocketRecvBufferKBytes, 
                                                      default_value::kSocketRecvBufferKBytes));
    }
}

void TcpEndpoint::UpdateTcpProps(const Property& props)
{
    assert(endpoint_header_);
    UpdateTcpProps<false>(endpoint_header_->tcp_endpoint, props);
}

void TcpEndpoint::OnEvent(Event* const event)
{
    assert(event_handler_);
    event_handler_->OnEvent((Endpoint*)this, event);
}

void TcpEndpoint::DeliverEvent(EndpointHeader* const endpoint_header, Event* const event)
{
    if (endpoint_header->is_singleton)
    {
        AddenCallback(endpoint_header, [=](TcpEndpoint* const endpoint_impl) {
            if (endpoint_impl->is_alive())
            {
                endpoint_impl->OnEvent(event);
            }
        });
    }
    else
    {
        do 
        {
            TcpEndpoint* endpoint_impl;
            {
                lock_guard<mutex> _(endpoint_header->lock);
                const EndpointChain& sub_endpoints = endpoint_header->sub_endpoints;
                if (ADK_UNLIKELY(0 == sub_endpoints.size()))
                {
                    break;
                }

                assert(1 == sub_endpoints.size());
                endpoint_impl = sub_endpoints.front();
                endpoint_impl->add_reference();
            }

            if (endpoint_impl->is_alive())
            {
                endpoint_impl->OnEvent(event);
            }

            endpoint_impl->sub_reference();
        } while (false);
    }
}

int32_t TcpEndpoint::DeliverWarnEvent(EndpointHeader* const endpoint_header, Event* const event)
{
    int32_t result = ErrorCode::kFailure;
    if (endpoint_header->is_singleton)
    {
        AddenCallback(endpoint_header, [&](TcpEndpoint* const endpoint_impl) {
            if (endpoint_impl->is_alive())
            {
                endpoint_impl->OnEvent(event);

                if (endpoint_impl->is_alive())
                {
                    result = ErrorCode::kSuccess;
                }
            }
        });
    }
    else
    {
        do
        {
            TcpEndpoint* endpoint_impl;
            {
                lock_guard<mutex> _(endpoint_header->lock);
                const EndpointChain& sub_endpoints = endpoint_header->sub_endpoints;
                if (ADK_UNLIKELY(0 == sub_endpoints.size()))
                {
                    break;
                }

                assert(1 == sub_endpoints.size());
                endpoint_impl = sub_endpoints.front();
                endpoint_impl->add_reference();
            }

            if (endpoint_impl->is_alive())
            {
                endpoint_impl->OnEvent(event);

                if (endpoint_impl->is_alive())
                {
                    result = ErrorCode::kSuccess;
                }
            }

            endpoint_impl->sub_reference();
        } while (false);
    }

    return result;
}

void TcpEndpoint::DeliverErrorEvent(EndpointHeader* const endpoint_header, Event* const event)
{
    TxMessageQueue* const tx_message_queue = endpoint_header->tx_message_queue;
    assert(tx_message_queue);

    tx_message_queue->set_release_alert();
    if (endpoint_header->is_singleton)
    {
        AddenCallback(endpoint_header, [=](TcpEndpoint* const endpoint_impl) {
            if (endpoint_impl->invalidate())
            {
                endpoint_impl->OnEvent(event);
            }
        });
    }
    else
    {
        do 
        {
            TcpEndpoint* endpoint_impl;
            {
                lock_guard<mutex> _(endpoint_header->lock);
                const EndpointChain& sub_endpoints = endpoint_header->sub_endpoints;
                if (ADK_UNLIKELY(0 == sub_endpoints.size()))
                {
                    break;
                }

                assert(1 == sub_endpoints.size());
                endpoint_impl = sub_endpoints.front();
                endpoint_impl->add_reference();
            }

            if (endpoint_impl->invalidate())
            {
                endpoint_impl->OnEvent(event);
            }

            endpoint_impl->sub_reference();
        } while (false);
    }
}

void TcpEndpoint::SendHeartbeatMsg(EndpointHeader* const endpoint_header)
{
    HeartbeatHandler* const heartbeat_handler = endpoint_header->heartbeat_handler;
    assert(heartbeat_handler);

    uint64_t& last_heartbeat_tp = endpoint_header->phy_time;
    const uint64_t interval_us = (uint64_t)(heartbeat_handler->GetPeriodMilli()) * 1000;
    const uint64_t current_time = endpoint_header->GetTimepoint();
    if (last_heartbeat_tp + interval_us > current_time)
    {
        return;
    }

    TcpEndpoint* endpoint_impl = nullptr;
    if (endpoint_header->is_singleton)
    {
        lock_guard<mutex> _(endpoint_header->lock);
        for (auto& sub_endpoint : endpoint_header->sub_endpoints)
        {
            if (sub_endpoint->is_running())
            {
                sub_endpoint->add_reference();
                endpoint_impl = sub_endpoint;
                break;
            }
        }
    }
    else
    {
        // call in io control
        lock_guard<mutex> _(endpoint_header->lock);
        if (0 != endpoint_header->sub_endpoints.size())
        {
            assert(1 == endpoint_header->sub_endpoints.size());
            endpoint_impl = endpoint_header->sub_endpoints.front();
            endpoint_impl->add_reference();
        }
    }

    if (nullptr != endpoint_impl)
    {
        heartbeat_handler->SendHBMsg((Endpoint*)endpoint_impl);
        endpoint_impl->sub_reference();
    }

    last_heartbeat_tp = current_time;
}

int32_t TcpEndpoint::OnAccept(AcceptHandler* const accept_handler)
{
    assert(accept_handler);

    Property props;
    accept_handler->OnAccept((Endpoint*)this, props);

    if (ADK_UNLIKELY(!set_ep_running()))
    {
        return ErrorCode::kFailure;
    }

    assert(endpoint_header_->tcp_endpoint);
    UpdateTcpProps<false>(endpoint_header_->tcp_endpoint, props);

    properties_.OverWriteFrom(props);

    OnInitEnd();

    return ErrorCode::kSuccess;
}

int32_t TcpEndpoint::OnConnect()
{
    if (ADK_UNLIKELY(!set_ep_running()))
    {
        return ErrorCode::kFailure;
    }

    Property props;
    assert(connect_handler_);
    connect_handler_->OnConnect((Endpoint*)this, props);
    if (ADK_UNLIKELY(!is_alive()))
    {
        return ErrorCode::kFailure;
    }

    assert(endpoint_header_->tcp_endpoint);
    UpdateTcpProps<false>(endpoint_header_->tcp_endpoint, props);

    properties_.OverWriteFrom(props);

    OnInitEnd();

    return ErrorCode::kSuccess;
}

int32_t TcpEndpoint::OnConnect(EndpointHeader* const endpoint_header)
{
    assert(endpoint_header);

    int32_t result = ErrorCode::kFailure;
    if (endpoint_header->is_singleton)
    {
        AddenCallback(endpoint_header, [&](TcpEndpoint* const endpoint_impl) {
            if (ErrorCode::kSuccess == endpoint_impl->OnConnect())
            {
                result = ErrorCode::kSuccess;
            }
        });
    }
    else
    {
        const EndpointChain& sub_endpoints = endpoint_header->sub_endpoints;
        if (0 != sub_endpoints.size())
        {
            assert(1 == sub_endpoints.size());

            TcpEndpoint* const endpoint_impl = sub_endpoints.front();
            return endpoint_impl->OnConnect();
        }
    }

    return result;
}

void TcpEndpoint::OnInitEnd()
{
    using namespace config::endpoint;

    // recover the older message_handler
    auto* const message_handler = properties_.GetValue(kMessageHandler, Pointer())
                                            .as_ptr<MessageHandler*>();
    if (nullptr != message_handler)
    {
        endpoint_header_->message_handler = message_handler;
    }

    auto* const decode_template = properties_.GetValue(kDecodeTemplate, Pointer())
                                            .as_ptr<DecodeTemplate*>();
    if (nullptr != decode_template)
    {
        endpoint_header_->decode_template = decode_template;
    }

    // recover the older heartbeat_handler
    auto* const heartbeat_handler = properties_.GetValue(kHeartbeatHandler, Pointer())
                                              .as_ptr<HeartbeatHandler*>();
    if (nullptr != heartbeat_handler)
    {
        endpoint_header_->heartbeat_handler = heartbeat_handler;
    }

    // refresh the event handler
    auto* const event_handler = properties_.GetValue(kEventHandler, Pointer())
                                          .as_ptr<EventHandler*>();
    if (nullptr != event_handler)
    {
        event_handler_ = event_handler;
    }

    // recover the older heartbeat_timeout
    const auto heartbeat_timeout = properties_.GetValue(kHeartbeatTimeoutMilli, kuint64Max);
    if (kuint64Max != heartbeat_timeout)
    {
        endpoint_header_->heartbeat_timeout = heartbeat_timeout * 1000;
    }

    if (nullptr == endpoint_header_->message_handler)
    {
        endpoint_header_->message_handler = tcp_engine_impl()->blank_message_handler();
    }

    if (nullptr == endpoint_header_->heartbeat_handler)
    {
        endpoint_header_->heartbeat_handler = tcp_engine_impl()->blank_heartbeat_handler();
    }

    if (properties_.HasValue(config::endpoint::kTxMinResidentMicro))
    {
        endpoint_header_->set_tx_min_resident(properties_.GetValue(config::endpoint::kTxMinResidentMicro,
                                                                  default_value::kTxMinResidentMicro));
    }
    else
    {
        if (tcp_engine_impl()->is_tx_latency_set())
        {
            if (tcp_engine_impl()->is_tx_low_latency())
            {
                endpoint_header_->set_tx_min_resident(default_value::kTxMinResidentMicroLowLatency);
            }
            else
            {
                endpoint_header_->set_tx_min_resident(default_value::kTxMinResidentMicroNoLowLatency);
            }
        }
    }

    if (properties_.HasValue(config::endpoint::kRxMinResidentMicro))
    {
        endpoint_header_->set_rx_min_resident(properties_.GetValue(config::endpoint::kRxMinResidentMicro, 
                                                                  default_value::kRxMinResidentMicro));
    }
    else
    {
        if (tcp_engine_impl()->is_rx_latency_set())
        {
            if (tcp_engine_impl()->is_rx_low_latency())
            {
                endpoint_header_->set_rx_min_resident(default_value::kRxMinResidentMicroLowLatency);
            }
            else
            {
                endpoint_header_->set_rx_min_resident(default_value::kRxMinResidentMicroNoLowLatency);
            }
        }
    }

    if (nullptr == endpoint_header_->share_ctx)
    {
        endpoint_header_->share_ctx = this;
    }
}

int32_t TcpEndpoint::Reconnect()
{
    if (!properties_.HasValue(config::endpoint::kRemoteIp)
        || !properties_.HasValue(config::endpoint::kRemotePort))
    {
        SetErrorInfo("property of endpoint has no remote address");
        return ErrorCode::kFailure;
    }

    return Reconnect(properties_.GetValue(config::endpoint::kRemoteIp, string()), 
                     properties_.GetValue(config::endpoint::kRemotePort, default_value::kInvalidPort));
}

int32_t TcpEndpoint::Reconnect(const string& remote_ip, uint16_t remote_port)
{
    if (remote_ip.empty() || (default_value::kInvalidPort == remote_port))
    {
        SetErrorInfo("remote address is invalid");
        return ErrorCode::kFailure;
    }

    if (ADK_UNLIKELY(!verify()))
    {
        SetErrorInfo("endpoint is invalid");
        return ErrorCode::kFailure;
    }

    {
        lock_guard<mutex> _(endpoint_header_->lock);
        if (!endpoint_header_->is_phy_release())
        {
            SetErrorInfo("endpoint status invalid");
            return ErrorCode::kFailure;
        }

        add_reference();
    }

    OnExit<> on_exit([this]() {
        sub_reference();
    });

    auto& endpoint_chain = endpoint_header_->sub_endpoints;
    if (endpoint_header_->is_singleton)
    {
        using namespace config::endpoint;
        if ((remote_ip != properties_.GetValue(kRemoteIp, string())) 
            || (remote_port != properties_.GetValue(kRemotePort, default_value::kInvalidPort)))
        {
            SetErrorInfo("singleton endpoint not support to reconnect another destination");
            return ErrorCode::kFailure;
        }

        lock_guard<mutex> _(endpoint_header_->lock);
        if (ADK_UNLIKELY(endpoint_chain.end() == std::find(endpoint_chain.begin(), 
                                                           endpoint_chain.end(), 
                                                           this)))
        {
            SetErrorInfo("endpoint is invalid");
            return ErrorCode::kFailure;
        }
    }
    else
    {
        lock_guard<mutex> _(endpoint_header_->lock);
        if (ADK_UNLIKELY((0 == endpoint_chain.size()) || (this != endpoint_chain.front())))
        {
            SetErrorInfo("endpoint is invalid");
            return ErrorCode::kFailure;
        }
    }

    if (ADK_UNLIKELY(!set_reconnecting()))
    {
        SetErrorInfo("endpoint is closed");
        return ErrorCode::kFailure;
    }

    auto* const tcp_engine_impl = endpoint_header_->tcp_engine_impl;
    assert(tcp_engine_impl);

    auto* const drive_engine = tcp_engine_impl->drive_engine();
    assert(drive_engine);

    endpoint_header_->tx_valid = false;
    endpoint_header_->rx_valid = false;

    do 
    {
        if (!endpoint_header_->is_tx_release())
        {
            drive_engine->UnregTxEndpoint(endpoint_header_);
        }

        if (!endpoint_header_->is_rx_release())
        {
             //如果是CorkSet + kActive状态，直接Uncork，设置为rx_release
            if (endpoint_header_->check_and_set_rx_cork_lck())
            {
                endpoint_header_->set_rx_release();
            }
            else
            {
                 drive_engine->UnregRxEndpoint(endpoint_header_);
            }
        }

        if (endpoint_header_->is_release())
        {
            break;
        }
    } while (is_alive());

    auto* const last_message_queue = endpoint_header_->tx_message_queue;
    assert(last_message_queue);

    if (last_message_queue->release_alert())
    {
        /**
         * resize message queue size, avoid overflow when batch send
         */
        using namespace config::endpoint;
        const auto tx_queue_size = std::max<uint32_t>(
            properties_.GetValue(kTxMessageQueueSize, default_value::kTxMessageQueueSize), kTxBatchSize * 2);

        auto* tx_message_queue = TxMessageQueue::Create("tx_msg_queue", tx_queue_size);
        if (ADK_UNLIKELY(nullptr == tx_message_queue))
        {
            tx_message_queue = TxMessageQueue::Create("tx_msg_queue", default_value::kTxMessageQueueSize);
            if (ADK_UNLIKELY(nullptr == tx_message_queue))
            {
                return ErrorCode::kNoMemory;
            }
        }

        endpoint_header_->tx_message_queue = tx_message_queue;

        ADK_BARRIER();
        Message* message = nullptr;
        while (ErrorCode::kSuccess == last_message_queue->Pop(message))
        {
            auto impl = (MessageImpl*)message;
            if (impl->is_last_reference())
            {
                IoEngineBase::DeleteTxMessage(impl);
            }
        }

        TxMessageQueue::Delete(last_message_queue);
    }

    auto* const tcp_endpoint = endpoint_header_->tcp_endpoint;
    if (nullptr != tcp_endpoint)
    {
        auto* const local_ports_register = tcp_engine_impl->local_ports_register();
        if (nullptr != local_ports_register)
        {
            local_ports_register->FreePort(tcp_endpoint->endpoint_id());
        }

        ITcpEndpoint::Destroy(tcp_endpoint);
    }

    endpoint_header_->tcp_endpoint = CreateITcpEndpoint();
    if (nullptr == endpoint_header_->tcp_endpoint)
    {
        return ErrorCode::kFailure;
    }

    endpoint_header_->tx_status = TxStatus::kInit;
    endpoint_header_->tx_valid = true;
    endpoint_header_->tx_sch_time = 0;
    assert(!endpoint_header_->tx_status_lock);

    endpoint_header_->rx_status = RxStatus::kInit;
    endpoint_header_->rx_valid = true;
    endpoint_header_->rx_sch_time = 0;
    endpoint_header_->rx_cork_stt = EpRxCorkStat::kRxCorkNot;
    endpoint_header_->phy_status = PhyStatus::kConnecting;

    ConnectTask* const reconnect_task = new ConnectTask;
    reconnect_task->task_type = TaskType::kAsynConnect;
    reconnect_task->endpoint_header = endpoint_header_;
    reconnect_task->remote_ip = remote_ip;
    reconnect_task->remote_port = remote_port;
    reconnect_task->retry_times = 0;
    reconnect_task->limit_times = properties_.GetValue(config::endpoint::kRetryConnectTimes,
                                                       default_value::kRetryConnectTimes);
    /**
     * limit_times must larger than zero
     */
    reconnect_task->limit_times = std::max<uint32_t>(reconnect_task->limit_times, 1);

    reconnect_task->interval_us = properties_.GetValue(config::endpoint::kConnectIntervalMilli,
                                                     default_value::kConnectIntervalMilli) * 1000;
    reconnect_task->interval_us = std::max<int64_t>(100 * 1000, reconnect_task->interval_us);

    int32_t result;
    do
    {
        result = drive_engine->AddTaskConnect(reconnect_task);
    } while (ADK_UNLIKELY(ErrorCode::kSuccess != result));

    return ErrorCode::kSuccess;
}

}

}