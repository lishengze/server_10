#include "drive_engine.h"
#include "tcp_verbs/tcp_socket.h"
#if defined(__x86_64__)
#include "tcp_verbs/tcp_direct_zf.h"
#elif defined(__aarch64__)
#include "tcp_verbs/tcp_direct_zf_arm.h"
#endif

#include <iostream>

#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <adk/entry_wrapper.h>
#include <adk/io_engine/property.h>
#include <adk/io_engine/config_key.h>
#include <adk/io_engine/tcp_engine.h>

namespace adk_impl
{

namespace io_engine
{

DriveEngine::DriveEngine(TcpEngineImpl* const tcp_engine_impl)
{
    is_running_ = false;

    tx_delete_pipe_[0] = kInvalidFd;
    tx_delete_pipe_[1] = kInvalidFd;
    rx_delete_pipe_[0] = kInvalidFd;
    rx_delete_pipe_[1] = kInvalidFd;

    tx_epoller_ = nullptr;
    rx_epoller_ = nullptr;

    tcp_engine_impl_ = tcp_engine_impl;

    control_actor_ = nullptr;
    tx_selector_ = 0;
    rx_selector_ = 0;
    duplex_selector_ = 0;
    auto prefix = tcp_engine_impl_->engine_name();
    if (prefix.empty())
    {
        prefix = "adk";
    }

    tx_drive_name_ = prefix + "-ioe-txpoll";
    rx_drive_name_ = prefix + "-ioe-rxpoll";
}

int32_t DriveEngine::Init(const Property& props)
{
    Exit();

    control_actor_ = new ControlActor(this);
    if (ADK_UNLIKELY(ErrorCode::kSuccess != control_actor_->Init(props)))
    {
        control_actor_->Exit();
        delete control_actor_;
        control_actor_ = nullptr;

        return ErrorCode::kFailure;
    }

    auto* const tcp_stack_ptr = tcp_stack();
    assert(tcp_stack_ptr);

    if (ITcpStack::DriveMode::kPoller == tcp_stack_ptr->drive_mode())
    {
        ADK_ASSERT(pipe2(tx_delete_pipe_, O_NONBLOCK), 0);
        assert(tx_delete_pipe_[0] > 0);
        assert(tx_delete_pipe_[1] > 0);

        ADK_ASSERT(pipe2(rx_delete_pipe_, O_NONBLOCK), 0);
        assert(rx_delete_pipe_[0] > 0);
        assert(rx_delete_pipe_[1] > 0);

        tx_epoller_ = ITcpEPoller::Create(tcp_stack_ptr, ITcpEPoller::PollerType::kGeneral);
        assert(tx_epoller_);

        if (ADK_UNLIKELY(!tx_epoller_->EPollAddR(tx_delete_pipe_[0],
                                                 reinterpret_cast<void*>(tx_delete_pipe_[0]))))
        {
            return ErrorCode::kFailure;
        }

        rx_epoller_ = ITcpEPoller::Create(tcp_stack_ptr, ITcpEPoller::PollerType::kGeneral);
        assert(rx_epoller_);

        if (ADK_UNLIKELY(!rx_epoller_->EPollAddR(rx_delete_pipe_[0],
                                                 reinterpret_cast<void*>(rx_delete_pipe_[0]))))
        {
            return ErrorCode::kFailure;
        }
    }

    if (!tcp_stack_ptr->io_parallel_support())
    {
        auto* const duplex_actor = new DuplexActor(this);
        if (ADK_UNLIKELY(ErrorCode::kSuccess != duplex_actor->Init(props)))
        {
            duplex_actor->Exit();
            delete duplex_actor;
            return ErrorCode::kFailure;
        }

        duplex_actors_.push_back(duplex_actor);
        StartWorker(tcp_stack_ptr, true);
    }
    else
    {
        uint32_t tx_thread_num = props.GetValue(config::kTxThreadNum, default_value::kTxThreadNum);
        uint32_t rx_thread_num = props.GetValue(config::kRxThreadNum, default_value::kRxThreadNum);
        tx_thread_num = std::max<int32_t>(tx_thread_num, 1);
        rx_thread_num = std::max<int32_t>(rx_thread_num, 1);

        if (props.GetValue(config::kUseDuplexIOActor, default_value::kUseDuplexIOActor))
        {
            const auto thread_num = std::max<uint32_t>(tx_thread_num, rx_thread_num);
            for (uint32_t index = 0; index < thread_num; ++index)
            {
                auto* const duplex_actor = new DuplexActor(this);
                if (ADK_UNLIKELY(ErrorCode::kSuccess != duplex_actor->Init(props)))
                {
                    duplex_actor->Exit();
                    delete duplex_actor;
                    return ErrorCode::kFailure;
                }

                duplex_actors_.push_back(duplex_actor);
            }

            StartWorker(tcp_stack_ptr, true);
        }
        else
        {
            for (uint32_t tx_index = 0; tx_index < tx_thread_num; ++tx_index)
            {
                auto* const send_actor = new SendActor(this);
                if (ADK_UNLIKELY(ErrorCode::kSuccess != send_actor->Init(props)))
                {
                    send_actor->Exit();
                    delete send_actor;
                    return ErrorCode::kFailure;
                }

                send_actors_.push_back(send_actor);
            }

            for (uint32_t rx_index = 0; rx_index < rx_thread_num; ++rx_index)
            {
                auto* const recv_actor = new RecvActor(this);
                if (ADK_UNLIKELY(ErrorCode::kSuccess != recv_actor->Init(props)))
                {
                    recv_actor->Exit();
                    delete recv_actor;
                    return ErrorCode::kFailure;
                }

                recv_actors_.push_back(recv_actor);
            }

            StartWorker(tcp_stack_ptr, false);
        }
    }

    control_actor_->Start();

    bool in_multi_thread = (send_actors_.size() > 1);
    for (auto& actor_ptr : send_actors_)
    {
        actor_ptr->Start(in_multi_thread);
    }

    for (auto& actor_ptr : recv_actors_)
    {
        actor_ptr->Start();
    }

    in_multi_thread = (duplex_actors_.size() > 1);
    for (auto& actor_ptr : duplex_actors_)
    {
        actor_ptr->Start(in_multi_thread);
    }

    return ErrorCode::kSuccess;
}

void DriveEngine::Stop()
{
    is_running_ = false;

    if (tx_drive_hdl_.joinable())
    {
        tx_drive_hdl_.join();
    }

    if (rx_drive_hdl_.joinable())
    {
        rx_drive_hdl_.join();
    }

    for (auto& actor_ptr : duplex_actors_)
    {
        actor_ptr->Stop();
    }

    for (auto& actor_ptr : send_actors_)
    {
        actor_ptr->Stop();
    }

    for (auto& actor_ptr : recv_actors_)
    {
        actor_ptr->Stop();
    }

    control_actor_->Stop();

    if (kInvalidFd != tx_delete_pipe_[0])
    {
        close(tx_delete_pipe_[0]);
    }

    if (kInvalidFd != tx_delete_pipe_[1])
    {
        close(tx_delete_pipe_[1]);
    }

    if (kInvalidFd != rx_delete_pipe_[0])
    {
        close(rx_delete_pipe_[0]);
    }

    if (kInvalidFd != rx_delete_pipe_[1])
    {
        close(rx_delete_pipe_[1]);
    }

    if (nullptr != tx_epoller_)
    {
        tx_epoller_->Stop();
    }

    if (nullptr != rx_epoller_)
    {
        rx_epoller_->Stop();
    }
}

void DriveEngine::Exit()
{
    if (is_running_)
    {
        Stop();
    }

    for (auto& actor_ptr : send_actors_)
    {
        actor_ptr->Exit();
        delete actor_ptr;
    }
    send_actors_.clear();

    for (auto& actor_ptr : recv_actors_)
    {
        actor_ptr->Exit();
        delete actor_ptr;
    }
    recv_actors_.clear();

    for (auto& actor_ptr : duplex_actors_)
    {
        actor_ptr->Exit();
        delete actor_ptr;
    }
    duplex_actors_.clear();

    if (nullptr != control_actor_)
    {
        control_actor_->Exit();
        delete control_actor_;
        control_actor_ = nullptr;
    }

    if (nullptr != tx_epoller_)
    {
        ITcpEPoller::Destroy(tx_epoller_);
        tx_epoller_ = nullptr;
    }

    if (nullptr != rx_epoller_)
    {
        ITcpEPoller::Destroy(rx_epoller_);
        rx_epoller_ = nullptr;
    }

    tx_delete_pipe_[0] = kInvalidFd;
    tx_delete_pipe_[1] = kInvalidFd;
    rx_delete_pipe_[0] = kInvalidFd;
    rx_delete_pipe_[1] = kInvalidFd;
}

void DriveEngine::InsertActorArena(RecvActorArena* actor_arena)
{
    std::lock_guard<std::mutex> _(actor_area_lock_);
    actor_area_list_.push_back(actor_arena);
}

void DriveEngine::RemoveActorArena(RecvActorArena* actor_arena)
{
    std::lock_guard<std::mutex> _(actor_area_lock_);
    const auto iter = std::find(actor_area_list_.begin(), actor_area_list_.end(), actor_arena);
    if (actor_area_list_.end() != iter)
    {
        actor_area_list_.erase(iter);
    }
    else
    {
        assert(false);
    }
}

void DriveEngine::RxActorRCU(RecvActorArena* actor_arena)
{
    {
        std::lock_guard<std::mutex> _(actor_area_lock_);
        for (auto& arena_ptr : actor_area_list_)
        {
            if (actor_arena != arena_ptr)
            {
                arena_ptr->actor_rcu_counter = ACCESS_ONCE(arena_ptr->actor_counter);
            }
        }
    }

    do 
    {
        bool sync_complete = true;
        {
            std::lock_guard<std::mutex> _(actor_area_lock_);
            for (const auto& arena_ptr : actor_area_list_)
            {
                if (actor_arena != arena_ptr)
                {
                    if (arena_ptr->actor_rcu_counter == ACCESS_ONCE(arena_ptr->actor_counter))
                    {
                        sync_complete = false;
                        break;
                    }
                }
            }
        }

        if (sync_complete)
        {
            break;
        }

        usleep(0);
    } while (ACCESS_ONCE(is_running_));
}

int32_t DriveEngine::PauseRxActor()
{
    if (nullptr != control_actor_)
    {
        control_actor_->set_pause();
    }

    {
        std::lock_guard<std::mutex> _(actor_area_lock_);
        for (auto& arena_ptr : actor_area_list_)
        {
            arena_ptr->is_running = false;
            ADK_BARRIER();
            arena_ptr->actor_rcu_counter = ACCESS_ONCE(arena_ptr->actor_counter);
        }
    }

    auto* const actor_arena = IOActor::current_actor_arena();
    do
    {
        if (ADK_UNLIKELY(!ACCESS_ONCE(is_running_)))
        {
            return ErrorCode::kInvalidInvoke;
        }

        bool sync_complete = true;
        {
            std::lock_guard<std::mutex> _(actor_area_lock_);
            for (const auto& arena_ptr : actor_area_list_)
            {
                if (actor_arena != arena_ptr)
                {
                    if (arena_ptr->actor_rcu_counter == ACCESS_ONCE(arena_ptr->actor_counter))
                    {
                        sync_complete = false;
                        break;
                    }
                }
            }
        }

        if (sync_complete)
        {
            break;
        }

        usleep(0);
    } while (true);
    return ErrorCode::kSuccess;
}

void DriveEngine::ResumeRxActor()
{
    if (nullptr != control_actor_)
    {
        control_actor_->set_resume();
    }

    std::lock_guard<std::mutex> _(actor_area_lock_);
    for (auto& arena_ptr : actor_area_list_)
    {
        assert(arena_ptr);
        arena_ptr->is_running = true;
    }
}

void DriveEngine::StartWorker(verbs::ITcpStack* tcp_stack, bool duplex_actor)
{
    is_running_ = true;
    switch (tcp_stack->stack_type())
    {
    case ITcpStack::StackType::kStackSk:
        if (ITcpStack::DriveMode::kPoller == verbs::TcpStackSk::kDriveMode)
        {
            if (duplex_actor)
            {
                tx_drive_hdl_ = std_thread(
                    tx_drive_name_.c_str(),
                    "tx epoll thread",
                    std::bind(&DriveEngine::TxEpollThread<verbs::TcpEPollerSk, true>, this));

                rx_drive_hdl_ = std_thread(
                    rx_drive_name_.c_str(),
                    "rx epoll thread",
                    std::bind(&DriveEngine::RxEpollThread<verbs::TcpEPollerSk, true>, this));
            }
            else
            {
                tx_drive_hdl_ = std_thread(
                    tx_drive_name_.c_str(),
                    "tx epoll thread",
                    std::bind(&DriveEngine::TxEpollThread<verbs::TcpEPollerSk, false>, this));

                rx_drive_hdl_ = std_thread(
                    rx_drive_name_.c_str(),
                    "rx epoll thread",
                    std::bind(&DriveEngine::RxEpollThread<verbs::TcpEPollerSk, false>, this));
            }
        }
        break;
    case ITcpStack::StackType::kStackZf:
        if (ITcpStack::DriveMode::kPoller == verbs::TcpStackZf::kDriveMode)
        {
            assert(false);
            if (duplex_actor)
            {
                tx_drive_hdl_ = std_thread(
                     tx_drive_name_.c_str(),
                    "tx epoll thread",
                    std::bind(&DriveEngine::TxEpollThread<verbs::TcpEPollerZf, true>, this));

                rx_drive_hdl_ = std_thread(
                    rx_drive_name_.c_str(),
                    "rx epoll thread",
                    std::bind(&DriveEngine::RxEpollThread<verbs::TcpEPollerZf, true>, this));
            }
            else
            {
                tx_drive_hdl_ = std_thread(
                     tx_drive_name_.c_str(),
                    "tx epoll thread",
                    std::bind(&DriveEngine::TxEpollThread<verbs::TcpEPollerZf, false>, this));

                rx_drive_hdl_ = std_thread(
                    rx_drive_name_.c_str(),
                    "rx epoll thread",
                    std::bind(&DriveEngine::RxEpollThread<verbs::TcpEPollerZf, false>, this));
            }
        }
        break;
    default:
        assert(false);
    }
}

template<typename EPollerType, bool kDuplexActor>
void DriveEngine::TxEpollThread()
{
    char  pipe_buffer[kPipeReadBufferSize] = {0};

    // ACCESS_ONCE: avoid compiler optimize
    const auto delete_pipe_fd = tx_delete_pipe_[0];

    assert(tx_epoller_);
    EPollerType* const epoller = (EPollerType*)tx_epoller_;

    constexpr int32_t kMaxEventsSize = 128;
    struct epoll_event events[kMaxEventsSize];

    while (ACCESS_ONCE(is_running_))
    {
        const auto events_nr = epoller->EPollWait(events, kMaxEventsSize, kEpollTimeoutms);
        if (ADK_UNLIKELY(events_nr <= 0))
        {
            continue;
        }

        bool pipe_event = false;
        for (int32_t index = 0; index < events_nr; ++index)
        {
            const struct epoll_event& event_node = events[index];
            if (ADK_UNLIKELY(event_node.events & EPOLLIN))
            {
                if (reinterpret_cast<void*>(delete_pipe_fd) == event_node.data.ptr)
                {
                    pipe_event = true;
                    continue;
                }
            }

            EndpointHeader* const endpoint_header = (EndpointHeader*)(event_node.data.ptr);
            assert(endpoint_header);

            if (ADK_UNLIKELY(event_node.events & EPOLLERR))
            {
                DropTxEndpoint(endpoint_header);
            }
            else
            {
                auto* const send_actor_impl = endpoint_header->send_actor_impl;
                assert(send_actor_impl);

                endpoint_header->set_tx_active();
                send_actor_impl->AddJob(endpoint_header);
            }

            ADK_ASSERT_BOOL(epoller->EPollDel(endpoint_header->tcp_endpoint));

            ++endpoint_header->tx_dispatch2_nr;
        }

        if (ADK_UNLIKELY(pipe_event))
        {
            const auto len = read(delete_pipe_fd, pipe_buffer, kPipeReadBufferSize);
            DropPollingEndpoint(pipe_buffer, len, [=](EndpointHeader* const endpoint_header) {
                if (epoller->EPollDel(endpoint_header->tcp_endpoint))
                {
                    ++endpoint_header->tx_dispatch2_nr;
                    DropTxEndpoint(endpoint_header);
                }
            });
        }
    }
}

template<typename EPollerType, bool kDuplexActor>
void DriveEngine::RxEpollThread()
{
    char  pipe_buffer[kPipeReadBufferSize] = { 0 };

    // ACCESS_ONCE: avoid compiler optimize
    const auto delete_pipe_fd = rx_delete_pipe_[0];

    assert(rx_epoller_);
    EPollerType* const epoller = (EPollerType*)rx_epoller_;

    constexpr int32_t kMaxEventsSize = 128;
    struct epoll_event events[kMaxEventsSize];

    while (ACCESS_ONCE(is_running_))
    {
        const auto events_nr = epoller->EPollWait(events, kMaxEventsSize, kEpollTimeoutms);
        if (ADK_UNLIKELY(events_nr <= 0))
        {
            continue;
        }

        bool pipe_event = false;
        for (int32_t index = 0; index < events_nr; ++index)
        {
            const struct epoll_event& event_node = events[index];
            if (ADK_UNLIKELY(reinterpret_cast<void*>(delete_pipe_fd) == event_node.data.ptr))
            {
                pipe_event = true;
                continue;
            }

            EndpointHeader* const endpoint_header = (EndpointHeader*)(event_node.data.ptr);
            assert(endpoint_header);

            if (ADK_UNLIKELY(event_node.events & EPOLLERR))
            {
                auto* const tcp_endpoint = endpoint_header->tcp_endpoint;
                assert(tcp_endpoint);

                EventSocketError* event_socket_error = new EventSocketError("RxEpoll", 
                                                                            tcp_endpoint->LastError());

                assert(control_actor_);
                if(control_actor_->ToDeliverEvent<true>(endpoint_header, event_socket_error) != ErrorCode::kSuccess)
                {
                    delete event_socket_error;
                }
            }
            else
            {
                DispatchRxEndpoint<kDuplexActor>(endpoint_header);
            }

            ADK_ASSERT_BOOL(epoller->EPollDel(endpoint_header->tcp_endpoint));

            ++endpoint_header->rx_dispatch2_nr;
        }

        if (ADK_UNLIKELY(pipe_event))
        {
            const auto len = read(delete_pipe_fd, pipe_buffer, kPipeReadBufferSize);
            DropPollingEndpoint(pipe_buffer, len, [=](EndpointHeader* const endpoint_header) {
                if (epoller->EPollDel(endpoint_header->tcp_endpoint))
                {
                    ++endpoint_header->rx_dispatch2_nr;
                    DropRxEndpoint(endpoint_header);
                }
            });
        }
    }
}

}

}
