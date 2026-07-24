#include "io_thread.h"
#include "message_impl.h"
#include "drive_engine.h"
#include "local_storage_queue.h"
#include "tcp_verbs/tcp_socket.h"

#if defined(__x86_64__)
#include "tcp_verbs/tcp_direct_zf.h"
#elif defined(__aarch64__)
#include "tcp_verbs/tcp_direct_zf_arm.h"
#endif

#include <sched.h>

#include <cstdlib>

#include <adk/entry_wrapper.h>
#include <adk/io_engine/property.h>
#include <adk/io_engine/config_key.h>

// #define _IO_ENGINE_PERF_TEST_

#ifdef _IO_ENGINE_PERF_TEST_
#include <iomanip>
#include <boost/date_time/posix_time/posix_time.hpp>

class StatsInfo
{
public:
    inline void OnNewInfo(uint64_t time_diff)
    {
        if (ADK_UNLIKELY(reset_))
        {
            Reset();
        }

        ++counter_;
        grand_total_ += time_diff;
        min_ = std::min<uint64_t>(min_, time_diff);
        max_ = std::max<uint64_t>(max_, time_diff);
    }

    StatsInfo(const std::string& stats_name)
    {
        stats_name_ = stats_name;

        Reset();
        new std::thread([&]() {
            do
            {
                sleep(1);
                StatsPrint();
            } while (true);
        });
    }

private:
    void Reset()
    {
        reset_ = false;
        counter_ = 0;
        grand_total_ = 0;
        min_ = 0xfffffffful;
        max_ = 0;
    }

    void StatsPrint()
    {
        std::lock_guard<std::mutex> _(stats_lock_);

        const auto counter = ACCESS_ONCE(counter_);
        if (0 != counter)
        {
            const auto avg = (double)ACCESS_ONCE(grand_total_) / (double)(counter_ * 1000);

            std::cout.precision(3);
            std::cout << boost::posix_time::second_clock::local_time()
                << " | " << stats_name_ << " | nr:" << std::setw(8) << counter
                << " | avg(us):" << std::setw(8) << std::fixed << avg
                << " | min(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(min_) / (double)1000
                << " | max(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(max_) / (double)1000
                << std::endl;
        }
        else
        {
            std::cout << boost::posix_time::second_clock::local_time()
                << " | " << stats_name_ << " | nr:" << std::setw(8) << counter
                << " | rr_avg(us):" << std::setw(8) << std::fixed << "N/A"
                << " | rr_min(us):" << std::setw(8) << std::fixed << "N/A"
                << " | rr_max(us):" << std::setw(8) << std::fixed << "N/A"
                << std::endl;
        }

        reset_ = true;
    }

    std::string   stats_name_;

    volatile bool reset_;
    uint32_t      counter_;
    uint64_t      grand_total_;
    uint64_t      min_;
    uint64_t      max_;

    static std::mutex stats_lock_;
};

std::mutex StatsInfo::stats_lock_;

#endif

namespace adk_impl
{

namespace io_engine
{

RecvActorArena::RecvActorArena(RxMessagePool* message_pool)
{
    delivering = false;
    is_running = true;
    length = 0;
    orig_app_data = nullptr;
    actor_counter = 0;
    actor_rcu_counter = 0;
    rx_message_pool = message_pool;
}

enum class IoResult
{
    kDrop = 0,
    kActive0,
    kActive1,
};

#ifndef PUSH_ACTIVE_QUEUE
#define PUSH_ACTIVE_QUEUE(active_queue, active0_queue, endpoint_header)             \
    if (ADK_UNLIKELY(ErrorCode::kSuccess != active_queue.TryPush(endpoint_header))) \
    {                                                                               \
        ADK_ASSERT_SUCCESS(active0_queue->Push(endpoint_header));                   \
    }
#endif

#ifndef PUSH_TX_ACTIVE_QUEUE
#define PUSH_TX_ACTIVE_QUEUE(active_queue, active0_queue, endpoint_header)          \
    if (ADK_UNLIKELY(ErrorCode::kSuccess != active_queue.TryPush(endpoint_header))) \
    {                                                                               \
        ADK_ASSERT_SUCCESS(active0_queue->Push(endpoint_header));                   \
        endpoint_header->tx_sch_time = endpoint_header->GetTimepoint();             \
    }
#endif

#ifndef PUSH_RX_ACTIVE_QUEUE
#define PUSH_RX_ACTIVE_QUEUE(active_queue, active0_queue, endpoint_header)          \
    if (ADK_UNLIKELY(ErrorCode::kSuccess != active_queue.TryPush(endpoint_header))) \
    {                                                                               \
        ADK_ASSERT_SUCCESS(active0_queue->Push(endpoint_header));                   \
        endpoint_header->rx_sch_time = endpoint_header->GetTimepoint();             \
    }
#endif

ADK_ALWAYS_INLINE void RecycleTxMessage(TxMessageQueue* const tx_message_queue,
                                        Message** messages,
                                        int32_t message_size)
{
    for (int32_t index = 0; index < message_size; ++index)
    {
        tx_message_queue->UnsafeDrop();

        MessageImpl* const message_impl = (MessageImpl*)(messages[index]);
        if (message_impl->is_last_reference())
        {
            IoEngineBase::DeleteTxMessage(message_impl);
        }
    }
}

ADK_ALWAYS_INLINE void RecycleTxMessage(TxMessageQueue* const tx_message_queue,
                                        MessageImpl* message_impl)
{
    tx_message_queue->UnsafeDrop();
    if (message_impl->is_last_reference())
    {
        IoEngineBase::DeleteTxMessage(message_impl);
    }
}

template<typename EndpointType, bool kPreSendEnable, bool kInMultiThread>
ADK_ALWAYS_INLINE IoResult SendMessage(DriveEngine* drive_engine, 
                                       EndpointHeader* endpoint_header)
{
    assert(endpoint_header);

    Message* messages[kTxBatchSize];
    struct iovec iov_msgs[kTxBatchSize];

    TxMessageQueue* const tx_message_queue = endpoint_header->tx_message_queue;
    assert(tx_message_queue);

    auto* const tcp_engine_impl = endpoint_header->tcp_engine_impl;
    assert(tcp_engine_impl);
    auto &tx_consume_flag = endpoint_header->tx_consume_flag;

    PreSendHandler* pre_send_handler;
    if (kPreSendEnable)
    {
        pre_send_handler = tcp_engine_impl->pre_send_handler();
        assert(pre_send_handler);
    }

    int32_t message_index = 0;
    int32_t batch_msg_len = 0;
    do
    {
        Message** const message_pptr = tx_message_queue->ElementAt(message_index);
        if (ADK_UNLIKELY(nullptr == message_pptr))
        {
            break;
        }

        auto message = (MessageImpl*)(*message_pptr);
        if (kPreSendEnable && (0 == message->consume_len()) && (!message->is_tx_called()))
        {
            // auto* const endpoint = (Endpoint*)(message->endpoint_ctx<true>());
            int32_t pretx_result = 0;
            const auto fanout = message->get_fanout();
            if (kInMultiThread && fanout > 1)
            {
                auto clone = message->TxMessageClone(tcp_engine_impl);
                assert(clone);

                *message_pptr = clone;

                if (message->is_last_reference())
                {
                    IoEngineBase::DeleteTxMessage(message);
                }
                //TODO 这个为什么不行？
                //RecycleTxMessage(tx_message_queue, messages);
                pretx_result = pre_send_handler->OnTxMessageBefore(endpoint_header->share_ctx, clone);
            }
            else
            {
                pretx_result = pre_send_handler->OnTxMessageBefore(endpoint_header->share_ctx, message);
            }
            

            message = reinterpret_cast<MessageImpl*>(*message_pptr);
            if (PreSendHandler::PreTxResult::kCallOnce == pretx_result)
            {
                message->set_tx_callonce();
            }
            else if (PreSendHandler::PreTxResult::kSuccess != pretx_result)
            {
                message->set_consume_len(message->data_len());
            }
        }

        const auto msg_len = message->data_len();

        struct iovec& tx_iov = iov_msgs[message_index];
        tx_iov.iov_base = message->data();
        tx_iov.iov_len = msg_len;

        batch_msg_len += msg_len;
        messages[message_index] = (Message*)message;
    } while (++message_index < kTxBatchSize);

    if (0 == message_index)
    {
        return IoResult::kActive1;
    }

    ///> all message droped by PreSendHandler::OnTxMessageBefore
    if (kPreSendEnable && ADK_UNLIKELY(0 == batch_msg_len))
    {
        RecycleTxMessage(tx_message_queue, messages, message_index);
        return IoResult::kActive0;
    }

    assert(batch_msg_len > 0);

    if (ADK_UNLIKELY(0 != tx_consume_flag))
    {
        struct iovec& tx_iov = iov_msgs[0];
        auto &iov_len = tx_iov.iov_len;
        if (iov_len > tx_consume_flag)
        {
            assert(batch_msg_len > tx_consume_flag);
            batch_msg_len -= tx_consume_flag;
            tx_iov.iov_base = (char*)(tx_iov.iov_base) + tx_consume_flag;
            iov_len -= tx_consume_flag;
        }
    }

    auto* const tcp_endpoint = (EndpointType*)(endpoint_header->tcp_endpoint);
    assert(tcp_endpoint);

    const auto result = tcp_endpoint->Send(iov_msgs, message_index);
    if (kPreSendEnable)
    {
        pre_send_handler->OnTxMessageAfter(endpoint_header->share_ctx, result);
    }

    if (ADK_UNLIKELY(result < 0))
    {
        if (EAGAIN == errno)
        {
            switch (EndpointType::drive_mode())
            {
            case ITcpStack::DriveMode::kPoller:
                ///> epoll del after dispatch endpoint, so may be epoll add failed
                if (ADK_UNLIKELY(ErrorCode::kSuccess != drive_engine->OnTxEndpointBlock<typename EndpointType::EPollerType>(endpoint_header)))
                {
                    return IoResult::kActive0;
                }
                break;
            case ITcpStack::DriveMode::kReactor:
                return IoResult::kActive0;
            default:
                assert(false);
            }
        }
        else
        {
            ///> socket error drop the endpoint / rx actor call event handler
            drive_engine->DropTxEndpoint(endpoint_header);
        }

        return IoResult::kDrop;
    }

    endpoint_header->tx_message_bytes += result;
    if (result == batch_msg_len)
    {
        tx_consume_flag = 0;
        RecycleTxMessage(tx_message_queue, messages, message_index);
    }
    else
    {
        assert(result < batch_msg_len);

        auto send_length = result;
        for (int32_t index = 0; index < message_index; ++index)
        {
            MessageImpl* const message_impl = (MessageImpl*)(messages[index]);

            const auto iov_len = iov_msgs[index].iov_len;
            if (iov_len < send_length)
            {
                send_length -= iov_len;
                RecycleTxMessage(tx_message_queue, message_impl);
            }
            else if (iov_len > send_length)
            {
                // message_impl->adden_consume_len(send_length);
                if (0 == index)
                {
                    tx_consume_flag += send_length;
                }
                else
                {
                    tx_consume_flag = send_length;
                }
                break;
            }
            else
            {
                tx_consume_flag = 0;
                RecycleTxMessage(tx_message_queue, message_impl);
                break;
            }
        }
    }

    return IoResult::kActive0;
}

ADK_ALWAYS_INLINE void DoDeliverMessage(EndpointHeader* endpoint_header)
{
    auto* const message_handler = endpoint_header->message_handler;
    assert(message_handler);

do_deliver:
    auto* const deliver_message = endpoint_header->deliver_message;
    assert(deliver_message);
    assert(endpoint_header == deliver_message->endpoint_ctx<false>());

    const auto deliver_len = (int32_t)deliver_message->data_len();

    ++endpoint_header->deliver_message_nr;
    const auto result = message_handler->OnMessage(deliver_message);
    ++endpoint_header->deliver_message_nr;

    auto* const deliver_message_after = endpoint_header->deliver_message;
    if (MessageHandler::Result::kFollowUp == result)
    {
        assert(deliver_message == deliver_message_after);

        const auto left_len = (int32_t)deliver_message_after->data_len();
        if ((deliver_len > left_len)
            && (left_len >= deliver_message_after->data_more()))
        {
            if (ADK_UNLIKELY(endpoint_header->rx_cork_stat() == EpRxCorkStat::kRxCorkPre))
            {
                return ;
            }

            goto do_deliver;
        }
    }
    else
    {
        deliver_message_after->FreeBuffer();
        deliver_message_after->set_data_more(0);
    }
}

ADK_ALWAYS_INLINE void DoDeliverMessage(EndpointHeader* endpoint_header, 
                                        DecodeTemplate* decode_template)
{
    auto* const message_handler = endpoint_header->message_handler;
    assert(message_handler);

do_deliver:
    auto* const deliver_message = endpoint_header->deliver_message;
    assert(deliver_message);
    assert(endpoint_header == deliver_message->endpoint_ctx<false>());

    const auto effective_data_len = (int32_t)deliver_message->data_len();
    const auto message_len = decode_template->MessageLength(deliver_message->const_data(),
                                                            effective_data_len);
    if (message_len > 0)
    {
        if (message_len == effective_data_len)
        {
            ++endpoint_header->deliver_message_nr;
            message_handler->OnMessage(deliver_message);
            ++endpoint_header->deliver_message_nr;

            auto* const deliver_message_after = endpoint_header->deliver_message;
            deliver_message_after->FreeBuffer();
            deliver_message_after->set_data_more(0);
        }
        else if (message_len < effective_data_len)
        {
            auto* const actor_arena = IOActor::current_actor_arena();
            assert(actor_arena);

            const auto tail_len = effective_data_len - message_len;
            actor_arena->length = tail_len;

            deliver_message->set_data_len_impl(message_len);

            ++endpoint_header->deliver_message_nr;
            message_handler->OnMessage(deliver_message);
            ++endpoint_header->deliver_message_nr;

            actor_arena->length = 0;
            if (deliver_message == endpoint_header->deliver_message)
            {
                deliver_message->revert_tail_len(tail_len);
            }

            if (ADK_UNLIKELY(endpoint_header->rx_cork_stat() == EpRxCorkStat::kRxCorkPre))
            {
                return ;
            }
            goto do_deliver;
        }
        else
        {
            deliver_message->set_data_more(message_len - effective_data_len);
        }
    }
    else if (0 == message_len)
    {
        deliver_message->FreeBuffer();
        deliver_message->set_data_more(0);
    }
}

ADK_ALWAYS_INLINE void DeliverMessage(EndpointHeader* endpoint_header)
{
    assert(endpoint_header);
    if (nullptr == endpoint_header->decode_template)
    {
        DoDeliverMessage(endpoint_header);
    }
    else
    {
        DoDeliverMessage(endpoint_header, endpoint_header->decode_template);
    }
}

ADK_ALWAYS_INLINE void DeliverZcMessage(EndpointHeader* const endpoint_header, 
                                        char* buffer, 
                                        size_t len)
{
    auto* const deliver_message = endpoint_header->deliver_message;
    assert(deliver_message);

    if (ADK_UNLIKELY(deliver_message->data_len() > 0))
    {
        deliver_message->AppendBuffer(buffer, len);
        DeliverMessage(endpoint_header);
        return;
    }

    assert(0 == deliver_message->consume_len());
    assert(endpoint_header == deliver_message->endpoint_ctx<false>());

    auto* const message_handler = endpoint_header->message_handler;
    assert(message_handler);

    auto* const actor_arena = IOActor::current_actor_arena();
    assert(actor_arena);

    auto* const decode_template = endpoint_header->decode_template;
    if (nullptr == decode_template)
    {
        actor_arena->push_message(deliver_message);
        deliver_message->set_app_data(buffer);
        deliver_message->set_data_len(len);
        deliver_message->set_capacity(len);

        ++endpoint_header->deliver_message_nr;
        const auto result = message_handler->OnMessage(deliver_message);
        ++endpoint_header->deliver_message_nr;

        auto* const deliver_message_after = endpoint_header->deliver_message;
        if (deliver_message == deliver_message_after)
        {
            actor_arena->pop_message(deliver_message);
        }

        if (MessageHandler::Result::kFollowUp == result)
        {
            const auto consume_len = deliver_message_after->consume_len();
            assert(consume_len <= len);

            deliver_message_after->FreeBuffer();
            if (len > consume_len)
            {
                actor_arena->reset();
                deliver_message->AppendBuffer(buffer + consume_len, len - consume_len);
                DoDeliverMessage(endpoint_header);
            }
        }
        else
        {
            deliver_message_after->FreeBuffer();
            deliver_message_after->set_data_more(0);
        }
    }
    else
    {
        const auto message_len = decode_template->MessageLength(buffer, len);
        if (message_len > 0)
        {
            if (len == (size_t)message_len)
            {
                actor_arena->push_message(deliver_message);
                deliver_message->set_app_data(buffer);
                deliver_message->set_data_len(message_len);
                deliver_message->set_capacity(message_len);

                ++endpoint_header->deliver_message_nr;
                message_handler->OnMessage(deliver_message);
                ++endpoint_header->deliver_message_nr;

                auto* const deliver_message_after = endpoint_header->deliver_message;
                if (deliver_message == deliver_message_after)
                {
                    actor_arena->pop_message(deliver_message);
                    deliver_message->FreeBuffer();
                }

                deliver_message_after->set_data_more(0);
            }
            else if ((size_t)message_len < len)
            {
                actor_arena->push_message(deliver_message);
                deliver_message->set_app_data(buffer);
                deliver_message->set_data_len(message_len);
                deliver_message->set_capacity(message_len);

                ++endpoint_header->deliver_message_nr;
                message_handler->OnMessage(deliver_message);
                ++endpoint_header->deliver_message_nr;

                auto* const deliver_message_after = endpoint_header->deliver_message;
                if (deliver_message == deliver_message_after)
                {
                    actor_arena->pop_message(deliver_message);
                    deliver_message->FreeBuffer();
                }

                actor_arena->reset();
                deliver_message_after->AppendBuffer(buffer + message_len, len - message_len);
                DoDeliverMessage(endpoint_header, decode_template);
            }
            else
            {
                deliver_message->AppendBuffer(buffer, len);
                deliver_message->set_data_more(message_len - len);
            }
        }
        else if (message_len < 0)
        {
            deliver_message->AppendBuffer(buffer, len);
            deliver_message->set_data_more(0);
        }
    }
}

template<typename EndpointType, bool kPreRecvEnable>
ADK_ALWAYS_INLINE IoResult RecvAndDeliverMessage(DriveEngine* drive_engine,
                                                 EndpointHeader* endpoint_header)
{
    assert(drive_engine);
    assert(endpoint_header);

    auto* const tcp_endpoint = (EndpointType*)(endpoint_header->tcp_endpoint);
    assert(tcp_endpoint);

    auto* const deliver_message = endpoint_header->deliver_message;
    assert(deliver_message);

    int32_t result;
    if (EndpointType::kZcRecvSupport)
    {
        if (kPreRecvEnable)
        {
            auto* const tcp_engine_impl = endpoint_header->tcp_engine_impl;
            assert(tcp_engine_impl);

            auto* const pre_recv_handler = tcp_engine_impl->pre_recv_handler();
            assert(pre_recv_handler);

            const auto rx_message_bytes = endpoint_header->rx_message_bytes;
            pre_recv_handler->OnRxMessageBefore(endpoint_header->share_ctx);
            result = tcp_endpoint->ZcRecv([&](char* buffer, ssize_t len) {
                endpoint_header->rx_message_bytes += len;

                if (len >= deliver_message->data_more())
                {
                    IOActor::message_deliver_begin(endpoint_header);
                    DeliverZcMessage(endpoint_header, buffer, len);
                    IOActor::message_deliver_end(endpoint_header);
                }
                else
                {
                    deliver_message->AppendBuffer(buffer, len);
                    deliver_message->dec_data_more(len);
                }
            });

            if (result > 0)
            {
                pre_recv_handler->OnRxMessageAfter(endpoint_header->share_ctx, 
                                                   endpoint_header->rx_message_bytes - rx_message_bytes);

                if (ADK_UNLIKELY(endpoint_header->set_rx_cork_from_pre()))
                {  
                    return IoResult::kDrop;                       
                }
                else
                {
                   return  IoResult::kActive0;
                }
            }
            else
            {
                pre_recv_handler->OnRxMessageAfter(endpoint_header->share_ctx, result);
            }
        }
        else
        {
            result = tcp_endpoint->ZcRecv([&](void* buffer, ssize_t len) {
                endpoint_header->rx_message_bytes += len;
                if (len >= deliver_message->data_more())
                {
                    IOActor::message_deliver_begin(endpoint_header);
                    DeliverZcMessage(endpoint_header, (char*)buffer, len);
                    IOActor::message_deliver_end(endpoint_header);
                }
                else
                {
                    deliver_message->AppendBuffer(buffer, len);
                    deliver_message->dec_data_more(len);
                }
            });

            if (result > 0)
            {
                if (ADK_UNLIKELY(endpoint_header->set_rx_cork_from_pre()))
                { 
                    return IoResult::kDrop;                       
                }
                else
                {
                   return  IoResult::kActive0;
                }
            }
        }
    }
    else
    {
        const auto data_more = deliver_message->data_more();
        const auto batch_read_size = std::max<int32_t>(deliver_message->capacity() >> 1, data_more);
        auto* const rx_buffer = deliver_message->AllocBuffer(batch_read_size);
        assert(rx_buffer);

        if (kPreRecvEnable)
        {
            auto* const tcp_engine_impl = endpoint_header->tcp_engine_impl;
            assert(tcp_engine_impl);

            auto* const pre_recv_handler = tcp_engine_impl->pre_recv_handler();
            assert(pre_recv_handler);

            pre_recv_handler->OnRxMessageBefore(endpoint_header->share_ctx);
            result = tcp_endpoint->Recv(rx_buffer, batch_read_size);
            pre_recv_handler->OnRxMessageAfter(endpoint_header->share_ctx, result);
        }
        else
        {
            result = tcp_endpoint->Recv(rx_buffer, batch_read_size);
        }

        if (result > 0)
        {
            deliver_message->PostBuffer(result);
            endpoint_header->rx_message_bytes += result;
            if (result >= data_more)
            {
                IOActor::message_deliver_begin(endpoint_header);
                DeliverMessage(endpoint_header);
                IOActor::message_deliver_end(endpoint_header);
            }
            else
            {
                deliver_message->dec_data_more(result);
            }

            if (ADK_UNLIKELY(endpoint_header->set_rx_cork_from_pre()))
            { 
                return IoResult::kDrop;                       
            }
            else
            {
                return  IoResult::kActive0;
            }
        }
    }

    if (result < 0)
    {
        if (EAGAIN == errno)
        {
            return  IoResult::kActive1;
        }

        auto* const control_actor = drive_engine->control_actor();
        assert(control_actor);
        auto evt = new EventSocketError("RecvActor", errno);
        if (control_actor->ToDeliverEvent<true>(endpoint_header, evt) != ErrorCode::kSuccess)
        {
            delete evt;
        }
    }
    else
    {
        auto* const control_actor = drive_engine->control_actor();
        assert(control_actor);

        auto evt = new EventEndOfStream();
        if (control_actor->ToDeliverEvent<true>(endpoint_header, evt) != ErrorCode::kSuccess)
        {
            delete evt;
        }
    }

    return IoResult::kDrop;
}

template<ITcpStack::DriveMode kDriveMode, typename EPollerType>
ADK_ALWAYS_INLINE void OnRxEndpointBlock(DriveEngine* drive_engine, 
                                         EndpointHeader* endpoint_header,
                                         ThreadLocalQueue<EndpointHeader*>* active_eps)
{
    switch (kDriveMode)
    {
    case ITcpStack::DriveMode::kPoller:
        if (endpoint_header->in_rx_resident_time())
        {
            ADK_ASSERT_SUCCESS(active_eps->Push(endpoint_header));
        }
        else if (ADK_UNLIKELY(ErrorCode::kSuccess != drive_engine->OnRxEndpointBlock<EPollerType>(endpoint_header)))
        {
            ADK_ASSERT_SUCCESS(active_eps->Push(endpoint_header));
        }
        break;
    case ITcpStack::DriveMode::kReactor:
        ADK_ASSERT_SUCCESS(active_eps->Push(endpoint_header));
        break;
    default:
        assert(false);
    }
}

IOActor::IOActor(DriveEngine* const drive_engine)
{
    is_running_       = false;
    //actor_load_       = 0;
    drive_engine_     = drive_engine;
    futex_event_      = 0;
}

int32_t IOActor::Init(const Property& props)
{
    return ErrorCode::kSuccess;
}

void IOActor::Start()
{
    is_running_ = true;
}

void IOActor::Stop()
{
    is_running_ = false;

    if (ADK_UNLIKELY(futex_event_))
    {
        FutexWakePrivate(&futex_event_);
        futex_event_ = 0;
    }

    if (thread_hdl_.joinable())
    {
        thread_hdl_.join();
    }
}

void IOActor::Exit()
{
    futex_event_ = 0;
}

thread_local RecvActorArena* IOActor::s_actor_arena_ = nullptr;

SendActor::SendActor(DriveEngine* const drive_engine) 
    : IOActor(drive_engine)
{
    active_endpoints_ = nullptr;
}

int32_t SendActor::Init(const Property& props)
{
    if (ADK_UNLIKELY(ErrorCode::kSuccess != IOActor::Init(props)))
    {
        return ErrorCode::kFailure;
    }

    actor_cpu_list_ = props.GetValue(config::kTxActorCpuAffinity, std::string());

    assert(drive_engine_);
    auto* const tcp_engine_impl = drive_engine_->tcp_engine_impl();
    assert(tcp_engine_impl);

    active_endpoints_ = TxActiveEpQue::Create("tx_actor_queue", 
                                              tcp_engine_impl->engine_capacity());
    assert(active_endpoints_);

    // 从输入属性获取tx线程名, 若无则使用默认值
    name_ = props.GetValue(config::kTxActorName,
                            default_value::kTxActorName);
    
    auto prefix = tcp_engine_impl->engine_name();
    if (prefix.empty())
    {
        prefix = "adk";
    }
    name_ = prefix + "-" + name_;
    return ErrorCode::kSuccess;
}

void SendActor::Start(bool multithread)
{
    if (multithread)
    {
        Start<true>();
    }
    else
    {
        Start<false>();
    }
}

template <bool kInMultiThread>
void SendActor::Start()
{
    assert(drive_engine_);

    IOActor::Start();

    auto* const tcp_stack = drive_engine_->tcp_stack();
    assert(tcp_stack);

    auto* const tcp_engine_impl = drive_engine_->tcp_engine_impl();
    assert(tcp_engine_impl);

    switch (tcp_stack->stack_type())
    {
    case ITcpStack::StackType::kStackSk:
        if (tcp_engine_impl->is_tx_low_latency())
        {
            if (nullptr != tcp_engine_impl->pre_send_handler())
            {
                thread_hdl_ = std_thread(
                    name_.c_str(),
                    "send actor thread",
                    std::bind(&SendActor::ActorThread<true, true, verbs::TcpEndpointSk, kInMultiThread>, this));
            }
            else
            {
                thread_hdl_ = std_thread(
                    name_.c_str(),
                    "send actor thread", 
                    std::bind(&SendActor::ActorThread<true, false, verbs::TcpEndpointSk, kInMultiThread>, this));
            }
        }
        else
        {
            if (nullptr != tcp_engine_impl->pre_send_handler())
            {
                thread_hdl_ = std_thread(
                    name_.c_str(),
                    "send actor thread",
                    std::bind(&SendActor::ActorThread<false, true, verbs::TcpEndpointSk, kInMultiThread>, this));
            }
            else
            {
                thread_hdl_ = std_thread(
                    name_.c_str(),
                    "send actor thread",
                    std::bind(&SendActor::ActorThread<false, false, verbs::TcpEndpointSk, kInMultiThread>, this));
            }
        }
        break;
    case ITcpStack::StackType::kStackZf:
    default:
        assert(false);
    }
}

template<bool kIsLowLatency, bool kPreSendEnable, typename EndpointType, bool kInMultiThread>
void SendActor::ActorThread()
{
    uint32_t idle_counter = 0;
    uint64_t loop_counter = 0;
    EndpointHeader* endpoint_header = nullptr;

    assert(drive_engine_);
    auto* const tcp_engine_impl = drive_engine_->tcp_engine_impl();
    assert(tcp_engine_impl);

    auto* const active_endpoints = active_endpoints_->Duplicate();
    assert(active_endpoints);

    LocalStorageQueue<EndpointHeader*, kTxActiveQSize> active_eps;

    auto* const active_eps_0 = ThreadLocalQueue<EndpointHeader*>::Create("active0", 
                                                                         tcp_engine_impl->engine_capacity());
    assert(active_eps_0);

    auto* const active_eps_1 = ThreadLocalQueue<EndpointHeader*>::Create("active1", 
                                                                         tcp_engine_impl->engine_capacity());
    assert(active_eps_1);

    SetCpuAffinity(actor_cpu_list_);

    while (ACCESS_ONCE(is_running_))
    {
        const auto active_eps_len = active_eps.length();
        for (uint32_t index = 0; index < active_eps_len; ++index)
        {
            ADK_ASSERT_SUCCESS(active_eps.TryPop(endpoint_header));
            const auto result = SendMessage<EndpointType, kPreSendEnable, kInMultiThread>(drive_engine_, endpoint_header);
            if (IoResult::kActive0 == result)
            {
                ADK_ASSERT_SUCCESS(active_eps.TryPush(endpoint_header));
            }
            else if (IoResult::kActive1 == result)
            {
                if (kIsLowLatency)
                {
                    if ((active_eps_len <= kActorTxActive0Eps)
                        || endpoint_header->in_tx_active_time(kTxDecayAcitve0Loop))
                    {
                        if (endpoint_header->tx_valid)
                        {
                            ADK_ASSERT_SUCCESS(active_eps.TryPush(endpoint_header));
                        }
                        else
                        {
                            drive_engine_->DropTxEndpoint(endpoint_header);
                        }

                        continue;
                    }
                }

                ADK_ASSERT_SUCCESS(active_eps_0->Push(endpoint_header));
            }
        }

        const auto active_eps0_len = active_eps_0->length();
        for (uint32_t index = 0; index < active_eps0_len; ++index)
        {
            ADK_ASSERT_SUCCESS(active_eps_0->Pop(endpoint_header));
            if (ADK_UNLIKELY(!endpoint_header->tx_valid))
            {
                drive_engine_->DropTxEndpoint(endpoint_header);
                continue;
            }

            const auto result = SendMessage<EndpointType, kPreSendEnable, kInMultiThread>(drive_engine_, endpoint_header);
            if (IoResult::kActive0 == result)
            {
                endpoint_header->tx_sch_time = 0;
                PUSH_TX_ACTIVE_QUEUE(active_eps, active_eps_0, endpoint_header);
            }
            else if (IoResult::kActive1 == result)
            {
                if (endpoint_header->in_tx_active_time(kTxDecayAcitve1Loop))
                {
                    ADK_ASSERT_SUCCESS(active_eps_0->Push(endpoint_header));
                }
                else
                {
                    ADK_ASSERT_SUCCESS(active_eps_1->Push(endpoint_header));
                    endpoint_header->tx_sch_time = endpoint_header->GetTimepoint();
                }
            }
        }

        const auto active_eps1_len = active_eps_1->length();
        if (0 == ((++loop_counter) & kBackOffMask))
        {
            while (ADK_UNLIKELY(ErrorCode::kSuccess == active_endpoints->Pop(endpoint_header)))
            {
                const auto result = SendMessage<EndpointType, kPreSendEnable, kInMultiThread>(drive_engine_, endpoint_header);
                if (IoResult::kDrop != result)
                {
                    endpoint_header->tx_sch_time = 0;
                    PUSH_ACTIVE_QUEUE(active_eps, active_eps_0, endpoint_header);
                }
            }

            for (int32_t index = 0; index < active_eps1_len; ++index)
            {
                ADK_ASSERT_SUCCESS(active_eps_1->Pop(endpoint_header));
                if (ADK_UNLIKELY(!endpoint_header->tx_valid))
                {
                    drive_engine_->DropTxEndpoint(endpoint_header);
                    continue;
                }

                const auto result = SendMessage<EndpointType, kPreSendEnable, kInMultiThread>(drive_engine_, endpoint_header);
                if (IoResult::kActive0 == result)
                {
                    endpoint_header->tx_sch_time = 0;
                    PUSH_TX_ACTIVE_QUEUE(active_eps, active_eps_0, endpoint_header);
                }
                else if (IoResult::kActive1 == result)
                {
                    if (endpoint_header->in_tx_resident_time())
                    {
                        ADK_ASSERT_SUCCESS(active_eps_1->Push(endpoint_header));
                    }
                    else
                    {
                        drive_engine_->OnTxEndpointIdle(endpoint_header);
                    }
                }
            }

            if (0 == active_eps.length() + active_eps0_len + active_eps1_len)
            {
                //actor_load_ = 0;
                OnActorIdle<kIsLowLatency>(idle_counter);
            }
            else
            {
                if (!kIsLowLatency)
                {
                    idle_counter = 0;
                }

                /*
                actor_load_ = (active_eps_len << kTxActiveLoadWBits)
                            + (active_eps0_len << kTxActive0LoadWBits)
                            + active_eps1_len;
                            */
            }
        }
    }

    while (ErrorCode::kSuccess == active_endpoints->Pop(endpoint_header))
    {
        drive_engine_->DropTxEndpoint(endpoint_header);
    }

    TxActiveEpQue::Delete(active_endpoints);

    while (ErrorCode::kSuccess == active_eps.TryPop(endpoint_header))
    {
        drive_engine_->DropTxEndpoint(endpoint_header);
    }

    while (ErrorCode::kSuccess == active_eps_0->Pop(endpoint_header))
    {
        drive_engine_->DropTxEndpoint(endpoint_header);
    }

    ThreadLocalQueue<EndpointHeader*>::Delete(active_eps_0);

    while (ErrorCode::kSuccess == active_eps_1->Pop(endpoint_header))
    {
        drive_engine_->DropTxEndpoint(endpoint_header);
    }

    ThreadLocalQueue<EndpointHeader*>::Delete(active_eps_1);
}

void SendActor::Stop()
{
    IOActor::Stop();

    assert(drive_engine_);
    assert(active_endpoints_);

    EndpointHeader* endpoint_header = nullptr;
    while (ErrorCode::kSuccess == active_endpoints_->Pop(endpoint_header))
    {
        drive_engine_->DropTxEndpoint(endpoint_header);
    }
}

void SendActor::Exit()
{
    if (is_running_)
    {
        Stop();
    }

    IOActor::Exit();

    if (nullptr != active_endpoints_)
    {
        TxActiveEpQue::Delete(active_endpoints_);
        active_endpoints_ = nullptr;
    }
}

RecvActor::RecvActor(DriveEngine* const drive_engine) 
    : IOActor(drive_engine)
{
    active_endpoints_ = nullptr;
}

int32_t RecvActor::Init(const Property& props)
{
    if (ADK_UNLIKELY(ErrorCode::kSuccess != IOActor::Init(props)))
    {
        return ErrorCode::kFailure;
    }

    actor_cpu_list_ = props.GetValue(config::kTxActorCpuAffinity, std::string());

    rx_message_pool_.Init(props.GetValue(config::kRxMemoryBlockSize,
                                         default_value::kRxMemoryBlockSize), 
                          props.GetValue(config::kRxMemoryPoolSize,
                                         default_value::kRxMemoryPoolSize));

    
    assert(drive_engine_);
    auto* const tcp_engine_impl = drive_engine_->tcp_engine_impl();
    assert(tcp_engine_impl);

    active_endpoints_ = RxActiveEpQue::Create("rx_actor_queue", 
                                              tcp_engine_impl->engine_capacity());
    assert(active_endpoints_);

    // 从输入属性获取rx线程名, 若无则使用默认值
    name_ = props.GetValue(config::kRxActorName,
                                default_value::kRxActorName);
    auto prefix = tcp_engine_impl->engine_name();
    if (prefix.empty())
    {
        prefix = "adk";
    }
    name_ = prefix + "-" + name_;
    return ErrorCode::kSuccess;
}

void RecvActor::Start()
{
    assert(drive_engine_);

    auto* const tcp_stack = drive_engine_->tcp_stack();
    assert(tcp_stack);

    IOActor::Start();

    auto* const tcp_engine_impl = drive_engine_->tcp_engine_impl();
    assert(tcp_engine_impl);

    switch (tcp_stack->stack_type())
    {
    case ITcpStack::StackType::kStackSk:
        if (tcp_engine_impl->is_rx_low_latency())
        {
            if (nullptr != tcp_engine_impl->pre_recv_handler())
            {
                thread_hdl_ = std_thread(
                    name_.c_str(),
                    "recv actor thread",
                    std::bind(&RecvActor::ActorThread<true, true, verbs::TcpEndpointSk>, this));
            }
            else
            {
                thread_hdl_ = std_thread(
                    name_.c_str(),
                    "recv actor thread",
                    std::bind(&RecvActor::ActorThread<true, false, verbs::TcpEndpointSk>, this));
            }
        }
        else
        {
            if (nullptr != tcp_engine_impl->pre_recv_handler())
            {
                thread_hdl_ = std_thread(
                    name_.c_str(),
                    "recv actor thread",
                    std::bind(&RecvActor::ActorThread<false, true, verbs::TcpEndpointSk>, this));
            }
            else
            {
                thread_hdl_ = std_thread(
                    name_.c_str(),
                    "recv actor thread",
                    std::bind(&RecvActor::ActorThread<false, false, verbs::TcpEndpointSk>, this));
            }
        }
        break;
    case ITcpStack::StackType::kStackZf:
    default:
        assert(false);
    }
}

template<bool kIsLowLatency, bool kPreRecvEnable, typename EndpointType>
void RecvActor::ActorThread()
{
    uint32_t idle_counter = 0;
    EndpointHeader* endpoint_header = nullptr;

    RecvActorArena rx_actor_area(&rx_message_pool_);

    assert(drive_engine_);
    drive_engine_->InsertActorArena(&rx_actor_area);

    assert(nullptr == s_actor_arena_);
    s_actor_arena_ = &rx_actor_area;

    auto* const tcp_engine_impl = drive_engine_->tcp_engine_impl();
    assert(tcp_engine_impl);

    auto* const active_endpoints = active_endpoints_->Duplicate();
    assert(active_endpoints);

    LocalStorageQueue<EndpointHeader*, kRxActiveQSize> active_eps;

    auto* const active_eps_0 = ThreadLocalQueue<EndpointHeader*>::Create("active0",
                                                                         tcp_engine_impl->engine_capacity());
    assert(active_eps_0);

    auto* const active_eps_1 = ThreadLocalQueue<EndpointHeader*>::Create("active1", 
                                                                         tcp_engine_impl->engine_capacity());
    assert(active_eps_1);

    SetCpuAffinity(actor_cpu_list_);

#if 0
    std::cout << "default SCHED_FIFO priority " << sched_getscheduler(0) << std::endl;

    struct sched_param param;
    memset(&param, 0x00, sizeof(param));
    param.sched_priority = sched_get_priority_min(SCHED_FIFO);
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0)
    {
        std::cout << "sched_setscheduler SCHED_FIFO priority " << param.sched_priority 
                  << " failed, " << strerror(errno) << std::endl;
    }
    else
    {
        std::cout << "sched_setscheduler SCHED_FIFO priority " << param.sched_priority << std::endl;
    }
#endif

    while (ACCESS_ONCE(is_running_))
    {
        const auto active_eps_len = active_eps.length();
        for (int32_t index = 0; index < active_eps_len; ++index)
        {
            ADK_ASSERT_SUCCESS(active_eps.TryPop(endpoint_header));
            const auto result = RecvAndDeliverMessage<EndpointType, kPreRecvEnable>(drive_engine_, endpoint_header);
            if (IoResult::kActive0 == result)
            {
                ADK_ASSERT_SUCCESS(active_eps.TryPush(endpoint_header));
            }
            else if (IoResult::kActive1 == result)
            {
                if (kIsLowLatency)
                {
                    if ((active_eps_len <= kActorRxActive0Eps)
                        || endpoint_header->in_rx_active_time(kRxDecayAcitve0Loop))
                    {
                        if (endpoint_header->rx_valid)
                        {
                            ADK_ASSERT_SUCCESS(active_eps.TryPush(endpoint_header));
                        }
                        else
                        {
                            drive_engine_->DropRxEndpoint(endpoint_header);
                        }

                        continue;
                    }
                }

                ADK_ASSERT_SUCCESS(active_eps_0->Push(endpoint_header));
            }
        }

        const auto active_eps0_len = active_eps_0->length();
        for (int32_t index = 0; index < active_eps0_len; ++index)
        {
            ADK_ASSERT_SUCCESS(active_eps_0->Pop(endpoint_header));
            if (ADK_UNLIKELY(!endpoint_header->rx_valid))
            {
                drive_engine_->DropRxEndpoint(endpoint_header);
                continue;
            }

            const auto result = RecvAndDeliverMessage<EndpointType, kPreRecvEnable>(drive_engine_, endpoint_header);
            if (IoResult::kActive0 == result)
            {
                endpoint_header->rx_sch_time = 0;
                PUSH_RX_ACTIVE_QUEUE(active_eps, active_eps_0, endpoint_header);
            }
            else if (IoResult::kActive1 == result)
            {
                if (endpoint_header->in_rx_active_time(kRxDecayAcitve1Loop))
                {
                    ADK_ASSERT_SUCCESS(active_eps_0->Push(endpoint_header));
                }
                else
                {
                    ADK_ASSERT_SUCCESS(active_eps_1->Push(endpoint_header));
                    endpoint_header->rx_sch_time = endpoint_header->GetTimepoint();
                }
            }
        }

        const auto active_eps1_len = active_eps_1->length();
        if (0 == ((++rx_actor_area.actor_counter) & kBackOffMask))
        {
            while (ErrorCode::kSuccess == active_endpoints->Pop(endpoint_header))
            {
                const auto result = RecvAndDeliverMessage<EndpointType, kPreRecvEnable>(drive_engine_, endpoint_header);
              //  assert(IoResult::kActive1 != result);

                if (IoResult::kDrop != result)
                {
                    endpoint_header->rx_sch_time = 0;
                    PUSH_ACTIVE_QUEUE(active_eps, active_eps_0, endpoint_header);
                }
            }

            for (int32_t index = 0; index < active_eps1_len; ++index)
            {
                ADK_ASSERT_SUCCESS(active_eps_1->Pop(endpoint_header));
                if (ADK_UNLIKELY(!endpoint_header->rx_valid))
                {
                    drive_engine_->DropRxEndpoint(endpoint_header);
                    continue;
                }

                const auto result = RecvAndDeliverMessage<EndpointType, kPreRecvEnable>(drive_engine_, endpoint_header);
                if (IoResult::kActive0 == result)
                {
                    PUSH_RX_ACTIVE_QUEUE(active_eps, active_eps_0, endpoint_header);
                }
                else if (IoResult::kActive1 == result)
                {
                    if (endpoint_header->in_rx_resident_time())
                    {
                        ADK_ASSERT_SUCCESS(active_eps_1->Push(endpoint_header));
                    }
                    else
                    {
                        if (ADK_UNLIKELY(ErrorCode::kSuccess != drive_engine_->OnRxEndpointBlock<typename EndpointType::EPollerType>(endpoint_header)))
                        {
                            ADK_ASSERT_SUCCESS(active_eps_1->Push(endpoint_header));
                        }
                    }
                }
            }

            if (0 == active_eps.length() + active_eps0_len + active_eps1_len)
            {
                // actor_load_ = 0;
                OnActorIdle<kIsLowLatency>(idle_counter);
            }
            else
            {
                if (!kIsLowLatency)
                {
                    idle_counter = 0;
                }

                /*
                actor_load_ = (active_eps_len << kRxActiveLoadWBits)
                            + (active_eps0_len << kRxActive0LoadWBits)
                            + active_eps1_len;*/
            }
        }
    }

    assert(s_actor_arena_);
    s_actor_arena_ = nullptr;
    drive_engine_->RemoveActorArena(&rx_actor_area);

    while (ErrorCode::kSuccess == active_endpoints->Pop(endpoint_header))
    {
        drive_engine_->DropRxEndpoint(endpoint_header);
    }

    RxActiveEpQue::Delete(active_endpoints);

    while (ErrorCode::kSuccess == active_eps.TryPop(endpoint_header))
    {
        drive_engine_->DropRxEndpoint(endpoint_header);
    }

    while (ErrorCode::kSuccess == active_eps_0->Pop(endpoint_header))
    {
        drive_engine_->DropRxEndpoint(endpoint_header);
    }

    ThreadLocalQueue<EndpointHeader*>::Delete(active_eps_0);

    while (ErrorCode::kSuccess == active_eps_1->Pop(endpoint_header))
    {
        drive_engine_->DropRxEndpoint(endpoint_header);
    }

    ThreadLocalQueue<EndpointHeader*>::Delete(active_eps_1);
}

void RecvActor::Stop()
{
    IOActor::Stop();

    assert(drive_engine_);
    assert(active_endpoints_);

    EndpointHeader* endpoint_header = nullptr;
    while (ErrorCode::kSuccess == active_endpoints_->Pop(endpoint_header))
    {
        drive_engine_->DropRxEndpoint(endpoint_header);
    }
}

void RecvActor::Exit()
{
    if (is_running_)
    {
        Stop();
    }

    IOActor::Exit();

    if (nullptr != active_endpoints_)
    {
        RxActiveEpQue::Delete(active_endpoints_);
        active_endpoints_ = nullptr;
    }
}

DuplexActor::DuplexActor(DriveEngine* const drive_engine)
    : SendActor(drive_engine)
{
    active_rxendpoints_ = nullptr;
}

int32_t DuplexActor::Init(const Property& props)
{
    if (ADK_UNLIKELY(ErrorCode::kSuccess != SendActor::Init(props)))
    {
        return ErrorCode::kFailure;
    }

    const auto tx_actor_cpu = props.GetValue(config::kTxActorCpuAffinity, std::string());
    const auto rx_actor_cpu = props.GetValue(config::kRxActorCpuAffinity, std::string());
    if (!tx_actor_cpu.empty())
    {
        actor_cpu_list_ = tx_actor_cpu;
        if (!rx_actor_cpu.empty())
        {
            actor_cpu_list_ += ",";
            actor_cpu_list_ += rx_actor_cpu;
        }
    }
    else
    {
        actor_cpu_list_ = rx_actor_cpu;
    }

    rx_message_pool_.Init(props.GetValue(config::kRxMemoryBlockSize, 
                                         default_value::kRxMemoryBlockSize),
                          props.GetValue(config::kRxMemoryPoolSize, 
                                         default_value::kRxMemoryPoolSize));

    assert(drive_engine_);
    auto* const tcp_engine_impl = drive_engine_->tcp_engine_impl();
    assert(tcp_engine_impl);

    active_rxendpoints_ = RxActiveEpQue::Create("rx_actor_queue", 
                                                tcp_engine_impl->engine_capacity());
    assert(active_rxendpoints_);

      // 从输入属性获取rx线程名, 若无则使用默认值
    name_ = props.GetValue(config::kDupActorName,
                                         default_value::kDupActorName);

    auto prefix = tcp_engine_impl->engine_name();
    if (prefix.empty())
    {
        prefix = "adk";
    }
    name_ = prefix + "-" + name_;

    return ErrorCode::kSuccess;
}

void DuplexActor::Start(bool multithread)
{
    if (multithread)
    {
        Start<true>();
    }
    else
    {
        Start<false>();
    }
}

template <bool kInMultiThread>
void DuplexActor::Start()
{
    IOActor::Start();

    assert(drive_engine_);
    auto* const tcp_stack = drive_engine_->tcp_stack();
    assert(tcp_stack);

    auto* const tcp_engine_impl = drive_engine_->tcp_engine_impl();
    assert(tcp_engine_impl);

    switch (tcp_stack->stack_type())
    {
    case ITcpStack::StackType::kStackZf:
        if (tcp_engine_impl->is_tx_low_latency() || tcp_engine_impl->is_rx_low_latency())
        {
            if (nullptr != tcp_engine_impl->pre_send_handler())
            {
                if (nullptr != tcp_engine_impl->pre_recv_handler())
                {
                    thread_hdl_ = std_thread(
                        name_.c_str(),
                        "duplex actor thread",
                        std::bind(&DuplexActor::ActorThread<true, true, true, verbs::TcpEndpointZf, kInMultiThread>, this));
                }
                else
                {
                    thread_hdl_ = std_thread(
                        name_.c_str(),
                        "duplex actor thread",
                        std::bind(&DuplexActor::ActorThread<true, true, false, verbs::TcpEndpointZf, kInMultiThread>, this));
                }
            }
            else
            {
                if (nullptr != tcp_engine_impl->pre_recv_handler())
                {
                    thread_hdl_ = std_thread(
                         name_.c_str(),
                        "duplex actor thread",
                        std::bind(&DuplexActor::ActorThread<true, false, true, verbs::TcpEndpointZf, kInMultiThread>, this));
                }
                else
                {
                    thread_hdl_ = std_thread(
                        "adk-ioe-dupactor",
                        "duplex actor thread",
                        std::bind(&DuplexActor::ActorThread<true, false, false, verbs::TcpEndpointZf, kInMultiThread>, this));
                }
            }
        }
        else
        {
            if (nullptr != tcp_engine_impl->pre_send_handler())
            {
                if (nullptr != tcp_engine_impl->pre_recv_handler())
                {
                    thread_hdl_ = std_thread(
                        name_.c_str(),
                        "duplex actor thread",
                        std::bind(&DuplexActor::ActorThread<false, true, true, verbs::TcpEndpointZf, kInMultiThread>, this));
                }
                else
                {
                    thread_hdl_ = std_thread(
                        name_.c_str(),
                        "duplex actor thread",
                        std::bind(&DuplexActor::ActorThread<false, true, false, verbs::TcpEndpointZf, kInMultiThread>, this));
                }
            }
            else
            {
                if (nullptr != tcp_engine_impl->pre_recv_handler())
                {
                    thread_hdl_ = std_thread(
                        name_.c_str(),
                        "duplex actor thread",
                        std::bind(&DuplexActor::ActorThread<false, false, true, verbs::TcpEndpointZf, kInMultiThread>, this));
                }
                else
                {
                    thread_hdl_ = std_thread(
                        name_.c_str(),
                        "duplex actor thread",
                        std::bind(&DuplexActor::ActorThread<false, false, false, verbs::TcpEndpointZf, kInMultiThread>, this));
                }
            }
        }
        break;
    case ITcpStack::StackType::kStackSk:
        if (tcp_engine_impl->is_tx_low_latency() || tcp_engine_impl->is_rx_low_latency())
        {
            if (nullptr != tcp_engine_impl->pre_send_handler())
            {
                if (nullptr != tcp_engine_impl->pre_recv_handler())
                {
                    thread_hdl_ = std_thread(
                        name_.c_str(),
                        "duplex actor thread",
                        std::bind(&DuplexActor::ActorThread<true, true, true, verbs::TcpEndpointSk, kInMultiThread>, this));
                }
                else
                {
                    thread_hdl_ = std_thread(
                        name_.c_str(),
                        "duplex actor thread",
                        std::bind(&DuplexActor::ActorThread<true, true, false, verbs::TcpEndpointSk, kInMultiThread>, this));
                }
            }
            else
            {
                if (nullptr != tcp_engine_impl->pre_recv_handler())
                {
                    thread_hdl_ = std_thread(
                        name_.c_str(),
                        "duplex actor thread",
                        std::bind(&DuplexActor::ActorThread<true, false, true, verbs::TcpEndpointSk, kInMultiThread>, this));
                }
                else
                {
                    thread_hdl_ = std_thread(
                        name_.c_str(),
                        "duplex actor thread",
                        std::bind(&DuplexActor::ActorThread<true, false, false, verbs::TcpEndpointSk, kInMultiThread>, this));
                }
            }
        }
        else
        {
            if (nullptr != tcp_engine_impl->pre_send_handler())
            {
                if (nullptr != tcp_engine_impl->pre_recv_handler())
                {
                    thread_hdl_ = std_thread(
                        name_.c_str(),
                        "duplex actor thread",
                        std::bind(&DuplexActor::ActorThread<false, true, true, verbs::TcpEndpointSk, kInMultiThread>, this));
                }
                else
                {
                    thread_hdl_ = std_thread(
                        name_.c_str(),
                        "duplex actor thread",
                        std::bind(&DuplexActor::ActorThread<false, true, false, verbs::TcpEndpointSk, kInMultiThread>, this));
                }
            }
            else
            {
                if (nullptr != tcp_engine_impl->pre_recv_handler())
                {
                    thread_hdl_ = std_thread(
                        name_.c_str(),
                        "duplex actor thread",
                        std::bind(&DuplexActor::ActorThread<false, false, true, verbs::TcpEndpointSk, kInMultiThread>, this));
                }
                else
                {
                    thread_hdl_ = std_thread(
                        name_.c_str(),
                        "duplex actor thread",
                        std::bind(&DuplexActor::ActorThread<false, false, false, verbs::TcpEndpointSk, kInMultiThread>, this));
                }
            }
        }
        break;
    default:
        assert(false);
    }
}

void DuplexActor::Stop()
{
    SendActor::Stop();

    assert(drive_engine_);
    assert(active_rxendpoints_);

    EndpointHeader* endpoint_header = nullptr;
    while (ErrorCode::kSuccess == active_rxendpoints_->Pop(endpoint_header))
    {
        drive_engine_->DropRxEndpoint(endpoint_header);
    }
}

void DuplexActor::Exit()
{
    if (is_running_)
    {
        Stop();
    }

    SendActor::Exit();
    if (nullptr != active_rxendpoints_)
    {
        RxActiveEpQue::Delete(active_rxendpoints_);
        active_rxendpoints_ = nullptr;
    }
}

template<bool kIsLowLatency, bool kPreSendEnable, bool kPreRecvEnable, typename EndpointType, bool kInMultiThread>
void DuplexActor::ActorThread()
{
    uint32_t idle_counter = 0;
    EndpointHeader* endpoint_header = nullptr;

    RecvActorArena actor_arena(&rx_message_pool_);

    assert(drive_engine_);
    drive_engine_->InsertActorArena(&actor_arena);

    assert(nullptr == s_actor_arena_);
    s_actor_arena_ = &actor_arena;

    auto* const tcp_engine_impl = drive_engine_->tcp_engine_impl();
    assert(tcp_engine_impl);

    assert(active_rxendpoints_);
    auto* const active_rxendpoints = active_rxendpoints_->Duplicate();
    assert(active_rxendpoints);

    LocalStorageQueue<EndpointHeader*, kDuplexRxActiveQSize> active_rxeps;

    auto* const active_rxeps_0 = ThreadLocalQueue<EndpointHeader*>::Create("rx-active0", 
                                                                           tcp_engine_impl->engine_capacity());
    assert(active_rxeps_0);

    auto* const active_rxeps_1 = ThreadLocalQueue<EndpointHeader*>::Create("rx-active1", 
                                                                           tcp_engine_impl->engine_capacity());
    assert(active_rxeps_1);

    assert(active_endpoints_);
    auto* const active_txendpoints = active_endpoints_->Duplicate();
    assert(active_txendpoints);

    LocalStorageQueue<EndpointHeader*, kDuplexTxActiveQSize> active_txeps;

    auto* const active_txeps_0 = ThreadLocalQueue<EndpointHeader*>::Create("tx-active0", 
                                                                           tcp_engine_impl->engine_capacity());
    assert(active_txeps_0);

    auto* const active_txeps_1 = ThreadLocalQueue<EndpointHeader*>::Create("tx-active1", 
                                                                           tcp_engine_impl->engine_capacity());
    assert(active_txeps_1);

    auto* const tcp_stack = (typename EndpointType::StackType*)(tcp_engine_impl->tcp_stack());
    assert(tcp_stack);

    SetCpuAffinity(actor_cpu_list_);

#ifdef _IO_ENGINE_PERF_TEST_
    StatsInfo stats_info0("Reactor 0");
    StatsInfo stats_info1("Reactor 1");
#endif

    while (ACCESS_ONCE(is_running_))
    {
#ifdef _IO_ENGINE_PERF_TEST_
        struct timespec timepoint_before;
        clock_gettime(CLOCK_MONOTONIC_RAW, &timepoint_before);
#endif

        /**
         * do rx reverse according to the result of ReactorPerform may be inaccurate
        const auto need_rx_reverse = (ITcpStack::DriveMode::kReactor == EndpointType::drive_mode())
                                   ? (bool)(tcp_stack->ReactorPerform())
                                   : true;
         */
        constexpr bool need_rx_reverse = true;
        if (ITcpStack::DriveMode::kReactor == EndpointType::drive_mode())
        {
            tcp_stack->ReactorPerform();
        }

#ifdef _IO_ENGINE_PERF_TEST_
        struct timespec timepoint_after;
        clock_gettime(CLOCK_MONOTONIC_RAW, &timepoint_after);

        if (need_rx_reverse)
        {
            stats_info1.OnNewInfo(timepoint_after.tv_sec * 1000000000 + timepoint_after.tv_nsec
                                   - (timepoint_before.tv_sec * 1000000000 + timepoint_before.tv_nsec));
        }
        else
        {
            stats_info0.OnNewInfo(timepoint_after.tv_sec * 1000000000 + timepoint_after.tv_nsec
                                   - (timepoint_before.tv_sec * 1000000000 + timepoint_before.tv_nsec));
        }
#endif

        const auto active_rxeps_len = active_rxeps.length();
        if (need_rx_reverse)
        {
            for (uint32_t index = 0; index < active_rxeps_len; ++index)
            {
                ADK_ASSERT_SUCCESS(active_rxeps.TryPop(endpoint_header));
                const auto result = RecvAndDeliverMessage<EndpointType, kPreRecvEnable>(drive_engine_, endpoint_header);
                if (IoResult::kActive0 == result)
                {
                    ADK_ASSERT_SUCCESS(active_rxeps.TryPush(endpoint_header));
                }
                else if (IoResult::kActive1 == result)
                {
                    if (kIsLowLatency)
                    {
                        if ((active_rxeps_len <= kDuplexRxActive0Eps)
                            || endpoint_header->in_rx_active_time(kRxDecayAcitve0Loop))
                        {
                            if (endpoint_header->rx_valid)
                            {
                                ADK_ASSERT_SUCCESS(active_rxeps.TryPush(endpoint_header));
                            }
                            else
                            {
                                drive_engine_->DropRxEndpoint(endpoint_header);
                            }

                            continue;
                        }
                    }

                    ADK_ASSERT_SUCCESS(active_rxeps_0->Push(endpoint_header));
                }
            }
        }

        const auto active_txeps_len = active_txeps.length();
        for (uint32_t index = 0; index < active_txeps_len; ++index)
        {
            ADK_ASSERT_SUCCESS(active_txeps.TryPop(endpoint_header));
            const auto result = SendMessage<EndpointType, kPreSendEnable, kInMultiThread>(drive_engine_, endpoint_header);
            if (IoResult::kActive0 == result)
            {
                ADK_ASSERT_SUCCESS(active_txeps.TryPush(endpoint_header));
            }
            else if (IoResult::kActive1 == result)
            {
                if (kIsLowLatency)
                {
                    if ((active_txeps_len <= kDuplexTxActive0Eps)
                        || endpoint_header->in_tx_active_time(kTxDecayAcitve0Loop))
                    {
                        if (endpoint_header->tx_valid)
                        {
                            ADK_ASSERT_SUCCESS(active_txeps.TryPush(endpoint_header));
                        }
                        else
                        {
                            drive_engine_->DropTxEndpoint(endpoint_header);
                        }

                        continue;
                    }
                }

                ADK_ASSERT_SUCCESS(active_txeps_0->Push(endpoint_header));
            }
        }

        const auto active_rxeps0_len = active_rxeps_0->length();
        if (need_rx_reverse)
        {
            for (uint32_t index = 0; index < active_rxeps0_len; ++index)
            {
                ADK_ASSERT_SUCCESS(active_rxeps_0->Pop(endpoint_header));
                if (ADK_UNLIKELY(!endpoint_header->rx_valid))
                {
                    drive_engine_->DropRxEndpoint(endpoint_header);
                    continue;
                }

                const auto result = RecvAndDeliverMessage<EndpointType, kPreRecvEnable>(drive_engine_, endpoint_header);
                if (IoResult::kActive0 == result)
                {
                    endpoint_header->rx_sch_time = 0;
                    PUSH_RX_ACTIVE_QUEUE(active_rxeps, active_rxeps_0, endpoint_header);
                }
                else if (IoResult::kActive1 == result)
                {
                    if (endpoint_header->in_rx_active_time(kRxDecayAcitve1Loop))
                    {
                        ADK_ASSERT_SUCCESS(active_rxeps_0->Push(endpoint_header));
                    }
                    else
                    {
                        ADK_ASSERT_SUCCESS(active_rxeps_1->Push(endpoint_header));
                        endpoint_header->rx_sch_time = endpoint_header->GetTimepoint();
                    }
                }
            }
        }

        const auto active_txeps0_len = active_txeps_0->length();
        for (uint32_t index = 0; index < active_txeps0_len; ++index)
        {
            ADK_ASSERT_SUCCESS(active_txeps_0->Pop(endpoint_header));
            if (ADK_UNLIKELY(!endpoint_header->tx_valid))
            {
                drive_engine_->DropTxEndpoint(endpoint_header);
                continue;
            }

            const auto result = SendMessage<EndpointType, kPreSendEnable, kInMultiThread>(drive_engine_, endpoint_header);
            if (IoResult::kActive0 == result)
            {
                endpoint_header->tx_sch_time = 0;
                PUSH_TX_ACTIVE_QUEUE(active_txeps, active_txeps_0, endpoint_header);
            }
            else if (IoResult::kActive1 == result)
            {
                if (endpoint_header->in_tx_active_time(kTxDecayAcitve1Loop))
                {
                    ADK_ASSERT_SUCCESS(active_txeps_0->Push(endpoint_header));
                }
                else
                {
                    ADK_ASSERT_SUCCESS(active_txeps_1->Push(endpoint_header));
                    endpoint_header->tx_sch_time = endpoint_header->GetTimepoint();
                }
            }
        }

        if (0 == ((++actor_arena.actor_counter) & kBackOffMask))
        {
            while (ADK_UNLIKELY(ErrorCode::kSuccess == active_rxendpoints->Pop(endpoint_header)))
            {
                const auto result = RecvAndDeliverMessage<EndpointType, kPreRecvEnable>(drive_engine_, endpoint_header);
                if (ITcpStack::DriveMode::kPoller == EndpointType::drive_mode())
                {
                    assert(IoResult::kActive1 != result);
                }

                if (IoResult::kDrop != result)
                {
                    endpoint_header->rx_sch_time = 0;
                    PUSH_ACTIVE_QUEUE(active_rxeps, active_rxeps_0, endpoint_header);
                }
            }

            while (ADK_UNLIKELY(ErrorCode::kSuccess == active_txendpoints->Pop(endpoint_header)))
            {
                const auto result = SendMessage<EndpointType, kPreSendEnable, kInMultiThread>(drive_engine_, endpoint_header);
                if (IoResult::kDrop != result)
                {
                    endpoint_header->tx_sch_time = 0;
                    PUSH_ACTIVE_QUEUE(active_txeps, active_txeps_0, endpoint_header);
                }
            }

            const auto active_rxeps1_len = active_rxeps_1->length();
            if (need_rx_reverse)
            {
                for (int32_t index = 0; index < active_rxeps1_len; ++index)
                {
                    ADK_ASSERT_SUCCESS(active_rxeps_1->Pop(endpoint_header));
                    if (ADK_UNLIKELY(!endpoint_header->rx_valid))
                    {
                        drive_engine_->DropRxEndpoint(endpoint_header);
                        continue;
                    }

                    const auto result = RecvAndDeliverMessage<EndpointType, 
                                                              kPreRecvEnable>(drive_engine_, endpoint_header);
                    if (IoResult::kActive0 == result)
                    {
                        endpoint_header->rx_sch_time = 0;
                        PUSH_RX_ACTIVE_QUEUE(active_rxeps, active_rxeps_0, endpoint_header);
                    }
                    else if (IoResult::kActive1 == result)
                    {
                        OnRxEndpointBlock<EndpointType::drive_mode(), typename EndpointType::EPollerType>(
                            drive_engine_, 
                            endpoint_header, 
                            active_rxeps_1);
                    }
                }
            }
            else
            {
                for (uint32_t index = 0; index < active_rxeps_len; ++index)
                {
                    ADK_ASSERT_SUCCESS(active_rxeps.TryPop(endpoint_header));
                    if (ADK_UNLIKELY(!endpoint_header->rx_valid))
                    {
                        drive_engine_->DropRxEndpoint(endpoint_header);
                        continue;
                    }

                    if ((active_rxeps_len <= kDuplexRxActive0Eps)
                        || endpoint_header->in_rx_active_time(kRxDecayAcitve0Loop))
                    {
                        ADK_ASSERT_SUCCESS(active_rxeps.TryPush(endpoint_header));
                    }
                    else
                    {
                        ADK_ASSERT_SUCCESS(active_rxeps_0->Push(endpoint_header));
                    }
                }

                for (uint32_t index = 0; index < active_rxeps0_len; ++index)
                {
                    ADK_ASSERT_SUCCESS(active_rxeps_0->Pop(endpoint_header));
                    if (ADK_UNLIKELY(!endpoint_header->rx_valid))
                    {
                        drive_engine_->DropRxEndpoint(endpoint_header);
                        continue;
                    }

                    if (endpoint_header->in_rx_active_time(kRxDecayAcitve1Loop))
                    {
                        ADK_ASSERT_SUCCESS(active_rxeps_0->Push(endpoint_header));
                    }
                    else
                    {
                        ADK_ASSERT_SUCCESS(active_rxeps_1->Push(endpoint_header));
                        endpoint_header->rx_sch_time = endpoint_header->GetTimepoint();
                    }
                }

                for (int32_t index = 0; index < active_rxeps1_len; ++index)
                {
                    ADK_ASSERT_SUCCESS(active_rxeps_1->Pop(endpoint_header));
                    if (ADK_UNLIKELY(!endpoint_header->rx_valid))
                    {
                        drive_engine_->DropRxEndpoint(endpoint_header);
                        continue;
                    }

                    OnRxEndpointBlock<EndpointType::drive_mode(), typename EndpointType::EPollerType>(
                        drive_engine_, 
                        endpoint_header, 
                        active_rxeps_1);
                }
            }

            const auto active_txeps1_len = active_txeps_1->length();
            for (int32_t index = 0; index < active_txeps1_len; ++index)
            {
                ADK_ASSERT_SUCCESS(active_txeps_1->Pop(endpoint_header));
                if (ADK_UNLIKELY(!endpoint_header->tx_valid))
                {
                    drive_engine_->DropTxEndpoint(endpoint_header);
                    continue;
                }

                const auto result = SendMessage<EndpointType, kPreSendEnable, kInMultiThread>(drive_engine_, endpoint_header);
                if (IoResult::kActive0 == result)
                {
                    endpoint_header->tx_sch_time = 0;
                    PUSH_TX_ACTIVE_QUEUE(active_txeps, active_txeps_0, endpoint_header);
                }
                else if (IoResult::kActive1 == result)
                {
                    if (endpoint_header->in_tx_resident_time())
                    {
                        ADK_ASSERT_SUCCESS(active_txeps_1->Push(endpoint_header));
                    }
                    else
                    {
                        drive_engine_->OnTxEndpointIdle(endpoint_header);
                    }
                }
            }

            if (0 == active_rxeps.length() + active_rxeps0_len + active_rxeps1_len
                   + active_txeps.length() + active_txeps0_len + active_txeps1_len)
            {
                //actor_load_ = 0;
                OnActorIdle<kIsLowLatency>(idle_counter);
            }
            else
            {
                if (!kIsLowLatency)
                {
                    idle_counter = 0;
                }

                /*
                actor_load_ = (active_rxeps_len << kRxActiveLoadWBits)
                            + (active_txeps_len << kTxActiveLoadWBits)
                            + (active_rxeps0_len << kRxActive0LoadWBits)
                            + (active_txeps0_len << kTxActive0LoadWBits)
                            + active_rxeps1_len + active_txeps1_len;
                            */
            }
        }
    }

    assert(s_actor_arena_);
    s_actor_arena_ = nullptr;

    tcp_stack->ReactorTerminated();
    drive_engine_->RemoveActorArena(&actor_arena);

    while (ErrorCode::kSuccess == active_txendpoints->Pop(endpoint_header))
    {
        drive_engine_->DropTxEndpoint(endpoint_header);
    }

    TxActiveEpQue::Delete(active_txendpoints);

    while (ErrorCode::kSuccess == active_txeps.TryPop(endpoint_header))
    {
        drive_engine_->DropTxEndpoint(endpoint_header);
    }

    while (ErrorCode::kSuccess == active_txeps_0->Pop(endpoint_header))
    {
        drive_engine_->DropTxEndpoint(endpoint_header);
    }

    ThreadLocalQueue<EndpointHeader*>::Delete(active_txeps_0);

    while (ErrorCode::kSuccess == active_txeps_1->Pop(endpoint_header))
    {
        drive_engine_->DropTxEndpoint(endpoint_header);
    }

    ThreadLocalQueue<EndpointHeader*>::Delete(active_txeps_1);
    
    while (ErrorCode::kSuccess == active_rxendpoints->Pop(endpoint_header))
    {
        drive_engine_->DropRxEndpoint(endpoint_header);
    }

    RxActiveEpQue::Delete(active_rxendpoints);

    while (ErrorCode::kSuccess == active_rxeps.TryPop(endpoint_header))
    {
        drive_engine_->DropRxEndpoint(endpoint_header);
    }

    while (ErrorCode::kSuccess == active_rxeps_0->Pop(endpoint_header))
    {
        drive_engine_->DropRxEndpoint(endpoint_header);
    }

    ThreadLocalQueue<EndpointHeader*>::Delete(active_rxeps_0);

    while (ErrorCode::kSuccess == active_rxeps_1->Pop(endpoint_header))
    {
        drive_engine_->DropRxEndpoint(endpoint_header);
    }

    ThreadLocalQueue<EndpointHeader*>::Delete(active_rxeps_1);
}

}

}
