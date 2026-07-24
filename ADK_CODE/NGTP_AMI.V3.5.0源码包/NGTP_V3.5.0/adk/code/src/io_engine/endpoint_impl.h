#ifndef ADK_IMPL_IO_ENGINE_EP_IMPL_H_
#define ADK_IMPL_IO_ENGINE_EP_IMPL_H_

#include "handler_impl.h"
#include "stream_buffer.h"
#include "tcp_engine_impl.h"
#include "endpoint_register.h"

#include "tcp_verbs/tcp_interface.h"

#include <list>
#include <mutex>
#include <utility>
#include <functional>

#include <adk/seq_lock.h>
#include <adk/arch/generic.h>
#include <adk/token_buckets.h>
#include <adk/lock_free_queue_variant.h>

#include <adk/io_engine/property.h>
#include <adk/io_engine/endpoint.h>

namespace adk_impl
{

namespace io_engine
{

enum class TxStatus : uint16_t
{
    kInit = 0,
    kIdle,
    kActive,
    kBlock,
    kRelease,
    kDirecSend
};

enum class RxStatus : uint16_t
{
    kInit = 0,
    kActive,
    kPolling,
    kRelease,
};

///> io ctrl actor
enum class PhyStatus : int32_t
{
    kConnecting = 0,
    kEstablished,
    kToDelete,
    kRelease,
    kStatusMax,
};

enum class EpStatus : int32_t
{
    kInit = 0,
    kRunning,
    kToClose,
    kInvalid,
};
 enum class EpRxCorkStat : uint64_t 
 {
    kRxCorkNot,    //非cork状态
    kRxCorkPre,    //客户要求cork
    kRxCorkSet,    //设置cork状态
    kRxCorkLck     //终止状态
};
using std::list;
using std::mutex;
using std::lock_guard;
using variant::MPSCQueue;
using variant::SPSCQueue;
using variant::VariantEntry;

using verbs::ITcpEndpoint;

using EndpointChain = list<TcpEndpoint*>;
using EndpointProFunc = std::function<void(TcpEndpoint* const)>;

class Endpoint;
class SendActor;

class EventHandler;
class AcceptHandler;
class ConnectHandler;

class MessageHandler;
class TimingTaskHandler;
using TxMessageQueue = variant::MPSCQueue<Message*>;
using TxMessageQueueUnsafe = variant::SPSCQueue<Message*>;
using RxMessageQueue = variant::ThreadLocalQueue<Message*>;

struct EndpointHeader
{
    TxStatus          tx_status __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    bool              tx_status_lock;
    bool              tx_valid;
    uint32_t          tx_min_resident;
    uint64_t          tx_sch_time;
    TxMessageQueue*   tx_message_queue;
    SendActor*        send_actor_impl;
    uint64_t          tx_message_bytes;
    ITcpEndpoint*     tcp_endpoint;
    HeartbeatHandler* heartbeat_handler;
    TokenBucket*      token_bucket;
    TcpEngineImpl*    tcp_engine_impl;
    uint64_t          tx_dispatch1_nr;
    uint64_t          tx_consume_flag;

    RxStatus          rx_status __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    bool              rx_valid;
    uint32_t          rx_min_resident;
    uint64_t          rx_sch_time;
    uint64_t          rx_idle_time;
    uint64_t          heartbeat_timeout;
    MessageImpl*      deliver_message;
    uint64_t          rx_message_bytes;
    uint64_t          deliver_message_nr;
    EpRxCorkStat      rx_cork_stt;
    MessageHandler*   message_handler;
    DecodeTemplate*   decode_template;
    void*             share_ctx;
    uint64_t          rx_dispatch1_nr;

    uint64_t          tx_dispatch2_nr __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint64_t          rx_dispatch2_nr __attribute__((aligned(ADK_CACHE_LINE_SIZE)));

    mutex             lock __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    bool              is_singleton;
    EndpointChain     sub_endpoints;
    int32_t           distributor;
    PhyStatus         phy_status;
    uint64_t          phy_time;
    uint64_t          version;

    EndpointHeader();

    void Reset();

    inline int32_t allocate_index()
    {
        return ++distributor;
    }

    inline uint64_t GetTimepoint() const
    {
        assert(token_bucket);
        return token_bucket->GetCurrentTime();
    }

    const string& remote_ip() const
    {
        assert(tcp_endpoint);
        return tcp_endpoint->remote_ip();
    }

    uint16_t remote_port() const
    {
        assert(tcp_endpoint);
        return tcp_endpoint->remote_port();
    }

    const string& local_ip() const 
    {
        assert(tcp_endpoint);
        return tcp_endpoint->local_ip();
    }

    uint16_t local_port() const
    {
        assert(tcp_endpoint);
        return tcp_endpoint->local_port();
    }

    inline void set_established()
    {
        rx_cork_stt = EpRxCorkStat::kRxCorkNot;
        phy_status = PhyStatus::kEstablished;
    }

    inline void set_to_delete()
    {
        if (phy_status < PhyStatus::kToDelete)
        {
            phy_status = PhyStatus::kToDelete;
        }

        tx_valid = false;
        rx_valid = false;
    }

    inline void set_phy_release()
    {
        phy_status = PhyStatus::kRelease;
    }

    inline bool is_connecting() const
    {
        return (PhyStatus::kConnecting == phy_status);
    }

    inline bool is_running() const
    {
        return (PhyStatus::kEstablished == phy_status);
    }

    inline bool is_phy_release() const
    {
        return (PhyStatus::kRelease == phy_status);
    }

    inline void set_tx_idle()
    {
        tx_sch_time = GetTimepoint();
        ADK_BARRIER();
        tx_status = TxStatus::kIdle;
    }

    inline void set_tx_block()
    {
        tx_status = TxStatus::kBlock;
    }

    inline void set_tx_release()
    {
        tx_status = TxStatus::kRelease;

        assert(tx_message_queue);
        tx_message_queue->set_release_alert();
    }

    inline bool atomic_set_tx_release()
    {
        return (__sync_bool_compare_and_swap(&tx_status, 
                                             TxStatus::kIdle, 
                                             TxStatus::kRelease));
    }

    inline void set_tx_active()
    {
        tx_sch_time = GetTimepoint();
        tx_status = TxStatus::kActive;
    }

    inline bool atomic_set_tx_active()
    {
        return (__sync_bool_compare_and_swap(&tx_status, 
                                             TxStatus::kIdle, 
                                             TxStatus::kActive));
    }

    inline bool is_tx_release() const
    {
        return (TxStatus::kRelease == tx_status);
    }

    inline bool have_tx_msg() const
    {
        assert(tx_message_queue);
        return (0 != tx_message_queue->length<true>());
    }

    inline bool is_tx_idle() const
    {
        return (TxStatus::kIdle == tx_status);
    }

    inline bool is_tx_block() const
    {
        return (TxStatus::kBlock == tx_status);
    }

    inline void set_rx_active()
    {
        const auto timepoint = GetTimepoint();
        rx_sch_time = timepoint;
        rx_status = RxStatus::kActive;
    }

    inline void set_rx_polling()
    {
        rx_status = RxStatus::kPolling;
    }

    inline void set_rx_release()
    {
        assert(rx_cork_stat() != EpRxCorkStat::kRxCorkSet);
        rx_cork_stt = EpRxCorkStat::kRxCorkLck;
        rx_status = RxStatus::kRelease;
    }

    inline bool is_rx_polling() const
    {
        return (RxStatus::kPolling == rx_status);
    }

    inline bool is_rx_release() const
    {
        return (RxStatus::kRelease == rx_status);
    }

    inline bool is_release() const
    {
        if (is_tx_release())
        {
            const auto nr_lock2 = ACCESS_ONCE(tx_dispatch2_nr);
            ADK_BARRIER();
            const auto nr_lock1 = ACCESS_ONCE(tx_dispatch1_nr);

            if (ADK_UNLIKELY(nr_lock2 != nr_lock1))
            {
                return false;
            }
        }
        else
        {
            return false;
        }

        if (is_rx_release())
        {
            const auto nr_lock2 = ACCESS_ONCE(rx_dispatch2_nr);
            ADK_BARRIER();
            const auto nr_lock1 = ACCESS_ONCE(rx_dispatch1_nr);

            if (ADK_UNLIKELY(nr_lock2 != nr_lock1))
            {
                return false;
            }
        }
        else
        {
            return false;
        }

        return is_phy_release();
    }

    inline void set_phy_time()
    {
        phy_time = GetTimepoint();
    }

    inline void version_alloc()
    {
        ++version;
    }

    inline bool version_lock(uint64_t compare_version)
    {
        return __sync_bool_compare_and_swap(&version, compare_version, 0);
    }

    inline void version_free()
    {
        uint64_t temp_version;
        do
        {
        version_retry:
            temp_version = ACCESS_ONCE(version);
            if (ADK_UNLIKELY(0 == (temp_version & 0x1)))
            {
                goto version_retry;
            }
        } while (ADK_UNLIKELY(!__sync_bool_compare_and_swap(&version, 
                                                            temp_version, 
                                                            temp_version + 1)));
    }

    inline bool message_delivering()
    {
        return (deliver_message_nr & 0x1);
    }

    //!corklck-> corkpre
    bool SetRxCorkPre();

    //corkpre->corkset
    inline bool set_rx_cork_from_pre()
    {
        if (ADK_UNLIKELY(rx_cork_stat() == EpRxCorkStat::kRxCorkPre))
        {
            rx_cork_stt = EpRxCorkStat::kRxCorkSet;
            return true;
        }
        else
        {
            return false;
        }
    }

    bool SetRxUncork();
    /**
     *@brief  当cork状态为EpRxCorkStat::kRxCorkSet设置为kCorklck。 
     **/
    inline bool check_and_set_rx_cork_lck()
    {
        return __sync_bool_compare_and_swap(&rx_cork_stt,EpRxCorkStat::kRxCorkSet,EpRxCorkStat::kRxCorkLck);
    }


    inline EpRxCorkStat rx_cork_stat() const 
    {
        return  ACCESS_ONCE(rx_cork_stt);
    }

    inline void set_tx_min_resident(uint64_t tx_resident)
    {
        tx_min_resident = tx_resident;
    }

    inline bool in_tx_active_time(uint64_t decay_counter)
    {
        return ++tx_sch_time < decay_counter;
    }

    inline bool in_tx_resident_time() const
    {
        return (tx_sch_time + tx_min_resident) > GetTimepoint();
    }

    inline void set_rx_min_resident(uint64_t rx_resident)
    {
        rx_min_resident = rx_resident;
    }

    inline bool in_rx_active_time(uint64_t decay_counter)
    {
        return ++rx_sch_time < decay_counter;
    }

    inline bool in_rx_resident_time() const
    {
        return (rx_sch_time + rx_min_resident) > GetTimepoint();
    }
};

class TcpEndpoint final : public Endpoint
{
public:
    TcpEndpoint(EndpointHeader* const endpoint_header);

    ~TcpEndpoint();

    int32_t Init(const Property& props, 
                 EventHandler* const event_handler, 
                 ConnectHandler* const connect_handler);

    int32_t Init(const Property& props, 
                 EventHandler* const event_handler, 
                 ITcpEndpoint* const tcp_endpoint);

    void OnEvent(Event* const event);

    void OnInitEnd();

    static void Exit(EndpointHeader* const endpoint_header);

    static void Destroy(TcpEndpoint* const endpoint_impl);

    static TcpEndpoint* Duplicate(EndpointHeader* const endpoint_header, 
                                  const Property& props,
                                  EventHandler* const event_handler, 
                                  ConnectHandler* const connect_handler);

    int32_t OnAccept(AcceptHandler* const accept_handler);

    int32_t OnConnect();

    ITcpEndpoint* CreateITcpEndpoint();

    static int32_t OnConnect(EndpointHeader* const endpoint_header);

    static void DeliverEvent(EndpointHeader* const endpoint_header, Event* const event);

    static int32_t DeliverWarnEvent(EndpointHeader* const endpoint_header, Event* const event);

    static void DeliverErrorEvent(EndpointHeader* const endpoint_header, Event* const event);

    static void SendHeartbeatMsg(EndpointHeader* const endpoint_header);

    int32_t Reconnect() override;

    int32_t Reconnect(const string& remote_ip, uint16_t remote_port) override;

    inline void DeleteMessage(Message* const message)
    {
        TcpEngineImpl* const tcp_engine_impl = endpoint_header_->tcp_engine_impl;
        assert(tcp_engine_impl);

        tcp_engine_impl->DeleteMessage((MessageImpl*)message);
    }

    void add_reference()
    {
        __sync_fetch_and_add(&reference_, 1);
    }

    void sub_reference()
    {
        __sync_fetch_and_sub(&reference_, 1);
    }

    int32_t reference() const
    {
        return reference_;
    }

    inline uint64_t GetTimepoint() const
    {
        assert(endpoint_header_);
        return endpoint_header_->GetTimepoint();
    }

    void set_event_handler(EventHandler* const event_handler)
    {
        event_handler_ = event_handler;
    }

    void set_connect_handler(ConnectHandler* const connect_handler)
    {
        connect_handler_ = connect_handler;
    }

    void set_endpoint_header(EndpointHeader* const endpoint_header)
    {
        assert(endpoint_header);
        endpoint_header_ = endpoint_header;
    }

    TcpEngineImpl* tcp_engine_impl() const
    {
        assert(endpoint_header_);
        return endpoint_header_->tcp_engine_impl;
    }

    inline EndpointHeader* endpoint_header() const
    {
        return endpoint_header_;
    }

    inline bool is_running() const
    {
        return (EpStatus::kRunning == ep_status_);
    }

    inline bool is_alive() const
    {
        return (ACCESS_ONCE(ep_status_) < EpStatus::kToClose);
    }

    inline bool set_ep_running()
    {
        return __sync_bool_compare_and_swap(&ep_status_, 
                                            EpStatus::kInit, 
                                            EpStatus::kRunning);
    }

    inline bool set_reconnecting()
    {
        EpStatus ep_status;
        do 
        {
            ep_status = ACCESS_ONCE(ep_status_);
            if (EpStatus::kToClose == ep_status)
            {
                return false;
            }
        } while (!__sync_bool_compare_and_swap(&ep_status_,
                                               ep_status,
                                               EpStatus::kInit));
        return true;
    }

    inline bool set_to_close()
    {
        EpStatus ep_status;
        do 
        {
            ep_status = ACCESS_ONCE(ep_status_);
            if (EpStatus::kToClose == ep_status)
            {
                return false;
            }
        } while (!__sync_bool_compare_and_swap(&ep_status_, 
                                               ep_status, 
                                               EpStatus::kToClose));
        return true;
    }

    inline bool invalidate()
    {
        EpStatus ep_status;
        do 
        {
            ep_status = ACCESS_ONCE(ep_status_);
            if (ep_status > EpStatus::kRunning)
            {
                return false;
            }
        } while (!__sync_bool_compare_and_swap(&ep_status_, 
                                               ep_status, 
                                               EpStatus::kInvalid));

        return true;
    }

    inline bool verify() const
    {
        return (ErrorCode::kSuccess == EndpointRegister::VerifyEndpoint(this));
    }

    inline const Property& properties() const
    {
        return properties_;
    }

    void UpdateTcpProps(const Property& props);

private:

    int32_t Init(const Property& props);

    template<bool kDefaultInit>
    static void UpdateTcpProps(ITcpEndpoint* const tcp_endpoint, const Property& props);

    template<typename CallbackFunc>
    static void AddenCallback(EndpointHeader* const endpoint_header, 
                              const CallbackFunc& on_app_callback)
    {
        EndpointChain endpoint_chain;
        EndpointChain endpoint_adden;
        while (true)
        {
            endpoint_adden.clear();

            do
            {
                lock_guard<mutex> share_lock(endpoint_header->lock);
                for (auto& endpoint_impl : endpoint_header->sub_endpoints)
                {
                    if (endpoint_chain.end() == std::find(endpoint_chain.begin(), 
                                                          endpoint_chain.end(), 
                                                          endpoint_impl))
                    {
                        endpoint_impl->add_reference();
                        endpoint_adden.push_back(endpoint_impl);
                    }
                }

                endpoint_chain = endpoint_header->sub_endpoints;
            } while (false);

            if (0 == endpoint_adden.size())
            {
                break;
            }

            for (auto& endpoint_impl : endpoint_adden)
            {
                on_app_callback(endpoint_impl);
                endpoint_impl->sub_reference();
            }
        }
    }

    ///> start from 1
    uint32_t        sub_index_;
    void*           private_ctx_;
    EndpointHeader* endpoint_header_;

    Property        properties_;
    EpStatus        ep_status_;
    int32_t         reference_;
    ///> each singleton
    EventHandler*   event_handler_;
    ConnectHandler* connect_handler_;

    friend class Endpoint;
};

}

}

#endif
