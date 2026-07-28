#include "drive_engine.h"
#include "endpoint_impl.h"

namespace adk_impl
{

namespace io_engine
{

Endpoint* Message::endpoint()
{
    auto* const message_impl = static_cast<MessageImpl*>(this);
    if (!message_impl->is_direction_tx())
    {
        auto* const endpoint_header = (EndpointHeader*)(message_impl->endpoint_ctx<false>());
        assert(endpoint_header);

        const EndpointChain& sub_endpoints = endpoint_header->sub_endpoints;
        if (endpoint_header->is_singleton)
        {
            lock_guard<mutex> _(endpoint_header->lock);
            for (auto iter = sub_endpoints.begin(); iter != sub_endpoints.end(); ++iter)
            {
                TcpEndpoint* const endpoint_impl = *iter;
                if (endpoint_impl->is_running())
                {
                    return (Endpoint*)endpoint_impl;
                }
            }
        }
        else
        {
            lock_guard<mutex> _(endpoint_header->lock);
            if (1 == sub_endpoints.size())
            {
                TcpEndpoint* const endpoint_impl = sub_endpoints.front();
                if (endpoint_impl->is_running())
                {
                    return (Endpoint*)endpoint_impl;
                }
            }
        }
    }
    else
    {
        return (Endpoint*)message_impl->endpoint_ctx<true>();
    }
    return nullptr;
}

void* Message::endpoint_share_ctx()
{
    auto* const message_impl = static_cast<MessageImpl*>(this);
    if (!message_impl->is_direction_tx())
    {
        return ((EndpointHeader*)(message_impl->endpoint_ctx<false>()))->share_ctx;
    }

    return nullptr;
}

int32_t Message::forward_acquire()
{
    auto* const message_impl = static_cast<MessageImpl*>(this);
    if (!message_impl->is_direction_tx())
    {
        auto* const endpoint_header = (EndpointHeader*)(message_impl->endpoint_ctx<false>());
        assert(endpoint_header);
        assert(message_impl == endpoint_header->deliver_message);

        auto* const actor_arena = IOActor::current_actor_arena();
        assert(actor_arena);

        auto* const rx_message_pool = actor_arena->rx_message_pool;
        assert(rx_message_pool);

        MessageImpl* substitute_message;
        if (nullptr != actor_arena->orig_app_data)
        {
            assert(actor_arena->orig_app_data != app_data_);

            assert(0 == consume_len_);
            assert(capacity_ == data_len_);

            auto* const zc_buffer = app_data_;

            capacity_ = actor_arena->length;
            app_data_ = actor_arena->orig_app_data;
            if (ADK_UNLIKELY(data_len_ > capacity_))
            {
                message_impl->Resize(data_len_);
            }

            assert(app_data_ != zc_buffer);
            memcpy(app_data_, zc_buffer, data_len_);

            substitute_message = rx_message_pool->NewMessage<true>(endpoint_ctx_);
            assert(substitute_message);
        }
        else
        {
            if (actor_arena->length > 0)
            {
                substitute_message = (actor_arena->length <= rx_message_pool->message_capacity())
                                   ? rx_message_pool->NewMessage<true>(endpoint_ctx_)
                                   : RxMessagePool::NewMessage<true>(actor_arena->length, endpoint_ctx_);
                assert(substitute_message);

                substitute_message->set_data_len(actor_arena->length);
                memcpy(substitute_message->app_data_, app_data_ + data_len_, actor_arena->length);
            }
            else
            {
                substitute_message = rx_message_pool->NewMessage<true>(endpoint_ctx_);
                assert(substitute_message);
            }
        }

        substitute_message->data_flag_ = data_flag_;
        endpoint_header->deliver_message = substitute_message;
        return ErrorCode::kSuccess;
    }

    return ErrorCode::kFailure;
}

int32_t Message::set_fanout(uint32_t fanout_nr)
{
    if (static_cast<MessageImpl*>(this)->is_direction_tx())
    {
        static_cast<MessageImpl*>(this)->set_reference(fanout_nr);
        return ErrorCode::kSuccess;
    }

    return ErrorCode::kFailure;
}

int32_t Message::ReplyBlock(Message* msg)
{
    Endpoint* const reply_endpoint = endpoint();
    if (nullptr != reply_endpoint)
    {
        return static_cast<MessageImpl*>(msg)->is_direction_tx() 
                ? reply_endpoint->SendMsg<true>(msg)
                : reply_endpoint->SendMsg<true>(msg->const_data(), msg->data_len());
    }

    return ErrorCode::kFailure;
}

int32_t Message::ReplyUnBlock(Message* msg)
{
    Endpoint* const reply_endpoint = endpoint();
    if (nullptr != reply_endpoint)
    {
        return static_cast<MessageImpl*>(msg)->is_direction_tx()
                ? reply_endpoint->SendMsg<false>(msg)
                : reply_endpoint->SendMsg<false>(msg->const_data(), msg->data_len());
    }

    return ErrorCode::kFailure;
}

int32_t Message::ReplyBlock(const void* buffer, uint32_t len)
{
    Endpoint* const reply_endpoint = endpoint();
    if (nullptr != reply_endpoint)
    {
        return reply_endpoint->SendMsg<true>(buffer, len);
    }

    return ErrorCode::kFailure;
}

int32_t Message::ReplyUnBlock(const void* buffer, uint32_t len)
{
    Endpoint* const reply_endpoint = endpoint();
    if (nullptr != reply_endpoint)
    {
        return reply_endpoint->SendMsg<false>(buffer, len);
    }

    return ErrorCode::kFailure;
}

}

}