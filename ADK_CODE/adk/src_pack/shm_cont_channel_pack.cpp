#include <adk/shm_cont_channel.h>
#include <adk_pack/shm_cont_channel.h>

namespace adk
{

namespace sccl
{

using EntryImpl = adk_impl::sccl::Entry;
void* Entry::Buffer()
{
    return reinterpret_cast<EntryImpl*>(this)->Buffer();
}

uint32_t Entry::BufferSize() const
{
    return reinterpret_cast<const EntryImpl*>(this)->BufferSize();
}

uint16_t Entry::Index() const
{
    return reinterpret_cast<const EntryImpl*>(this)->Index();
}

using AgentImpl = adk_impl::sccl::Agent;
using AgentEventHandlerImpl = adk_impl::sccl::AgentEventHandler;
Agent* Agent::Create(const std::string& name, 
                     AgentEventHandler* event_handler,
                     bool do_recovery,
                     uint32_t memory_size, 
                     uint32_t max_message_size)
{
    auto* const agent = AgentImpl::Create(
        name, 
        reinterpret_cast<AgentEventHandlerImpl*>(event_handler),
        memory_size,
        max_message_size);
    return reinterpret_cast<Agent*>(agent);
}

void Agent::Destroy(Agent* agent)
{
    AgentImpl::Destroy(reinterpret_cast<AgentImpl*>(agent));
}

struct Entry* Agent::TryWaitEntry()
{
    return reinterpret_cast<Entry*>(reinterpret_cast<AgentImpl*>(this)->TryWaitEntry());
}

void Agent::FreeEntry(struct Entry* entry_ptr)
{
    reinterpret_cast<AgentImpl*>(this)->FreeEntry(reinterpret_cast<EntryImpl*>(entry_ptr));
}

using ProxyImpl = adk_impl::sccl::Proxy;
Proxy* Proxy::Create(const std::string& agent_name, 
                     const std::string& proxy_name,
                     const std::function<bool()>& broken_cb)
{
    auto* const proxy = ProxyImpl::Create(agent_name, proxy_name, broken_cb);
    return reinterpret_cast<Proxy*>(proxy);
}

void Proxy::Destroy(Proxy* proxy)
{
    ProxyImpl::Destroy(reinterpret_cast<ProxyImpl*>(proxy));
}

void* Proxy::AllocBuffer(uint32_t length)
{
    return reinterpret_cast<ProxyImpl*>(this)->AllocBuffer(length);
}

void Proxy::PostBuffer(void* buffer, uint32_t buf_size)
{
    reinterpret_cast<ProxyImpl*>(this)->PostBuffer(buffer, buf_size);
}

}

}