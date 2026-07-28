#include <adk/io_engine/tcp_engine.h>
#include <adk_pack/io_engine/property.h>
#include <adk_pack/io_engine/tcp_engine.h>

namespace adk
{

namespace io_engine
{

using PropertyImp = adk_impl::Property;
using MessageImp = adk_impl::io_engine::Message;
using TcpEngineImp = adk_impl::io_engine::TcpEngine;

TcpEngine* TcpEngine::Create(const Property& engine_props)
{
    const PropertyImp& prop_trans = reinterpret_cast<const PropertyImp&>(engine_props);
    return (TcpEngine*)(TcpEngineImp::Create(prop_trans));
}

void TcpEngine::Destroy(TcpEngine* const tcp_engine)
{
    TcpEngineImp::Destroy(reinterpret_cast<TcpEngineImp*>(tcp_engine));
}

const char* TcpEngine::GetLastError()
{
    return TcpEngineImp::GetLastError();
}

Acceptor* TcpEngine::Accept(const Property& accept_props)
{
    const PropertyImp& prop_trans = reinterpret_cast<const PropertyImp&>(accept_props);
    return (Acceptor*)reinterpret_cast<TcpEngineImp*>(this)->Accept(prop_trans);
}

Endpoint* TcpEngine::Connect(const Property& connect_props)
{
    const PropertyImp& prop_trans = reinterpret_cast<const PropertyImp&>(connect_props);
    return (Endpoint*)reinterpret_cast<TcpEngineImp*>(this)->Connect(prop_trans);
}

Message* TcpEngine::NewMessage(uint32_t len)
{
    return (Message*)reinterpret_cast<TcpEngineImp*>(this)->NewMessage(len);
}

void TcpEngine::DeleteMessage(Message* message)
{
    reinterpret_cast<TcpEngineImp*>(this)->DeleteMessage((MessageImp*)message);
}

int32_t TcpEngine::CollectIndicator(std::string& indicator)
{
    return reinterpret_cast<TcpEngineImp*>(this)->CollectIndicator(indicator);
}

TcpEngine::StackType TcpEngine::GetStackType(const std::string& message_ip)
{
    TcpEngineImp::StackType stack_type = TcpEngineImp::GetStackType(message_ip);
    return static_cast<TcpEngine::StackType>(stack_type);
}
}

}