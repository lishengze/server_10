#include "io_control.h"
#include "event_impl.h"
#include "drive_engine.h"
#include "acceptor_impl.h"
#include "tcp_engine_impl.h"

#include <list>

#include <adk/entry_wrapper.h>
#include <adk/io_engine/config_key.h>

namespace adk_impl
{

namespace io_engine
{

using std::set;
using std::list;

ControlActor::ControlActor(DriveEngine* const drive_egine)
{
    is_running_ = false;
    is_paused_ = false;
    drive_engine_ = drive_egine;
    accept_tasks_  = nullptr;
    connect_tasks_ = nullptr;
    to_delete_eps_ = nullptr;
    immediate_task_ = nullptr;
    to_destroy_eps_ = nullptr;
    heartbeat_monitor_ = nullptr;
    async_event_infos_ = nullptr;

    accepted_eps_nr_ = 0;

    connect_cancel_nr_ = 0;
    connect_failed_nr_ = 0;
    connected_eps_nr_ = 0;
}

int32_t ControlActor::Init(const Property& props)
{
    Exit();

    const uint32_t max_connection = props.GetValue(config::kMaxConnections,
                                                   default_value::kMaxConnections);

    accept_tasks_ = MPSCQueue<AcceptTask*>::Create("accept_task", max_connection);
    assert(accept_tasks_);

    connect_tasks_ = MPSCQueue<ConnectTask*>::Create("connect_task", max_connection);
    assert(connect_tasks_);

    to_delete_eps_ = MPSCQueue<TcpEndpoint*>::Create("to_delete_eps", max_connection);
    assert(to_delete_eps_);

    to_destroy_eps_ = SPSCQueue<EndpointHeader*>::Create("to_destroy_eps", max_connection);
    assert(to_destroy_eps_);

    immediate_task_ = MPSCQueue<ImmediateTask>::Create("imm_task", max_connection);
    assert(immediate_task_);

    heartbeat_monitor_ = MPSCQueue<HeartbeatNode>::Create("heartbeat monitor", max_connection);
    assert(heartbeat_monitor_);

    async_event_infos_ = MPSCQueue<AsyncEventInfo>::Create("async event info", max_connection);
    assert(async_event_infos_);

    auto prefix = drive_engine_->tcp_engine_impl()->engine_name();
    if (prefix.empty())
    {
        prefix = "adk";
    }

    name_ = prefix + "-ioe-ctrlactor";
    return ErrorCode::kSuccess;
}

void ControlActor::Start()
{
    is_running_ = true;
    is_paused_ = false;
    thread_hdl_ = std_thread(name_.c_str(),
                             "io engine control actor thread",
                             std::bind(&ControlActor::ActorThread, this));
}

void ControlActor::Stop()
{
    is_running_ = false;

    if (thread_hdl_.joinable())
    {
        thread_hdl_.join();
    }

    assert(immediate_task_);
    struct VariantEntry* entry_ptr;
    while (ErrorCode::kSuccess == immediate_task_->WaitEntry(&entry_ptr))
    {
        char* const buffer = entry_ptr->buffer;
        TcpEndpoint* const endpoint_impl = ((ImmediateTask*)buffer)->endpoint_impl;
        endpoint_impl->sub_reference();
        immediate_task_->FreeEntry(entry_ptr);
    }

    assert(drive_engine_);
    auto* const tcp_engine_impl = drive_engine_->tcp_engine_impl();
    assert(tcp_engine_impl);

    assert(connect_tasks_);
    ConnectTask* connect_task = nullptr;
    while (ErrorCode::kSuccess == connect_tasks_->Pop(connect_task))
    {
        assert(connect_task);

        if (TaskType::kAsynConnect == connect_task->task_type)
        {
            tcp_engine_impl->DestroyInproEndpoint(connect_task->endpoint_header,
                                                  connect_task->remote_ip,
                                                  connect_task->remote_port);
        }

        delete connect_task;
    }

    assert(accept_tasks_);
    AcceptTask* accept_task = nullptr;
    while (ErrorCode::kSuccess == accept_tasks_->Pop(accept_task))
    {
        assert(accept_task);
        auto* const accept_impl = accept_task->acceptor_impl;
        assert(accept_impl);

        if (accept_impl->valid())
        {
            accept_impl->Exit();
        }
        else
        {
            TcpAcceptor::Destroy(accept_impl);
        }

        delete accept_task;
    }

    assert(heartbeat_monitor_);
    while (ErrorCode::kSuccess == heartbeat_monitor_->WaitEntry(&entry_ptr))
    {
        char* const buffer = entry_ptr->buffer;
        DropCtrlEndpoint(((HeartbeatNode*)buffer)->endpoint_header);
        heartbeat_monitor_->FreeEntry(entry_ptr);
    }

    // the actor thread is the last thread to exit
    DoEpsDelete();

    // destroy endpoints
    DoForceEpsDestroy();
}

void ControlActor::Exit()
{
    if (is_running_)
    {
        Stop();
    }

    if (nullptr != accept_tasks_)
    {
        MPSCQueue<AcceptTask*>::Delete(accept_tasks_);
        accept_tasks_ = nullptr;
    }

    if (nullptr != connect_tasks_)
    {
        MPSCQueue<ConnectTask*>::Delete(connect_tasks_);
        connect_tasks_ = nullptr;
    }

    if (nullptr != to_delete_eps_)
    {
        assert(0 == to_delete_eps_->length());
        MPSCQueue<TcpEndpoint*>::Delete(to_delete_eps_);
        to_delete_eps_ = nullptr;
    }    

    if (nullptr != immediate_task_)
    {
        MPSCQueue<ImmediateTask>::Delete(immediate_task_);
        immediate_task_ = nullptr;
    }

    if (nullptr != to_destroy_eps_)
    {
        SPSCQueue<EndpointHeader*>::Delete(to_destroy_eps_);
        to_destroy_eps_ = nullptr;
    }

    if (nullptr != heartbeat_monitor_)
    {
        MPSCQueue<HeartbeatNode>::Delete(heartbeat_monitor_);
        heartbeat_monitor_ = nullptr;
    }

    if (nullptr != async_event_infos_)
    {
        VariantEntry* entry_ptr;
        while (ErrorCode::kSuccess == async_event_infos_->WaitEntry(&entry_ptr))
        {
            char* const buffer = entry_ptr->buffer;
            assert(nullptr != ((AsyncEventInfo*)buffer)->event);
            delete ((AsyncEventInfo*)buffer)->event;
            async_event_infos_->FreeEntry(entry_ptr);
        }

        MPSCQueue<AsyncEventInfo>::Delete(async_event_infos_);
        async_event_infos_ = nullptr;
    }
}

int32_t ControlActor::ToAccept(AcceptTask* const accept_task)
{
    assert(accept_tasks_);
    return accept_tasks_->TryPush(accept_task);
}

int32_t ControlActor::ToConnect(ConnectTask* const connect_task)
{
    assert(connect_tasks_);
    return connect_tasks_->TryPush(connect_task);
}

int32_t ControlActor::ToDelete(TcpEndpoint* const endpoint_impl)
{
    assert(to_delete_eps_);
    return to_delete_eps_->Push(endpoint_impl);
}

int32_t ControlActor::ToDelete(EndpointHeader* const endpoint_header)
{
    assert(to_destroy_eps_);

    //这里用phy_time表示endpoint_header排队销毁的起始时间。1秒后，可以销毁
    endpoint_header->set_phy_time();
    return to_destroy_eps_->Push(endpoint_header);
}

int32_t ControlActor::ToCallbackImmediate(TcpEndpoint* const endpoint, uint32_t task_type)
{
    assert(immediate_task_);

    VariantEntry* entry_ptr;
    const auto ec = immediate_task_->TryAllocEntry(&entry_ptr);
    if (ADK_UNLIKELY(ErrorCode::kSuccess != ec))
    {
        return ec;
    }

    char* const buffer = entry_ptr->buffer;
    ((ImmediateTask*)buffer)->endpoint_impl = endpoint;
    ((ImmediateTask*)buffer)->task_type = task_type;
    immediate_task_->PostEntry(entry_ptr);
    return ErrorCode::kSuccess;
}

void ControlActor::AcceptorIndicator(boost::property_tree::ptree& acceptor_ptree)
{
    acceptor_ptree.put("total_hist_nr", accept_tasks_->last_push_sqn());
    acceptor_ptree.put("gen_eps_nr", accepted_eps_nr_);
}

void ControlActor::ConnectIndicator(boost::property_tree::ptree& connect_ptree)
{
    connect_ptree.put("total_hist_nr", connect_tasks_->last_push_sqn());
    connect_ptree.put("gen_eps_nr", connected_eps_nr_);
    connect_ptree.put("canceled_nr", connect_cancel_nr_);
    connect_ptree.put("failed_nr", connect_failed_nr_);
}

void ControlActor::ActorThread()
{
    assert(drive_engine_);
    assert(accept_tasks_);
    assert(connect_tasks_);
    assert(immediate_task_);

    TcpEngineImpl* const tcp_engine_impl = drive_engine_->tcp_engine_impl();
    assert(tcp_engine_impl);

    const auto task_queue_size = tcp_engine_impl->engine_capacity();

    auto connect_tasks_inpro = 
        ThreadLocalQueue<ConnectTask*>::Create("connect task progressing", task_queue_size);
    auto connect_tasks_retry = 
        ThreadLocalQueue<ConnectTask*>::Create("connect task retrying", task_queue_size);
    auto accept_tasks_inpro = 
        ThreadLocalQueue<AcceptTask*>::Create("accept tasks progressing", task_queue_size);

    VariantEntry* entry_ptr = nullptr;
    AcceptTask*   accept_task = nullptr;
    ConnectTask*  connect_task = nullptr;

    EventConnectFail connect_fail_event;
    std::set<EndpointHeader*> drop_eps_set;
    constexpr uint32_t kMaxControlEvents = 1024;
    struct epoll_event wait_events[kMaxControlEvents];

    ITcpEPoller* const epoller = ITcpEPoller::Create(drive_engine_->tcp_stack(), 
                                                     ITcpEPoller::PollerType::kControl);
    assert(epoller);

    const uint32_t idle_sleep_us = 200;//adk_impl::IsEnvSetLowUtilization() ? 200 : 0;
    while (ACCESS_ONCE(is_running_))
    {
        ///> 1. callback immdiate task
        while (ErrorCode::kSuccess == immediate_task_->WaitEntry(&entry_ptr))
        {
            char* const buffer = entry_ptr->buffer;
            PerformTask((ImmediateTask*)buffer);
            immediate_task_->FreeEntry(entry_ptr);
        }

        ///> 2. deal new connect tasks
        while (ErrorCode::kSuccess == connect_tasks_->Pop(connect_task))
        {
            assert(connect_task);

            EndpointHeader* const endpoint_header = connect_task->endpoint_header;
            assert(endpoint_header);

            ITcpEndpoint* const tcp_endpoint = endpoint_header->tcp_endpoint;
            assert(tcp_endpoint);

            const auto result = tcp_endpoint->Connect(connect_task->remote_ip,
                                                      connect_task->remote_port);
            if (static_cast<int32_t>(ITcpEndpoint::ConnectResult::kSuccess) == result)
            {
                OnConnect(endpoint_header);
                delete connect_task;
            }
            else if (static_cast<int32_t>(ITcpEndpoint::ConnectResult::kInProgress) == result)
            {
                ADK_ASSERT_SUCCESS(connect_tasks_inpro->Push(connect_task));
                ADK_ASSERT_BOOL(epoller->EPollAdd(tcp_endpoint, connect_task));
            }
            else
            {
                ++connect_task->retry_times;
                endpoint_header->set_phy_time();
                ADK_ASSERT_SUCCESS(connect_tasks_retry->Push(connect_task));
            }
        }

        ///> 3. deal new accept task
        while (ErrorCode::kSuccess == accept_tasks_->Pop(accept_task))
        {
            assert(accept_task);
            auto* const acceptor_impl = accept_task->acceptor_impl;
            assert(acceptor_impl);

            if (acceptor_impl->valid())
            {
                auto* const tcp_acceptor = acceptor_impl->tcp_acceptor();
                assert(tcp_acceptor);

                ADK_ASSERT_SUCCESS(accept_tasks_inpro->Push(accept_task));
                ADK_ASSERT_BOOL(epoller->EPollAdd(tcp_acceptor, accept_task));
            }
            else
            {
                // acceptor closed
                TcpAcceptor::Destroy(acceptor_impl);
                delete accept_task;
            }
        }

        ///> 4. check complete and cancled connect task
        const auto inpro_tasks_len = connect_tasks_inpro->length();
        for (uint64_t index = 0; index < inpro_tasks_len; ++index)
        {
            ADK_ASSERT_SUCCESS(connect_tasks_inpro->Pop(connect_task));
            assert(connect_task);

            // has forward to retry task queue
            if (connect_task->is_retry_forward())
            {
                continue;
            }

            // task complete
            if (connect_task->is_complete())
            {
                delete connect_task;
                continue;
            }

            auto* const endpoint_header = connect_task->endpoint_header;
            assert(endpoint_header);

            // task canceled
            if (ADK_UNLIKELY(!endpoint_header->is_connecting()))
            {
                ADK_ASSERT_BOOL(epoller->EPollDel(endpoint_header->tcp_endpoint));
                DropEndpoint(endpoint_header);
                ++connect_cancel_nr_;
                delete connect_task;
                continue;
            }

            ADK_ASSERT_SUCCESS(connect_tasks_inpro->Push(connect_task));
        }

        ///> 5. check and retry connect tasks
        const auto retry_tasks_len = connect_tasks_retry->length();
        for (uint64_t index = 0; index < retry_tasks_len; ++index)
        {
            ADK_ASSERT_SUCCESS(connect_tasks_retry->Pop(connect_task));
            assert(connect_task);

            auto* const endpoint_header = connect_task->endpoint_header;
            assert(endpoint_header);

            // connect task was canceled
            if (ADK_UNLIKELY(!endpoint_header->is_connecting()))
            {
                DropEndpoint(endpoint_header);
                ++connect_cancel_nr_;
                delete connect_task;
                continue;
            }

            // check retry times
            if (connect_task->retry_times < connect_task->limit_times)
            {
                const uint64_t last_timer = endpoint_header->phy_time;
                const uint64_t current_tp = endpoint_header->GetTimepoint();

                // check interval time
                if (last_timer + connect_task->interval_us < current_tp)
                {
                    ITcpEndpoint* const tcp_endpoint = endpoint_header->tcp_endpoint;
                    assert(tcp_endpoint);

                    const auto result = tcp_endpoint->Connect(connect_task->remote_ip,
                                                              connect_task->remote_port);
                    if (static_cast<int32_t>(ITcpEndpoint::ConnectResult::kSuccess) == result)
                    {
                        OnConnect(endpoint_header);
                        delete connect_task;
                    }
                    else if (static_cast<int32_t>(ITcpEndpoint::ConnectResult::kInProgress) == result)
                    {
                        ADK_ASSERT_SUCCESS(connect_tasks_inpro->Push(connect_task));
                        ADK_ASSERT_BOOL(epoller->EPollAdd(tcp_endpoint, connect_task));
                    }
                    else
                    {
                        if (++connect_task->retry_times < connect_task->limit_times)
                        {
                            endpoint_header->set_phy_time();
                            ADK_ASSERT_SUCCESS(connect_tasks_retry->Push(connect_task));
                        }
                        else
                        {
                            goto task_failed;
                        }
                    }
                }
                else
                {
                    ADK_ASSERT_SUCCESS(connect_tasks_retry->Push(connect_task));
                }
            }
            else
            {
            task_failed:
                ++connect_failed_nr_;
                DropEndpoint(endpoint_header);
                TcpEndpoint::DeliverErrorEvent(endpoint_header, &connect_fail_event);
                delete connect_task;
            }
        }

        ///> 6. check cancled accept task
        const auto accept_tasks_len = accept_tasks_inpro->length();
        for (uint64_t index = 0; index < accept_tasks_len; ++index)
        {
            ADK_ASSERT_SUCCESS(accept_tasks_inpro->Pop(accept_task));
            assert(accept_task);

            auto* const acceptor_impl = accept_task->acceptor_impl;
            assert(acceptor_impl);

            if (acceptor_impl->valid())
            {
                ADK_ASSERT_SUCCESS(accept_tasks_inpro->Push(accept_task));
            }
            else
            {
                auto* const tcp_acceptor = acceptor_impl->tcp_acceptor();
                assert(tcp_acceptor);

                ADK_ASSERT_BOOL(epoller->EPollDel(tcp_acceptor));
                TcpAcceptor::Destroy(acceptor_impl);
                delete accept_task;
            }
        }

        if ((connect_tasks_inpro->length() > 0) || (accept_tasks_inpro->length() > 0))
        {
            const auto events_nr = epoller->EPollWait(wait_events, kMaxControlEvents, kEpollTimeoutms);
            for (int32_t index = 0; index < events_nr; ++index)
            {
                struct epoll_event& ep_event = wait_events[index];
                assert(ep_event.data.ptr);

                TaskBase* const task_base = (TaskBase*)(ep_event.data.ptr);
                switch (task_base->task_type)
                {
                case TaskType::kAsynConnect:
                    {
                        auto* const connect_task = (ConnectTask*)(ep_event.data.ptr);
                        auto* const endpoint_header = connect_task->endpoint_header;
                        assert(endpoint_header);

                        auto* const tcp_endpoint = endpoint_header->tcp_endpoint;
                        assert(tcp_endpoint);

                        if (tcp_endpoint->EPollResult(ep_event.events))
                        {
                            OnConnect(endpoint_header);
                            connect_task->complete();
                        }
                        else
                        {
                            ++(connect_task->retry_times);
                            connect_task->retry_forward();
                            endpoint_header->set_phy_time();
                            ADK_ASSERT_SUCCESS(connect_tasks_retry->Push(connect_task));
                        }

                        ADK_ASSERT_BOOL(epoller->EPollDel(tcp_endpoint));
                    }
                    break;
                case TaskType::kAsynAccept:
                    if (ITcpAcceptor::EPollResult(ep_event.events))
                    {
                        AcceptTask* const accept_task = (AcceptTask*)(ep_event.data.ptr);
                        TcpAcceptor* const acceptor_impl = accept_task->acceptor_impl;
                        assert(acceptor_impl);

                    retry_accept:
                        TcpEndpoint * const endpoint_impl = acceptor_impl->Accept();
                        if (nullptr != endpoint_impl)
                        {
                            OnAccept(acceptor_impl, endpoint_impl);
                            goto retry_accept;
                        }
                    }
                    else
                    {
                        assert(false);
                    }
                    break;
                default:
                    assert(false);
                }
            }
        }

        // check the to delete endpoints is released
        DoEpsDelete();

        // do deliver async event
        DoDeliverEvent(drop_eps_set);

        // diagnose endpoint
        DiagnoseHeartbeat(drop_eps_set);

        // destroy endpoints
        DoEpsDestroy();

        usleep(idle_sleep_us);
    }

    while (ErrorCode::kSuccess == connect_tasks_inpro->Pop(connect_task))
    {
        assert(connect_task);

        /**
         * check current task is forward, avoid double free
         */
        if (connect_task->is_retry_forward())
        {
            continue;
        }

        if (connect_task->is_complete())
        {
            delete connect_task;
            continue;
        }

        tcp_engine_impl->DestroyInproEndpoint(connect_task->endpoint_header, 
                                              connect_task->remote_ip, 
                                              connect_task->remote_port);

        delete connect_task;
    }

    ThreadLocalQueue<ConnectTask*>::Delete(connect_tasks_inpro);

    while (ErrorCode::kSuccess == connect_tasks_retry->Pop(connect_task))
    {
        assert(connect_task);

        tcp_engine_impl->DestroyInproEndpoint(connect_task->endpoint_header, 
                                              connect_task->remote_ip,
                                              connect_task->remote_port);

        delete connect_task;
    }

    ThreadLocalQueue<ConnectTask*>::Delete(connect_tasks_retry);

    while (ErrorCode::kSuccess == accept_tasks_inpro->Pop(accept_task))
    {
        assert(accept_task);

        auto* const acceptor_impl = accept_task->acceptor_impl;
        assert(acceptor_impl);

        if (acceptor_impl->valid())
        {
            acceptor_impl->Exit();
        }
        else
        {
            TcpAcceptor::Destroy(acceptor_impl);
        }

        delete accept_task;
    }

    ThreadLocalQueue<AcceptTask*>::Delete(accept_tasks_inpro);
}

void ControlActor::WorkerThread()
{
#if 0
    fd_set read_set;
    fd_set write_set;
    VariantEntry* entry_ptr;

    EventConnectFail connect_fail_event;

    deque<ConnectTask>::iterator conn_iter;
    deque<ConnectTask> connect_tasks_inpro;
    deque<ConnectTask> connect_tasks_retry;

    deque<AcceptTask>::iterator acpt_iter;
    deque<AcceptTask> accept_tasks_inpro;

    int32_t max_fd = 0;
    uint32_t idle_counter = 0;
    struct timeval select_timeout = { 0, kEstablishSelectTimeoutus };

    while (ACCESS_ONCE(is_running_))
    {
        ///> callback immdiate task
        while (ErrorCode::kSuccess == immediate_task_->WaitEntry(&entry_ptr))
        {
            char* const buffer = entry_ptr->buffer;
            PerformTask((ImmediateTask*)buffer);
            immediate_task_->FreeEntry(entry_ptr);
            idle_counter = 0;
        }

        max_fd = 0;
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);

        // new connect tasks
        while (ErrorCode::kSuccess == connect_tasks_->WaitEntry(&entry_ptr))
        {
            char* const buffer = entry_ptr->buffer;
            ConnectTask& connect_task = *((ConnectTask*)buffer);
            EndpointImpl* const endpoint_impl = (EndpointImpl*)(connect_task.endpoint);

            const string& remote_ip = endpoint_impl->remote_ip();
            const uint16_t remote_port = endpoint_impl->remote_port();
            ITcpEndpoint* const tcp_endpoint = endpoint_impl->tcp_endpoint();

            const auto result = tcp_endpoint->Connect(remote_ip, remote_port);
            if (AsyncResult::kAsPass == result)
            {
                OnConnect(endpoint_impl);
            }
            else if (AsyncResult::kAsInpro == result)
            {
                const int32_t endpoint_id = endpoint_impl->endpoint_id();
                FD_SET(endpoint_id, &write_set);
                max_fd = std::max<int32_t>(max_fd, endpoint_id);
                connect_tasks_inpro.push_back(connect_task);
            }
            else
            {
                ++connect_task.retry_times;
                endpoint_impl->set_scheduler_timer();
                connect_tasks_retry.push_back(connect_task);
            }
            connect_tasks_->FreeEntry(entry_ptr);
        }

        while (ErrorCode::kSuccess == accept_tasks_->WaitEntry(&entry_ptr))
        {
            char* const buffer = entry_ptr->buffer;
            AcceptTask& accept_task = *((AcceptTask*)buffer);
            EndpointImpl* const endpoint_impl = accept_task.endpoint;
            assert(endpoint_impl);

            ITcpEndpoint* const tcp_endpoint = accept_task.accepter->Accept();
            if (nullptr == tcp_endpoint)
            {
                const int32_t acpt_fd = accept_task.accepter->sock_fd();
                FD_SET(acpt_fd, &read_set);
                max_fd = std::max<int32_t>(max_fd, acpt_fd);
                accept_tasks_inpro.push_back(accept_task);
            }
            else
            {
                OnAccept(endpoint_impl, tcp_endpoint, accept_task.accepter);
            }

            accept_tasks_->FreeEntry(entry_ptr);
        }

        // retry connect tasks
        conn_iter = connect_tasks_retry.begin();
        while (conn_iter != connect_tasks_retry.end())
        {
            CheckInproEpStatus(*conn_iter);
            EndpointImpl* const endpoint_impl = conn_iter->endpoint;
            if (nullptr == endpoint_impl)
            {
                conn_iter = connect_tasks_retry.erase(conn_iter);
                continue;
            }

            // check interval times
            if (conn_iter->retry_times < conn_iter->limit_times)
            {
                const uint64_t current_tp = endpoint_impl->GetTimepoint();
                const uint64_t last_timer = endpoint_impl->scheduler_timer();
                if (last_timer + conn_iter->interval_us < current_tp)
                {
                    const string& remote_ip = endpoint_impl->remote_ip();
                    const uint16_t remote_port = endpoint_impl->remote_port();
                    ITcpEndpoint* const tcp_endpoint = endpoint_impl->tcp_endpoint();
                    const auto result = tcp_endpoint->Connect(remote_ip, remote_port);
                    if (AsyncResult::kAsPass == result)
                    {
                        OnConnect(endpoint_impl);                        
                        conn_iter = connect_tasks_retry.erase(conn_iter);
                        continue;
                    }
                    else if (AsyncResult::kAsInpro == result)
                    {
                        const int32_t endpoint_id = endpoint_impl->endpoint_id();
                        FD_SET(endpoint_id, &write_set);
                        max_fd = std::max<int32_t>(max_fd, endpoint_id);
                        connect_tasks_inpro.push_back(*conn_iter);
                        conn_iter = connect_tasks_retry.erase(conn_iter);
                        continue;
                    }
                    else
                    {
                        endpoint_impl->set_scheduler_timer();
                        ++(conn_iter->retry_times);
                    }                    
                }                  
            }
            else
            {
                endpoint_impl->OnEventErrorChain(&connect_fail_event);
                endpoint_impl->release();
                conn_iter = connect_tasks_retry.erase(conn_iter);
                continue;
            }

            ++conn_iter;
        }

        // check close
        conn_iter = connect_tasks_inpro.begin();
        while (conn_iter != connect_tasks_inpro.end())
        {
            EndpointImpl* const endpoint_impl = conn_iter->endpoint;
            const int32_t endpoint_id = endpoint_impl->endpoint_id();

            CheckInproEpStatus(*conn_iter);
            if (nullptr == conn_iter->endpoint)
            {
                conn_iter = connect_tasks_inpro.erase(conn_iter);
            }
            else
            {
                ++conn_iter;
                FD_SET(endpoint_id, &write_set);
                max_fd = std::max<int32_t>(max_fd, endpoint_id);
            }
        }

        // check the socket closed by application
        acpt_iter = accept_tasks_inpro.begin();
        while (acpt_iter != accept_tasks_inpro.end())
        {
            ITcpServer* const accepter = acpt_iter->accepter;
            const int32_t acpt_fd = accepter->sock_fd();

            CheckInproEpStatus(*acpt_iter);
            if (ADK_UNLIKELY(nullptr == acpt_iter->endpoint))
            {
                acpt_iter = accept_tasks_inpro.erase(acpt_iter);
            }
            else
            {
                ++acpt_iter;
                FD_SET(acpt_fd, &read_set);
                max_fd = std::max<int32_t>(max_fd, acpt_fd);
            }
        }
        
        if (max_fd > 0)
        {
            const auto select_result = select(max_fd + 1, &read_set, &write_set, nullptr, &select_timeout);
            if (select_result > 0)
            {
                int32_t optval;
                socklen_t optlen = sizeof(int32_t);
                conn_iter = connect_tasks_inpro.begin();
                while (conn_iter != connect_tasks_inpro.end())
                {
                    ConnectTask& connect_task = *conn_iter;
                    EndpointImpl* const endpoint_impl = connect_task.endpoint;
                    const int32_t endpoint_id = endpoint_impl->endpoint_id();
                    if (FD_ISSET(endpoint_id, &write_set))
                    {
                        optval = -1;
                        getsockopt(endpoint_id, SOL_SOCKET, SO_ERROR, &optval, &optlen);
                        if (0 == optval)
                        {
                            OnConnect(endpoint_impl);
                        }
                        else
                        {
                            endpoint_impl->set_scheduler_timer();
                            ++connect_task.retry_times;
                            connect_tasks_retry.push_back(connect_task);
                        }

                        conn_iter = connect_tasks_inpro.erase(conn_iter);
                    }
                    else
                    {
                        ++conn_iter;
                        max_fd = std::max<int32_t>(max_fd, endpoint_id);
                    }
                }

                acpt_iter = accept_tasks_inpro.begin();
                while (acpt_iter != accept_tasks_inpro.end())
                {
                    AcceptTask& accept_task = *acpt_iter;
                    const int32_t accepter_fd = accept_task.accepter->sock_fd();
                    if (FD_ISSET(accepter_fd, &read_set))
                    {
                        ITcpEndpoint* const tcp_endpoint = accept_task.accepter->Accept();
                        assert(tcp_endpoint);

                        OnAccept(accept_task.endpoint, tcp_endpoint, accept_task.accepter);
                        acpt_iter = accept_tasks_inpro.erase(acpt_iter);
                    }
                    else
                    {
                        ++acpt_iter;
                        max_fd = std::max<int32_t>(max_fd, accepter_fd);
                    }
                }

                idle_counter = 0;
                continue;
            }
        }

        DoEpsDelete();
        DoEpsDestroy();

        if (++idle_counter > kMaxIdleLoopCounter)
        {
            usleep(0);
        }
    }

    for (conn_iter = connect_tasks_inpro.begin(); conn_iter != connect_tasks_inpro.end(); ++conn_iter)
    {
        EndpointImpl* const endpoint_impl = conn_iter->endpoint;
        TcpEngineImpl* const tcp_engine_impl = endpoint_impl->tcp_engine_impl();
        assert(tcp_engine_impl);
        tcp_engine_impl->DestroyInproEndpoint(endpoint_impl);
    }

    for (conn_iter = connect_tasks_retry.begin(); conn_iter != connect_tasks_retry.end(); ++conn_iter)
    {
        EndpointImpl* const endpoint_impl = conn_iter->endpoint;
        TcpEngineImpl* const tcp_engine_impl = endpoint_impl->tcp_engine_impl();
        assert(tcp_engine_impl);
        tcp_engine_impl->DestroyInproEndpoint(endpoint_impl);
    }

    for (acpt_iter = accept_tasks_inpro.begin(); acpt_iter != accept_tasks_inpro.end(); ++acpt_iter)
    {
        EndpointImpl* const endpoint_impl = acpt_iter->endpoint;
        TcpEngineImpl* const tcp_engine_impl = endpoint_impl->tcp_engine_impl();
        assert(tcp_engine_impl);
        tcp_engine_impl->DestroyEndpoint(endpoint_impl);
    }
#endif
}

void ControlActor::OnConnect(EndpointHeader* const endpoint_header)
{
    assert(drive_engine_);
    assert(endpoint_header);

    ++connected_eps_nr_;
    if (ErrorCode::kSuccess == TcpEndpoint::OnConnect(endpoint_header))
    {
        endpoint_header->set_established();

        if (ErrorCode::kSuccess == drive_engine_->RegEndpoint(endpoint_header))
        {
            ToMonitorEndpoint(endpoint_header);
        }
        else
        {
            DropCtrlEndpoint(endpoint_header);
        }
    }
    else
    {
        DropEndpoint(endpoint_header);
    }
}

void ControlActor::OnAccept(TcpAcceptor* const acceptor_impl, TcpEndpoint* const endpoint_impl)
{
    TcpEngineImpl* const tcp_engine_impl = drive_engine_->tcp_engine_impl();
    assert(tcp_engine_impl);

    EndpointHeader* const endpoint_header = endpoint_impl->endpoint_header();
    tcp_engine_impl->RegisterEndpoint(endpoint_header);

    ++accepted_eps_nr_;

    acceptor_impl->accept_lock();
    const auto accept_ec = endpoint_impl->OnAccept(acceptor_impl->accept_handler());
    acceptor_impl->accept_unlock();

    if (ErrorCode::kSuccess == accept_ec)
    {
        endpoint_header->set_established();

        if (ErrorCode::kSuccess == drive_engine_->RegEndpoint(endpoint_header))
        {
            ToMonitorEndpoint(endpoint_header);
        }
        else
        {
            DropCtrlEndpoint(endpoint_header);
        }
    }
    else
    {
        DropEndpoint(endpoint_header);
    }
}

void ControlActor::DoEpsDelete()
{
    assert(to_delete_eps_);
    const uint64_t to_delete_len = to_delete_eps_->length();

    TcpEndpoint* endpoint_impl = nullptr;
    for (uint64_t index = 0; index < to_delete_len; ++index)
    {
        // mpsc queue length is not real time exact
        if (ADK_UNLIKELY(ErrorCode::kSuccess != to_delete_eps_->Pop(endpoint_impl)))
        {
            break;
        }

        TcpEngineImpl* const tcp_engine_impl = endpoint_impl->tcp_engine_impl();
        if (ADK_UNLIKELY(ErrorCode::kSuccess != tcp_engine_impl->DestroyEndpoint(endpoint_impl)))
        {
            ADK_ASSERT_SUCCESS(to_delete_eps_->Push(endpoint_impl));
        }
    }
}

void ControlActor::DoDeliverEvent(std::set<EndpointHeader*>& droped_eps_set)
{
    droped_eps_set.clear();
    assert(async_event_infos_);

    VariantEntry* entry_ptr;
    while (ErrorCode::kSuccess == async_event_infos_->WaitEntry(&entry_ptr))
    {
        char* const buffer = entry_ptr->buffer;

        auto* const endpoint_header = ((AsyncEventInfo*)buffer)->endpoint_header;
        auto* const event = ((AsyncEventInfo*)buffer)->event;

        switch (event->level())
        {
        case EventLevel::kInfo:
            TcpEndpoint::DeliverEvent(endpoint_header, event);
            break;
        case EventLevel::kWarn:
            TcpEndpoint::DeliverWarnEvent(endpoint_header, event);
            break;
        case EventLevel::kError:
        case EventLevel::kFatal:
            DropCtrlEndpoint(endpoint_header);
            droped_eps_set.insert(endpoint_header);
            
            if (((AsyncEventInfo*)buffer)->rx_release_action)
            {
                endpoint_header->set_rx_release();
            }
            else
            {
                endpoint_header->set_tx_release();
            }

            TcpEndpoint::DeliverErrorEvent(endpoint_header, event);

            /**
             * check endpoint release status after deliver event
             * 
             * release endpoint physical resources
             * 
             * reserved property interface callable
             */
            if (!endpoint_header->tx_status_lock)
            {
                endpoint_header->atomic_set_tx_release();
            }

            if (endpoint_header->is_release())
            {
                auto* const tcp_endpoint = endpoint_header->tcp_endpoint;
                assert(tcp_endpoint);

                tcp_endpoint->Close();
            }
            break;
        default:
            assert(false);
        }

        delete event;

        async_event_infos_->FreeEntry(entry_ptr);
    }
}

void ControlActor::DoEpsDestroy()
{
    assert(to_destroy_eps_);
    EndpointHeader* endpoint_header = nullptr;

    const uint64_t to_destroy_len = to_destroy_eps_->length();
    for (uint64_t index = 0; index < to_destroy_len; ++index)
    {
        if (ADK_UNLIKELY(ErrorCode::kSuccess != to_destroy_eps_->Pop(endpoint_header)))
        {
            break;
        }
        
        if (!endpoint_header->is_tx_release())
        {
            drive_engine_->UnregTxEndpoint(endpoint_header);
        }

        if (!endpoint_header->is_rx_release())
        {
            //如果是CorkSet + kActive状态，直接Uncork，设置为rx_release
            if (endpoint_header->check_and_set_rx_cork_lck())
            {
                endpoint_header->set_rx_release();
            }
            else
            {
                 drive_engine_->UnregRxEndpoint(endpoint_header);
            }
        }

        const uint64_t current_tp = endpoint_header->GetTimepoint();
        if (endpoint_header->is_release() && 
            endpoint_header->phy_time + kEndpointHeaderDestroyDelayMicro < current_tp)
        {
            TcpEngineImpl* const tcp_engine_impl = endpoint_header->tcp_engine_impl;
            assert(tcp_engine_impl);
            tcp_engine_impl->DestroyEndpoint(endpoint_header);
        }
        else
        {
            __attribute__((unused)) const auto push_ec = to_destroy_eps_->Push(endpoint_header);
            assert(ErrorCode::kSuccess == push_ec);
        }
    }
}

void ControlActor::DoForceEpsDestroy()
{
    assert(to_destroy_eps_);
    EndpointHeader* endpoint_header = nullptr;

    while (ErrorCode::kSuccess == to_destroy_eps_->Pop(endpoint_header))
    {
        TcpEngineImpl* const tcp_engine_impl = endpoint_header->tcp_engine_impl;
        assert(tcp_engine_impl);

        // assert(endpoint_header->is_tx_release());
        // assert(endpoint_header->is_rx_release());
        // assert(endpoint_header->is_phy_release());
        tcp_engine_impl->DestroyEndpoint(endpoint_header);
    }
}

void ControlActor::DiagnoseHeartbeat(const std::set<EndpointHeader*>& droped_eps_set)
{
    assert(heartbeat_monitor_);

    EventHBTO event_hbto;
    HeartbeatNode heartbeat_node;

    const uint64_t monitor_length = heartbeat_monitor_->length();
    for (uint64_t index = 0; index < monitor_length; ++index)
    {
        if (ADK_UNLIKELY(ErrorCode::kSuccess != heartbeat_monitor_->Pop(heartbeat_node)))
        {
            break;
        }

        assert(heartbeat_node.endpoint_header);
        auto* const endpoint_header = heartbeat_node.endpoint_header;
        if (ADK_UNLIKELY(droped_eps_set.end() != droped_eps_set.find(endpoint_header)))
        {
            continue;
        }

        if (ADK_UNLIKELY(!endpoint_header->is_running()))
        {
            DropCtrlEndpoint(endpoint_header);
            continue;
        }
 
        if (kuint64Max != endpoint_header->heartbeat_timeout)
        {
            const auto current_tp = endpoint_header->GetTimepoint();
            if (heartbeat_node.last_rx_bytes != endpoint_header->rx_message_bytes)
            {
                heartbeat_node.last_rx_bytes = endpoint_header->rx_message_bytes;
                heartbeat_node.last_check_timepoint = current_tp;
            }
            else
            {
                if (current_tp > heartbeat_node.last_check_timepoint + endpoint_header->heartbeat_timeout)
                {
                    if (!endpoint_header->message_delivering()
                        && (EpRxCorkStat::kRxCorkNot == endpoint_header->rx_cork_stat())
                        && !ACCESS_ONCE(is_paused_))
                    {
                        if (ErrorCode::kSuccess != TcpEndpoint::DeliverWarnEvent(
                            endpoint_header, 
                            &event_hbto))
                        {
                            DropCtrlEndpoint(endpoint_header);
                            continue;
                        }
                    }

                    heartbeat_node.last_check_timepoint = current_tp;
                }
            }
        }

        TxMessageQueue* const tx_message_queue = endpoint_header->tx_message_queue;
        assert(tx_message_queue);

        if (0 == tx_message_queue->length())
        {
            TcpEndpoint::SendHeartbeatMsg(endpoint_header);
        }

        ADK_ASSERT_SUCCESS(heartbeat_monitor_->Push(heartbeat_node));
    }
}

}

}
