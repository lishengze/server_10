#ifndef ADK_IMPL_IO_ENGINE_ACCEPTOR_IMPL_H_
#define ADK_IMPL_IO_ENGINE_ACCEPTOR_IMPL_H_

#include "event_impl.h"
#include "tcp_verbs/tcp_interface.h"

#include <adk/seq_lock.h>
#include <adk/io_engine/property.h>
#include <adk/io_engine/acceptor.h>

namespace adk_impl
{

namespace io_engine
{

class TcpEndpoint;
class EventHandler;
class AcceptHandler;
class TcpEngineImpl;

using verbs::ITcpAcceptor;
using verbs::ITcpEndpoint;

class TcpAcceptor final : public Acceptor
{
public:
    TcpAcceptor(TcpEngineImpl* const tcp_engine_impl);

    int32_t Init(const Property& accept_props);

    void Exit();

    void Close() override;

    const std::string& listen_ip() const override
    {
        assert(tcp_acceptor_);
        return tcp_acceptor_->listen_ip();
    }

    uint16_t listen_port() const override
    {
        assert(tcp_acceptor_);
        return tcp_acceptor_->listen_port();
    }

    TcpEndpoint* Accept();

    static void Destroy(TcpAcceptor* const acceptor_impl);

    void set_event_handler(EventHandler* const event_handler)
    {
        assert(event_handler);
        event_handler_ = event_handler;
    }

    void set_accept_handler(AcceptHandler* const accept_handler)
    {
        assert(accept_handler);
        accept_handler_ = accept_handler;
    }

    ITcpAcceptor* tcp_acceptor() const
    {
        return tcp_acceptor_;
    }

    void accept_lock()
    {
        accept_lock_.WriteBegin<true>();
    }

    void accept_unlock()
    {
        accept_lock_.WriteEnd();
    }

    AcceptHandler* accept_handler() const
    {
        assert(accept_lock_.IsLocked());
        return accept_handler_;
    }

    bool valid() const
    {
        return valid_;
    }

private:
    Property       props_;
    bool           valid_;
    SeqLock        accept_lock_;
    ITcpAcceptor*  tcp_acceptor_;
    EventHandler*  event_handler_;
    AcceptHandler* accept_handler_;
    TcpEngineImpl* tcp_engine_impl_;
};

}

}

#endif