#include "last_error.h"
#include "drive_engine.h"
#include "acceptor_impl.h"
#include "endpoint_impl.h"
#include "tcp_engine_impl.h"

#include <adk/io_engine/handler.h>
#include <adk/io_engine/config_key.h>

namespace adk_impl
{

namespace io_engine
{

TcpAcceptor::TcpAcceptor(TcpEngineImpl* const tcp_engine_impl)
{
    valid_ = true;
    tcp_acceptor_ = nullptr;
    event_handler_ = nullptr;
    accept_handler_ = nullptr;
    tcp_engine_impl_ = tcp_engine_impl;
}

int32_t TcpAcceptor::Init(const Property& accept_props)
{
    Exit();

    using namespace config::acceptor;

    assert(tcp_engine_impl_);
    tcp_acceptor_ = ITcpAcceptor::Create(tcp_engine_impl_->tcp_stack(), 
                                         accept_props.GetValue(kListenIp, string()), 
                                         accept_props.GetValue(kListenPort, default_value::kInvalidPort),
                                         accept_props.GetValue(kReuseAddr, false),
                                         accept_props.GetValue(kReusePort, false));
    if (ADK_UNLIKELY(nullptr == tcp_acceptor_))
    {
        SetErrorInfo(errno);
        return ErrorCode::kFailure;
    }

    valid_ = true;
    props_ = accept_props;
    return ErrorCode::kSuccess;
}

void TcpAcceptor::Exit()
{
    if (nullptr != tcp_acceptor_)
    {
        ITcpAcceptor::Destroy(tcp_acceptor_);
        tcp_acceptor_ = nullptr;
    }

    event_handler_ = nullptr;
    accept_handler_ = nullptr;
    valid_ = false;
}

void TcpAcceptor::Close()
{
    assert(tcp_engine_impl_);

    auto* const blank_accept_handler = tcp_engine_impl_->blank_accept_handler();
    assert(blank_accept_handler);

    if (blank_accept_handler != accept_handler_)
    {
        accept_handler_ = blank_accept_handler;

        auto* const drive_engine = tcp_engine_impl_->drive_engine();
        assert(drive_engine);

        auto* const control_actor = drive_engine->control_actor();
        assert(control_actor);

        if (control_actor->this_actor_id() != std::this_thread::get_id())
        {
            while (ADK_UNLIKELY(accept_lock_.IsLocked<true>()))
            {
                usleep(0);
            }
        }

        tcp_engine_impl_->ToCloseAcceptor(this);
        valid_ = false;
    }
}

TcpEndpoint* TcpAcceptor::Accept()
{
    assert(tcp_acceptor_);

    auto* const tcp_endpoint = tcp_acceptor_->Accept();
    if (nullptr == tcp_endpoint)
    {
        return nullptr;
    }

    assert(event_handler_);
    assert(tcp_engine_impl_);

    auto* const endpoint_impl = tcp_engine_impl_->CreateEndpoint(props_, 
                                                                 event_handler_, 
                                                                 tcp_endpoint);
    if (nullptr != endpoint_impl)
    {
        return endpoint_impl;
    }

    EndpointHeader void_header;
    void_header.tcp_endpoint = tcp_endpoint;
    void_header.tx_message_queue = tcp_engine_impl_->blank_endpoint_header()->tx_message_queue;

    TcpEndpoint void_endpoint(&void_header);
    void_endpoint.set_to_close();

    EventNoEpResource event_no_resource;
    event_handler_->OnEvent(&void_endpoint, &event_no_resource);

    verbs::ITcpEndpoint::Destroy(tcp_endpoint);

    return nullptr;
}

void TcpAcceptor::Destroy(TcpAcceptor* const acceptor_impl)
{
    assert(acceptor_impl);
    acceptor_impl->Exit();
    delete acceptor_impl;
}

}

}
