#ifndef ADK_IMPL_IO_EVENT_ENGINE_H_
#define ADK_IMPL_IO_EVENT_ENGINE_H_

#include "io_thread.h"
#include "io_control.h"
#include "event_impl.h"
#include "endpoint_impl.h"

#include <mutex>
#include <thread>
#include <vector>
#include <sys/epoll.h>

#include <adk/error_code.h>
#include <adk/io_engine/property.h>
#include <adk/lock_free_queue_variant.h>

namespace adk_impl
{

namespace io_engine
{

using std::vector;
using verbs::ITcpEPoller;

using variant::MPSCQueue;
using PhyEndpint = std::pair<EndpointHeader* const, uint64_t>;

class SendActor;
class RecvActor;
class TcpEndpoint;
class ControlActor;

constexpr int32_t kInvalidFd = -1;
constexpr int32_t kEpollTimeoutms = 50;
constexpr int32_t kPipeReadBufferSize = 1024;

class DriveEngine
{
public:
    DriveEngine(TcpEngineImpl* const tcp_engine_impl);

    int32_t Init(const Property& props);

    void Stop();

    void Exit();

    inline int32_t RegEndpoint(EndpointHeader* const endpoint_header)
    {
        assert(endpoint_header);

        if (ADK_UNLIKELY(ErrorCode::kSuccess != RegTxEndpoint(endpoint_header)))
        {
            goto reg_error;
        }

        if (ADK_UNLIKELY(ErrorCode::kSuccess != RegRxEndpoint(endpoint_header)))
        {
            goto reg_error;
        }

        endpoint_header->set_phy_time();
        return ErrorCode::kSuccess;

    reg_error:
        ///> event deliver in io control thread
        EventSocketError sockerr_event("DriveReg", errno);
        TcpEndpoint::DeliverErrorEvent(endpoint_header, &sockerr_event);
        DropTxEndpoint(endpoint_header);
        DropRxEndpoint(endpoint_header);
        return ErrorCode::kFailure;
    }

    inline int32_t RegTxEndpoint(EndpointHeader* const endpoint_header)
    {
        assert(endpoint_header);

        auto* send_actor = static_cast<SendActor*>(PickDuplexActor());
        if (nullptr == send_actor)
        {
            send_actor = PickSendActor();
        }

        assert(send_actor);

        endpoint_header->send_actor_impl = send_actor;
        if (endpoint_header->have_tx_msg())
        {
            endpoint_header->set_tx_active();
            send_actor->AddJob(endpoint_header);
        }
        else
        {
            OnTxEndpointIdle(endpoint_header);
        }

        return ErrorCode::kSuccess;
    }

    inline int32_t UnregTxEndpoint(EndpointHeader* const endpoint_header)
    {
        assert(endpoint_header);

        if (ADK_UNLIKELY(endpoint_header->tx_status_lock))
        {
            return ErrorCode::kFailure;
        }

        if (endpoint_header->atomic_set_tx_release())
        {
            return ErrorCode::kSuccess;
        }

        if (endpoint_header->is_tx_block())
        {
            const uint64_t current_tp = endpoint_header->GetTimepoint();
            if (endpoint_header->phy_time + kDeleteCheckIntervalus < current_tp)
            {
                const PhyEndpint write_content = std::make_pair(endpoint_header, 
                                                                endpoint_header->version);
                const auto ec = write(tx_delete_pipe_[1], &write_content, sizeof(PhyEndpint));
                if (ADK_UNLIKELY(sizeof(PhyEndpint) != ec))
                {
                    return ErrorCode::kFailure;
                }

                endpoint_header->phy_time = current_tp;
            }
        }

        return ErrorCode::kSuccess;
    }

    inline int32_t RegRxEndpoint(EndpointHeader* const endpoint_header)
    {
        assert(endpoint_header);

        if (nullptr != rx_epoller_)
        {
            if (nullptr != endpoint_header->message_handler)
            {
                ++endpoint_header->rx_dispatch1_nr;
                if (ADK_UNLIKELY(!rx_epoller_->EPollAddR(endpoint_header->tcp_endpoint,
                                                         endpoint_header)))
                {
                    ++endpoint_header->rx_dispatch2_nr;
                    return ErrorCode::kFailure;
                }

                endpoint_header->set_rx_polling();
            }
            else
            {
                endpoint_header->set_rx_release();
            }
        }
        else
        {
            assert(1 == duplex_actors_.size());
            assert(endpoint_header->message_handler);

            auto* const duplex_actor = duplex_actors_.front();
            assert(duplex_actor);

            duplex_actor->AddJob(endpoint_header);
        }

        return ErrorCode::kSuccess;
    }

    inline int32_t UnregRxEndpoint(EndpointHeader* const endpoint_header)
    {
        assert(endpoint_header);

        if (endpoint_header->is_rx_polling())
        {
            const uint64_t current_tp = endpoint_header->GetTimepoint();
            if (endpoint_header->phy_time + kDeleteCheckIntervalus < current_tp)
            {
                const PhyEndpint write_content = std::make_pair(endpoint_header, 
                    endpoint_header->version);
                const auto ec = write(rx_delete_pipe_[1], &write_content, sizeof(PhyEndpint));
                if (ADK_UNLIKELY(sizeof(PhyEndpint) != ec))
                {
                    return ErrorCode::kFailure;
                }

                endpoint_header->phy_time = current_tp;
            }
        }

        return ErrorCode::kSuccess;
    }

    inline int32_t AddTaskAccept(AcceptTask* const accept_task)
    {
        return control_actor_->ToAccept(accept_task);
    }

    inline int32_t AddTaskConnect(ConnectTask* const connect_task)
    {
        return control_actor_->ToConnect(connect_task);
    }

    inline int32_t AddDeleteTask(TcpEndpoint* const endpoint_impl)
    {
        return control_actor_->ToDelete(endpoint_impl);
    }

    inline int32_t AddDeleteTask(EndpointHeader* const endpoint_header)
    {
        return control_actor_->ToDelete(endpoint_header);
    }

    inline int32_t AddImmediateTask(TcpEndpoint* const endpoint, uint32_t task_type)
    {
        return control_actor_->ToCallbackImmediate(endpoint, task_type);
    }

    inline void OnTxEndpointIdle(EndpointHeader* const endpoint_header)
    {
        assert(endpoint_header);

        endpoint_header->tx_status_lock = true;
        ADK_BARRIER();

        ///> 1. set tx idle flag
        endpoint_header->set_tx_idle();

        ///> 2. check message queue / 3. try to atomic set tx active
        if (endpoint_header->have_tx_msg() && endpoint_header->atomic_set_tx_active())
        {
            auto* const send_actor_impl = endpoint_header->send_actor_impl;
            assert(send_actor_impl);

            send_actor_impl->AddJob(endpoint_header);
        }

        ADK_BARRIER();
        endpoint_header->tx_status_lock = false;
    }

    ///> to the function when send failed
    template<typename EPollerType>
    inline int32_t OnTxEndpointBlock(EndpointHeader* const endpoint_header)
    {
        assert(endpoint_header);

        auto* const tcp_endpoint = endpoint_header->tcp_endpoint;
        assert(tcp_endpoint);

        ++endpoint_header->tx_dispatch1_nr;
        if (ADK_UNLIKELY(!((EPollerType*)tx_epoller_)->EPollAddW(tcp_endpoint, endpoint_header)))
        {
            ++endpoint_header->tx_dispatch2_nr;
            return ErrorCode::kFailure;
        }

        endpoint_header->set_tx_block();

        return ErrorCode::kSuccess;
    }

    template<typename EPollerType>
    inline int32_t OnRxEndpointBlock(EndpointHeader* const endpoint_header)
    {
        assert(endpoint_header);
        assert(endpoint_header->message_handler);

        ++endpoint_header->rx_dispatch1_nr;
        if (ADK_UNLIKELY(!((EPollerType*)rx_epoller_)->EPollAddR(endpoint_header->tcp_endpoint,
                                                                 endpoint_header)))
        {
            ++endpoint_header->rx_dispatch2_nr;
            return ErrorCode::kFailure;
        }

        endpoint_header->set_rx_polling();

        return ErrorCode::kSuccess;
    }
/*
    inline void DropEndpoint(EndpointHeader* const endpoint_header)
    {
        DropRxEndpoint(endpoint_header);
        DropTxEndpoint(endpoint_header);
    }
*/
    inline void DropTxEndpoint(EndpointHeader* const endpoint_header)
    {
        endpoint_header->set_tx_release();
    }

    inline void DropRxEndpoint(EndpointHeader* const endpoint_header)
    {
        endpoint_header->set_rx_release();
    }

    void RxActorRCU(RecvActorArena* actor_arena);

    void InsertActorArena(RecvActorArena* actor_arena);

    void RemoveActorArena(RecvActorArena* actor_arena);

    int32_t PauseRxActor();

    void ResumeRxActor();

    inline TcpEngineImpl* tcp_engine_impl() const
    {
        return tcp_engine_impl_;
    }

    inline ITcpStack* tcp_stack() const
    {
        assert(tcp_engine_impl_);
        return tcp_engine_impl_->tcp_stack();
    }

    inline ControlActor* control_actor() const
    {
        return control_actor_;
    }

    inline void DispatchRxEndpoint(EndpointHeader* const endpoint_header)
    {
        if (duplex_actors_.size() == 0)
        {
            DispatchRxEndpoint<false>(endpoint_header);
        }
        else
        {
            DispatchRxEndpoint<true>(endpoint_header);
        }
    }


private:

    void StartWorker(verbs::ITcpStack* tcp_stack, bool duplex_actor);

    template<typename EPollerType, bool kDuplexActor>
    void TxEpollThread();

    template<typename EPollerType, bool kDuplexActor>
    void RxEpollThread();

    template<typename DropFunc>
    void DropPollingEndpoint(char* buffer, int32_t len, const DropFunc& drop_func)
    {
        while (len > 0)
        {
            PhyEndpint* read_content = (PhyEndpint*)buffer;
            EndpointHeader* const endpoint_header = read_content->first;
            assert(endpoint_header);

            if (endpoint_header->version_lock(read_content->second))
            {
                drop_func(endpoint_header);
                endpoint_header->version = read_content->second;
            }

            buffer += sizeof(PhyEndpint);
            len -= sizeof(PhyEndpint);
        }
    }

    template<bool kDuplexActor>
    void DispatchRxEndpoint(EndpointHeader* const endpoint_header)
    {
        if (kDuplexActor)
        {
            auto* const duplex_actor = PickDuplexActor();
            assert(duplex_actor);

            duplex_actor->AddJob(endpoint_header);
        }
        else
        {
            auto* const recv_actor = PickRecvActor();
            assert(recv_actor);

            recv_actor->AddJob(endpoint_header);
        }
    }

    /*
    template<typename actor_type>
    actor_type* PickActor(const vector<actor_type*>& actors)
    {
        if (1 == actors.size())
        {
            return actors.front();
        }

        actor_type* right_actor = nullptr;
        uint64_t actor_min_load = kuint64Max;
        for (auto iter = actors.begin(); iter != actors.end(); ++iter)
        {
            const uint64_t actor_load = (*iter)->this_actor_load();
            if (actor_load < actor_min_load)
            {
                if (0 == actor_load)
                {
                    return *iter;
                }

                right_actor = *iter;
                actor_min_load = actor_load;
            }
        }

        return right_actor;
    }*/

    RecvActor* PickRecvActor()
    {
        const auto selector = ACCESS_ONCE(rx_selector_);
        if (selector < recv_actors_.size())
        {
            ++rx_selector_;
            return recv_actors_[selector];
        }

        rx_selector_ = 1;
        return recv_actors_.front();
    }

    SendActor* PickSendActor()
    {
        const auto selector = ACCESS_ONCE(tx_selector_);
        if (selector < send_actors_.size())
        {
            ++tx_selector_;
            return send_actors_[selector];
        }

        tx_selector_ = 1;
        return send_actors_.front();
    }

    DuplexActor* PickDuplexActor()
    {
        if (duplex_actors_.size() > 0)
        {
            const auto selector = ACCESS_ONCE(duplex_selector_);
            if (selector < duplex_actors_.size())
            {
                ++duplex_selector_;
                return duplex_actors_[selector];
            }

            duplex_selector_ = 1;
            return duplex_actors_.front();
        }

        return nullptr;
    }

    bool    is_running_;

    ///> data come event
    int32_t tx_delete_pipe_[2];
    int32_t rx_delete_pipe_[2];

    ITcpEPoller*    tx_epoller_;
    ITcpEPoller*    rx_epoller_;

    TcpEngineImpl*  tcp_engine_impl_;

    std::mutex      actor_area_lock_;
    std::vector<RecvActorArena*> actor_area_list_;

    ///> thread1: schedule thread
    std::string     tx_drive_name_;
    std::thread     tx_drive_hdl_;

    ///> thread2: schedule thread
    std::string     rx_drive_name_;
    std::thread     rx_drive_hdl_;

    ///> thread3: control worker / last to exit
    ControlActor*   control_actor_;

    ///> thread4: send worker
    uint32_t tx_selector_;
    std::vector<SendActor*> send_actors_;

    ///> thread5: recv worker
    uint32_t rx_selector_;
    std::vector<RecvActor*> recv_actors_;

    ///>  thread4: duplex worker
    uint32_t duplex_selector_;
    std::vector<DuplexActor*> duplex_actors_;
};

}

}

#endif
