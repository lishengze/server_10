#ifndef ADK_IMPL_TCP_ENGINE_IMPL_H_
#define ADK_IMPL_TCP_ENGINE_IMPL_H_

#include "local_ports.h"
#include "message_pool.h"
#include "io_engine_base.h"
#include "tcp_verbs/tcp_interface.h"

#include <map>
#include <mutex>
#include <vector>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <boost/unordered_map.hpp>

#include <adk/io_engine/handler.h>
#include <adk/io_engine/endpoint.h>
#include <adk/io_engine/tcp_engine.h>
#include <adk/lock_free_queue_variant.h>

namespace adk_impl
{

namespace io_engine
{

using std::map;
using std::pair;
using std::mutex;
using std::vector;
using std::string;
using std::unordered_set;
using std::unordered_map;

using verbs::ITcpStack;
using verbs::ITcpEndpoint;

using variant::VariantEntry;

class DriveEngine;
class TcpAcceptor;
class TcpEndpoint;
class EndpointHeader;

using RecycleEpsQueue = variant::MPMCQueue<TcpEndpoint*>;
using EndpointHeaderQueue = variant::SPMCQueue<EndpointHeader*>;

class TcpEngineImpl final : public TcpEngine, public IoEngineBase
{
public:
    typedef unordered_map<pair<string, uint16_t>, EndpointHeader*, 
        boost::hash<const pair<string, uint16_t>&>> EndpointHeaderMap;

    typedef unordered_map<uint16_t, TcpAcceptor*> AcceptorMap;

    typedef unordered_set<const EndpointHeader*, 
        boost::hash<const EndpointHeader*>> EndpointHeaderSet;

    TcpEngineImpl();

    int32_t Init(const Property& engine_props);

    void Exit();

    ///> inherit
    Acceptor* ToAccept(const Property& accept_props);

    ///> inherit
    Endpoint* ToConnect(const Property& connect_props);

    ///> create server acceptor
    TcpAcceptor* CreateAcceptor(const Property& aptr_props, 
                                EventHandler* const event_hdl, 
                                AcceptHandler* const accept_hdl);

    ///> create server endpoint
    TcpEndpoint* CreateEndpoint(const Property& ep_props, 
                                EventHandler* const event_hdl, 
                                ITcpEndpoint* const tcp_endpoint);

    ///> create client endpoint
    TcpEndpoint* CreateEndpoint(const Property& ep_props, 
                                EventHandler* const event_hdl, 
                                ConnectHandler* const connect_hdl);

	///> destroy acceptor
    void ToCloseAcceptor(TcpAcceptor* const acceptor_impl);

    ///> add io control async delete task
    void ToCloseEndpoint(TcpEndpoint* const endpoint_impl);

    ///> destroy the endpoint body
    int32_t DestroyEndpoint(TcpEndpoint* const endpoint_impl);

    ///> destroy endpoint the last master endpoint
    void DestroyEndpoint(EndpointHeader* const endpoint_header);

    ///> destory endpoints in establish process
    void DestroyInproEndpoint(EndpointHeader* const endpoint_header, 
                              const string& remote_ip, 
                              uint16_t remote_port);

    void RecycleEndpoint(TcpEndpoint* endpoint_impl);

    void RegisterEndpoint(EndpointHeader* const endpoint_header);

    void UnregisterEndpoint(EndpointHeader* const endpoint_header);

    void UninstallHandler(EndpointHeader* const endpoint_header);

    int32_t CollectIndicator(std::string& indicator);

    int32_t Pause();

    void Resume();

    LocalPorts* local_ports_register() const
    {
        return local_ports_register_;
    }

    inline ITcpStack* tcp_stack() const
    {
        return tcp_stack_;
    }

    uint32_t engine_capacity() const
    {
        assert(endpoint_header_queue_);
        return endpoint_header_queue_->capacity();
    }

    inline DriveEngine* drive_engine() const
    {
        return drive_engine_;
    }

    inline bool is_tx_latency_set() const
    {
        return (uint8_t)is_tx_low_latency() == tx_low_latency_;
    }

    inline bool is_tx_low_latency() const
    {
        return (uint8_t)true == tx_low_latency_;
    }

    inline bool is_rx_latency_set() const
    {
        return (uint8_t)is_rx_low_latency() == rx_low_latency_;
    }

    inline bool is_rx_low_latency() const
    {
        return (uint8_t)true == rx_low_latency_;
    }

    inline uint32_t rx_memory_block_size() const
    {
        return rx_memory_block_size_;
    }

    inline EndpointHeader*    blank_endpoint_header()
    {
        return blank_endpoint_header_;
    }

private:

    ///> Create blank endpoint
    TcpEndpoint* CreateEndpoint();

    ///> delete creating endpoint
    void DestroyCreatingEndpoint(TcpEndpoint* const endpoint_impl);

    ///> tcp verbs
    ITcpStack*           tcp_stack_;
    DriveEngine*         drive_engine_;
    EndpointHeader*      endpoint_headers_;
    RecycleEpsQueue*     recycle_eps_queue_;
    EndpointHeaderQueue* endpoint_header_queue_;

    ///> 0:no low latency/1:low latency/other:not set
    uint8_t              tx_low_latency_;
    uint8_t              rx_low_latency_;
    uint32_t             rx_memory_block_size_;

    LocalPorts*          local_ports_register_;
    EndpointHeader*      blank_endpoint_header_;

    ///> singleton endpoint map
    mutex                singleton_ep_lock_;
    EndpointHeaderMap    singleton_ep_map_;

    mutex                acceptor_lock_;
    AcceptorMap          acceptor_map_;

    mutex                phy_eps_lock_;
    EndpointHeaderSet    phy_eps_set_;
};

}

}

#endif