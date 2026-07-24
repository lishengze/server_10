#include <adk/io_engine/endpoint.h>
#include <adk/io_engine/property.h>
#include <adk_pack/io_engine/endpoint.h>
#include <adk_pack/io_engine/property.h>

namespace adk
{

namespace io_engine
{

using MessageImp = adk_impl::io_engine::Message;
using PropertyImp = adk_impl::io_engine::Property;

class EndpointImp : public adk_impl::io_engine::Endpoint
{
public:
    using ImplType = adk_impl::io_engine::Endpoint;

    int32_t SendMsgBlockImpl(Message* const msg)
    {
        return SendMsgBlock(reinterpret_cast<MessageImp*>(msg));
    }

    int32_t SendMsgUnblockImpl(Message* const msg)
    {
        return SendMsgUnblock(reinterpret_cast<MessageImp*>(msg));
    }

    int32_t SendMsgBlockUnsafeImpl(Message* const msg)
    {
        return SendMsgBlockUnsafe(reinterpret_cast<MessageImp*>(msg));
    }

    int32_t SendMsgUnblockUnsafeImpl(Message* const msg)
    {
        return SendMsgUnblockUnsafe(reinterpret_cast<MessageImp*>(msg));
    }
};

bool Endpoint::SetRxCork()
{
    return reinterpret_cast<EndpointImp*>(this)->SetRxCork();
}

bool Endpoint::SetRxUncork()
{
    return reinterpret_cast<EndpointImp*>(this)->SetRxUncork();
}

Message* Endpoint::NewMessage(uint32_t len)
{
    return (Message*)(reinterpret_cast<EndpointImp*>(this)->NewMessage(len));
}

void Endpoint::DeleteMessage(Message* message)
{
    reinterpret_cast<EndpointImp*>(this)->DeleteMessage((MessageImp*)message);
}

int32_t Endpoint::SendMsg(const std::vector<Endpoint*>& endpoint_vec, Message* const msg)
{
    std::vector<EndpointImp::ImplType*> ep_impl_vec;
    for (const auto& ep_node : endpoint_vec)
    {
        ep_impl_vec.push_back(reinterpret_cast<EndpointImp::ImplType*>(ep_node));
    }

    return EndpointImp::ImplType::SendMsg(ep_impl_vec, reinterpret_cast<MessageImp*>(msg));
}

int32_t Endpoint::GetPendingMsg(const std::function<int32_t(Message*)>& msg_handler)
{
    return reinterpret_cast<EndpointImp*>(this)->GetPendingMsg([=](MessageImp* msg_impl)->int32_t {
        return msg_handler(reinterpret_cast<Message*>(msg_impl));
    });
}

ssize_t Endpoint::Recv(char *buf, size_t len)
{
    return reinterpret_cast<EndpointImp*>(this)->Recv(buf, len);
}

void Endpoint::UpdateProperty(const Property& props)
{
    return reinterpret_cast<EndpointImp*>(this)->UpdateProperty((const PropertyImp&)props);
}

void Endpoint::Close(int32_t mode)
{
    return reinterpret_cast<EndpointImp*>(this)->Close(mode);
}

bool Endpoint::IsReady() const
{
    return reinterpret_cast<const EndpointImp*>(this)->IsReady();
}

void Endpoint::Shutdown()
{
    reinterpret_cast<EndpointImp*>(this)->Shutdown();
}

int32_t Endpoint::endpoint_id() const
{
    return reinterpret_cast<const EndpointImp*>(this)->endpoint_id();
}

uint32_t Endpoint::sub_index() const
{
    return reinterpret_cast<const EndpointImp*>(this)->sub_index();
}

bool Endpoint::is_singleton() const
{
    return reinterpret_cast<const EndpointImp*>(this)->is_singleton();
}

void Endpoint::set_private_ctx(void* const private_ctx)
{
    reinterpret_cast<EndpointImp*>(this)->set_private_ctx(private_ctx);
}

void* Endpoint::private_ctx() const
{
    return reinterpret_cast<const EndpointImp*>(this)->private_ctx();
}

void Endpoint::set_share_ctx(void* const share_ctx)
{
    reinterpret_cast<EndpointImp*>(this)->set_share_ctx(share_ctx);
}

void* Endpoint::share_ctx() const
{
    return reinterpret_cast<const EndpointImp*>(this)->share_ctx();
}

const std::string& Endpoint::remote_ip() const
{
    return reinterpret_cast<const EndpointImp*>(this)->remote_ip();
}

const uint16_t Endpoint::remote_port() const
{
    return reinterpret_cast<const EndpointImp*>(this)->remote_port();
}

const std::string& Endpoint::local_ip() const
{
    return reinterpret_cast<const EndpointImp*>(this)->local_ip();
}

const uint16_t Endpoint::local_port() const
{
    return reinterpret_cast<const EndpointImp*>(this)->local_port();
}

int32_t Endpoint::SendMsgBlock(Message* const msg)
{
    return reinterpret_cast<EndpointImp*>(this)->SendMsgBlockImpl(msg);
}

int32_t Endpoint::SendMsgUnblock(Message* const msg)
{
    return reinterpret_cast<EndpointImp*>(this)->SendMsgUnblockImpl(msg);
}

int32_t Endpoint::SendMsgBlockUnsafe(Message* const msg)
{
    return reinterpret_cast<EndpointImp*>(this)->SendMsgBlockUnsafeImpl(msg);
}

int32_t Endpoint::SendMsgUnblockUnsafe(Message* const msg)
{
    return reinterpret_cast<EndpointImp*>(this)->SendMsgUnblockUnsafeImpl(msg);
}

}

}