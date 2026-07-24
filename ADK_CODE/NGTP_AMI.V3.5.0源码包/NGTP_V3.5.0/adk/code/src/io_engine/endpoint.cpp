#include "event_impl.h"
#include "last_error.h"
#include "message_impl.h"
#include "drive_engine.h"
#include "endpoint_impl.h"
#include "tcp_engine_impl.h"
#include "endpoint_register.h"

#include <adk/io_engine/endpoint.h>
#include <adk/io_engine/property.h>
#include <adk/io_engine/config_key.h>

namespace adk_impl
{

namespace io_engine
{

static inline EndpointHeader* GetEndpointHeader(Endpoint* const endpoint)
{
    return static_cast<TcpEndpoint*>(endpoint)->endpoint_header();
}

static inline const EndpointHeader* GetEndpointHeader(const Endpoint* endpoint)
{
    return ((TcpEndpoint*)endpoint)->endpoint_header();
}

template<bool kIsBlock, bool kIsThreadSafe>
static inline int32_t SendMessage(TcpEndpoint* const endpoint_impl, MessageImpl* const msg)
{
    assert(endpoint_impl);

    auto* const endpoint_header = endpoint_impl->endpoint_header();
    assert(endpoint_header);

    //在零拷贝，单线程发送,fanout >1 情况下，assert 不一定成立
    //assert(msg->capacity());
    assert(msg->data_len());

    TxMessageQueue* const tx_message_queue = endpoint_header->tx_message_queue;
    assert(tx_message_queue);

    msg->set_endpoint_ctx<true>(endpoint_impl);
    if (kIsBlock)
    {
        if (kIsThreadSafe)
        {
            if (ADK_UNLIKELY(ErrorCode::kSuccess != tx_message_queue->Push(msg)))
            {
                return ErrorCode::kFailure;
            }
        }
        else
        {
            while (ADK_UNLIKELY(ErrorCode::kSuccess != ((TxMessageQueueUnsafe*)tx_message_queue)->Push(msg)))
            {
                if (ADK_UNLIKELY(tx_message_queue->release_alert()))
                {
                    return ErrorCode::kFailure;
                }

                ADK_PAUSE();
            }
        }
    }
    else
    {
        if (kIsThreadSafe)
        {
            if (ADK_UNLIKELY(ErrorCode::kSuccess != tx_message_queue->TryPush(msg)))
            {
                if (ADK_UNLIKELY(tx_message_queue->release_alert()))
                {
                    return ErrorCode::kFailure;
                }

                return ErrorCode::kWouldblock;
            }
        }
        else
        {
            if (ADK_UNLIKELY(ErrorCode::kSuccess != ((TxMessageQueueUnsafe*)tx_message_queue)->TryPush(msg)))
            {
                if (ADK_UNLIKELY(tx_message_queue->release_alert()))
                {
                    return ErrorCode::kFailure;
                }

                return ErrorCode::kWouldblock;
            }
        }
    }

    if (ADK_UNLIKELY(TxStatus::kIdle == endpoint_header->tx_status))
    {
        if (endpoint_header->atomic_set_tx_active())
        {
            auto* const send_actor_impl = endpoint_header->send_actor_impl;
            assert(send_actor_impl);

            send_actor_impl->AddJob(endpoint_header);
        }
    }

    return ErrorCode::kSuccess;
}

Message* Endpoint::NewMessage(uint32_t len)
{
    auto* const tcp_engine_impl = static_cast<TcpEndpoint*>(this)->tcp_engine_impl();

    assert(tcp_engine_impl);
    return tcp_engine_impl->NewTxMessage(len);
}

void Endpoint::DeleteMessage(Message* message)
{
    if (((MessageImpl*)message)->is_direction_tx())
    {
        if (((MessageImpl*)message)->is_last_reference())
        {
            IoEngineBase::DeleteTxMessage(((MessageImpl*)message));
        }
    }
    else
    {
        IoEngineBase::DeleteRxMessage(((MessageImpl*)message));
    }
}

int32_t Endpoint::SendMsg(const vector<Endpoint*>& endpoint_vec, Message* const msg)
{
    int32_t result = ErrorCode::kSuccess;
    ((MessageImpl*)msg)->set_reference(endpoint_vec.size());

    for (auto& endpoint : endpoint_vec)
    {
        const auto ec = static_cast<TcpEndpoint*>(endpoint)->SendMsg(msg);
        if (ADK_UNLIKELY(ErrorCode::kSuccess != ec))
        {
            result = ec;
        }
    }

    return result;
}

int32_t Endpoint::GetPendingMsg(const std::function<int32_t(Message*)>& msg_handler)
{
    EndpointHeader* const endpoint_header = GetEndpointHeader(this);

    assert(endpoint_header);
    TxMessageQueue* const tx_message_queue = endpoint_header->tx_message_queue;

    const uint64_t length = tx_message_queue->length();
    for (uint64_t index = 0; index < length; ++index)
    {
        Message** const message_pptr = tx_message_queue->UnsafeAt(index);
        assert(message_pptr);

        auto* const message_impl = (MessageImpl*)(*message_pptr);
        assert(message_impl);

        if (this == message_impl->endpoint_ctx<true>())
        {
            const auto ec = msg_handler((Message*)message_impl);
            if (ADK_UNLIKELY(ErrorCode::kSuccess != ec))
            {
                return ec;
            }
        }
    }
    return ErrorCode::kSuccess;
}

void Endpoint::Close(int32_t mode)
{
    TcpEndpoint* const endpoint_impl = static_cast<TcpEndpoint*>(this);
    if (ADK_UNLIKELY(ErrorCode::kSuccess != EndpointRegister::VerifyEndpoint(endpoint_impl)))
    {
        return;
    }

    if (endpoint_impl->set_to_close())
    {
        TcpEngineImpl* const tcp_engine_impl = endpoint_impl->tcp_engine_impl();
        assert(tcp_engine_impl);

        auto* const endpoint_header = endpoint_impl->endpoint_header();
        assert(endpoint_header);

        if (endpoint_header->is_singleton)
        {
            bool uninstall_handler = true;
            lock_guard<mutex> _(endpoint_header->lock);
            EndpointChain& sub_endpoints = endpoint_header->sub_endpoints;
            
            /**
             * check sub endpoint is active, can not remove inactive sub-endpoint here
             */
            for (auto iter = sub_endpoints.begin(); iter != sub_endpoints.end(); ++iter)
            {
                auto* const endpoint_sub = (TcpEndpoint*)(*iter);
                if (endpoint_impl == endpoint_sub)
                {
                    continue;
                }

                if (endpoint_sub->is_alive())
                {
                    uninstall_handler = false;
                    break;
                }
            }

            /**
             * all sub-endpoints are inactive, uninstall handler
             *
             * uninstall handler protected by endpoint_header->lock
             */
            if (uninstall_handler)
            {
                tcp_engine_impl->UninstallHandler(endpoint_header);
            }
        }
        else
        {
            lock_guard<mutex> _(endpoint_header->lock);
            assert(1 == endpoint_header->sub_endpoints.size());
            assert(endpoint_impl == endpoint_header->sub_endpoints.front());

            tcp_engine_impl->UninstallHandler(endpoint_header);
        }

        if (1 == mode)
        {
            endpoint_impl->event_handler_ = tcp_engine_impl->blank_event_handler();
        }

        tcp_engine_impl->ToCloseEndpoint(endpoint_impl);
    }
}

bool Endpoint::IsReady() const
{
    const TcpEndpoint* endpoint_impl = static_cast<const TcpEndpoint*>(this);
    return endpoint_impl->verify() && endpoint_impl->is_running();
}

void Endpoint::Shutdown()
{
    EndpointHeader* const endpoint_header = GetEndpointHeader(this);
    assert(endpoint_header);

    if (static_cast<TcpEndpoint*>(this)->invalidate())
    {
        auto* const tcp_engine_impl = endpoint_header->tcp_engine_impl;
        assert(tcp_engine_impl);

        tcp_engine_impl->UninstallHandler(endpoint_header);
    }
}

int32_t Endpoint::endpoint_id() const
{
    const auto* endpoint_header = GetEndpointHeader(this);
    assert(endpoint_header);

    const auto* tcp_endpoint = endpoint_header->tcp_endpoint;
    assert(tcp_endpoint);

    return tcp_endpoint->endpoint_id();
}

uint32_t Endpoint::sub_index() const
{
    return static_cast<const TcpEndpoint*>(this)->sub_index_;
}

bool Endpoint::is_singleton() const
{
    const EndpointHeader* endpoint_header = GetEndpointHeader(this);

    assert(endpoint_header);
    return endpoint_header->is_singleton;
}

void Endpoint::set_private_ctx(void* const private_ctx)
{
    static_cast<TcpEndpoint*>(this)->private_ctx_ = private_ctx;
}

void* Endpoint::private_ctx() const
{
    return static_cast<const TcpEndpoint*>(this)->private_ctx_;
}

void Endpoint::set_share_ctx(void* const share_ctx)
{
    EndpointHeader* const endpoint_header = GetEndpointHeader(this);

    assert(endpoint_header);
    endpoint_header->share_ctx = share_ctx;
}

int32_t Endpoint::SendMsgBlock(Message* const msg)
{
    return SendMessage<true, true>(static_cast<TcpEndpoint*>(this), ((MessageImpl*)msg));
}

int32_t Endpoint::SendMsgUnblock(Message* const msg)
{
    return SendMessage<false, true>(static_cast<TcpEndpoint*>(this), ((MessageImpl*)msg));
}

int32_t Endpoint::SendMsgBlockUnsafe(Message* const msg)
{
    return SendMessage<true, false>(static_cast<TcpEndpoint*>(this), ((MessageImpl*)msg));
}

int32_t Endpoint::SendMsgUnblockUnsafe(Message* const msg)
{
    return SendMessage<false, false>(static_cast<TcpEndpoint*>(this), ((MessageImpl*)msg));
}

ssize_t Endpoint::Recv(char *buf, size_t len)
{
    EndpointHeader* const endpoint_header = GetEndpointHeader(this);
    assert(endpoint_header);

    if (ADK_UNLIKELY((nullptr != endpoint_header->message_handler) 
        || (endpoint_header->is_rx_release())))
    {
        return -1;
    }

    ITcpEndpoint* const tcp_endpoint = endpoint_header->tcp_endpoint;
    assert(tcp_endpoint);

    const auto result = tcp_endpoint->Recv(buf, len);
    if (result > 0)
    {
        endpoint_header->rx_message_bytes += result;
    }
    else if (ADK_UNLIKELY(0 == result))
    {
        EventEndOfStream event_error;
        TcpEndpoint::DeliverErrorEvent(endpoint_header, &event_error);
    }
    else if (ADK_UNLIKELY(EAGAIN != errno))
    {
        EventSocketError event_error("AppRecv", errno);
        TcpEndpoint::DeliverErrorEvent(endpoint_header, &event_error);
    }

    return result;
}

void Endpoint::UpdateProperty(const Property& props)
{
    using namespace config::endpoint;

    EndpointHeader* const endpoint_header = GetEndpointHeader(this);
    assert(endpoint_header);

    EventHandler* const event_handler = props.GetValue(kEventHandler, Pointer())
                                             .as_ptr<EventHandler*>();
    if (nullptr != event_handler)
    {
        static_cast<TcpEndpoint*>(this)->set_event_handler(event_handler);
    }

    DecodeTemplate* const decode_template = props.GetValue(kDecodeTemplate, Pointer())
                                                 .as_ptr<DecodeTemplate*>();
    if (nullptr != decode_template)
    {
        endpoint_header->decode_template = decode_template;
    }

    MessageHandler* const message_handler = props.GetValue(kMessageHandler, Pointer())
                                                 .as_ptr<MessageHandler*>();
    if (nullptr != message_handler)
    {
        endpoint_header->message_handler = message_handler;
    }

    HeartbeatHandler* const heartbeat_handler = props.GetValue(kHeartbeatHandler, Pointer())
                                                     .as_ptr<HeartbeatHandler*>();
    if (nullptr != heartbeat_handler)
    {
        endpoint_header->heartbeat_handler = heartbeat_handler;
    }

    const auto heartbeat_timeout = props.GetValue(kHeartbeatTimeoutMilli, (uint64_t)0);
    if (0 != heartbeat_timeout)
    {
        endpoint_header->heartbeat_timeout = heartbeat_timeout * 1000;
    }
    else
    {
        endpoint_header->heartbeat_timeout = kuint64Max;
    }

    if (props.HasValue(kTxMinResidentMicro))
    {
        endpoint_header->set_tx_min_resident(props.GetValue(kTxMinResidentMicro, 
                                                            default_value::kTxMinResidentMicro));
    }

    if (props.HasValue(kRxMinResidentMicro))
    {
        endpoint_header->set_rx_min_resident(props.GetValue(kRxMinResidentMicro, 
                                                            default_value::kRxMinResidentMicro));
    }

    static_cast<TcpEndpoint*>(this)->UpdateTcpProps(props);
}

void* Endpoint::share_ctx() const
{
    const EndpointHeader* endpoint_header = GetEndpointHeader(this);

    assert(endpoint_header);
    return endpoint_header->share_ctx;
}

const string& Endpoint::remote_ip() const
{
    const EndpointHeader* endpoint_header = GetEndpointHeader(this);

    assert(endpoint_header);
    return endpoint_header->remote_ip();
}

const uint16_t Endpoint::remote_port() const
{
    const EndpointHeader* endpoint_header = GetEndpointHeader(this);

    assert(endpoint_header);
    return endpoint_header->remote_port();
}

const string& Endpoint::local_ip() const
{
    const EndpointHeader* endpoint_header = GetEndpointHeader(this);

    assert(endpoint_header);
    return endpoint_header->local_ip();
}

const uint16_t Endpoint::local_port() const
{
    const EndpointHeader* endpoint_header = GetEndpointHeader(this);

    assert(endpoint_header);
    return endpoint_header->local_port();
}

bool Endpoint::SetRxCork() 
{
    EndpointHeader* endpoint_header = GetEndpointHeader(this);
    return endpoint_header->SetRxCorkPre();
}

bool Endpoint::SetRxUncork()
{
    EndpointHeader* endpoint_header = GetEndpointHeader(this);
    return endpoint_header->SetRxUncork();
}

EndpointHandler::EndpointHandler(Endpoint* endpoint)
{
    assert(endpoint);
    endpoint_ = endpoint;

    ref_counter_ = new uint32_t;
    *ref_counter_ = 1;
}

EndpointHandler::EndpointHandler(EndpointHandler&& endpoint_handler)
{
    endpoint_ = endpoint_handler.endpoint_;
    ref_counter_ = endpoint_handler.ref_counter_;
    endpoint_handler.ref_counter_ = nullptr;
}

EndpointHandler::EndpointHandler(const EndpointHandler& endpoint_handler)
{
    assert(endpoint_handler.ref_counter_);
    __sync_fetch_and_add(endpoint_handler.ref_counter_, 1);
    endpoint_ = endpoint_handler.endpoint_;
    ref_counter_ = endpoint_handler.ref_counter_;
}

EndpointHandler::~EndpointHandler()
{
    if (nullptr != ref_counter_)
    {
        if (1 == __sync_fetch_and_sub(ref_counter_, 1))
        {
            assert(endpoint_);
            endpoint_->Close();
            delete ref_counter_;
        }
    }
}

EndpointHandler& EndpointHandler::operator = (EndpointHandler&& endpoint_handler)
{
    assert(ref_counter_);
    if (1 == __sync_fetch_and_sub(ref_counter_, 1))
    {
        assert(endpoint_);
        endpoint_->Close();
        delete ref_counter_;
    }

    endpoint_ = endpoint_handler.endpoint_;
    ref_counter_ = endpoint_handler.ref_counter_;
    endpoint_handler.ref_counter_ = nullptr;
    return *this;
}

EndpointHandler& EndpointHandler::operator = (const EndpointHandler& endpoint_handler)
{
    assert(endpoint_handler.ref_counter_);
    __sync_fetch_and_add(endpoint_handler.ref_counter_, 1);

    assert(ref_counter_);
    if (1 == __sync_fetch_and_sub(ref_counter_, 1))
    {
        assert(endpoint_);
        endpoint_->Close();
        delete ref_counter_;
    }

    endpoint_ = endpoint_handler.endpoint_;
    ref_counter_ = endpoint_handler.ref_counter_;
    return *this;
}

EndpointHandler& EndpointHandler::operator = (Endpoint* endpoint)
{
    assert(ref_counter_);
    if (1 == __sync_fetch_and_sub(ref_counter_, 1))
    {
        assert(endpoint_);
        endpoint_->Close();
    }
    else
    {
        ref_counter_ = new uint32_t;
    }

    *ref_counter_ = 1;
    endpoint_ = endpoint;
    return *this;
}

bool EndpointHandler::operator == (const EndpointHandler& endpoint_handler) const 
{
    return endpoint_ == endpoint_handler.endpoint_;
}

bool EndpointHandler::operator != (const EndpointHandler& endpoint_handler) const
{
    return endpoint_ != endpoint_handler.endpoint_;
}

bool EndpointHandler::operator == (Endpoint* endpoint) const
{
    return endpoint_ == endpoint;
}

bool EndpointHandler::operator != (Endpoint* endpoint) const
{
    return endpoint_ != endpoint;
}

}

}