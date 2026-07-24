#include <adk/memory_pool_variant.h>
#include <adk_pack/memory_pool_variant.h>

namespace adk
{

namespace variant
{

using MemoryPoolSPSCImpl = adk_impl::variant::MemoryPool<adk_impl::variant::SPSCQueue>;
using MemoryPoolSPMCImpl = adk_impl::variant::MemoryPool<adk_impl::variant::SPMCQueue>;
using MemoryPoolMPSCImpl = adk_impl::variant::MemoryPool<adk_impl::variant::MPSCQueue>;
using MemoryPoolMPMCImpl = adk_impl::variant::MemoryPool<adk_impl::variant::MPMCQueue>;

MemoryPoolSPSC* MemoryPoolSPSC::Create(const std::map<uint32_t, std::pair<uint32_t, std::string>>& memory_pool_property)
{
    return (MemoryPoolSPSC*)MemoryPoolSPSCImpl::Create(memory_pool_property);
}

void MemoryPoolSPSC::Delete(MemoryPoolSPSC* memory_pool)
{
    MemoryPoolSPSCImpl::Delete((MemoryPoolSPSCImpl*)memory_pool);
}

void* MemoryPoolSPSC::NewMemoryBlock(size_t length)
{
    return ((MemoryPoolSPSCImpl*)this)->NewMemory<true>(length);
}

void* MemoryPoolSPSC::NewMemoryNonblock(size_t length)
{
    return ((MemoryPoolSPSCImpl*)this)->NewMemory<false>(length);
}

void MemoryPoolSPSC::DeleteMemory(void* buffer)
{
    ((MemoryPoolSPSCImpl*)this)->DeleteMemory(buffer);
}

MemoryPoolSPMC* MemoryPoolSPMC::Create(const std::map<uint32_t, std::pair<uint32_t, std::string>>& memory_pool_property)
{
    return (MemoryPoolSPMC*)MemoryPoolSPMCImpl::Create(memory_pool_property);
}

void MemoryPoolSPMC::Delete(MemoryPoolSPMC* memory_pool)
{
    MemoryPoolSPMCImpl::Delete((MemoryPoolSPMCImpl*)memory_pool);
}

void* MemoryPoolSPMC::NewMemoryBlock(size_t length)
{
    return ((MemoryPoolSPMCImpl*)this)->NewMemory<true>(length);
}

void* MemoryPoolSPMC::NewMemoryNonblock(size_t length)
{
    return ((MemoryPoolSPMCImpl*)this)->NewMemory<false>(length);
}

void MemoryPoolSPMC::DeleteMemory(void* buffer)
{
    ((MemoryPoolSPMCImpl*)this)->DeleteMemory(buffer);
}


MemoryPoolMPSC* MemoryPoolMPSC::Create(const std::map<uint32_t, std::pair<uint32_t, std::string>>& memory_pool_property)
{
    return (MemoryPoolMPSC*)MemoryPoolMPSCImpl::Create(memory_pool_property);
}

void MemoryPoolMPSC::Delete(MemoryPoolMPSC* memory_pool)
{
    MemoryPoolMPSCImpl::Delete((MemoryPoolMPSCImpl*)memory_pool);
}

void* MemoryPoolMPSC::NewMemoryBlock(size_t length)
{
    return ((MemoryPoolMPSCImpl*)this)->NewMemory<true>(length);
}

void* MemoryPoolMPSC::NewMemoryNonblock(size_t length)
{
    return ((MemoryPoolMPSCImpl*)this)->NewMemory<false>(length);
}

void MemoryPoolMPSC::DeleteMemory(void* buffer)
{
    ((MemoryPoolMPSCImpl*)this)->DeleteMemory(buffer);
}

MemoryPoolMPMC* MemoryPoolMPMC::Create(const std::map<uint32_t, std::pair<uint32_t, std::string>>& memory_pool_property)
{
    return (MemoryPoolMPMC*)MemoryPoolMPMCImpl::Create(memory_pool_property);
}

void MemoryPoolMPMC::Delete(MemoryPoolMPMC* memory_pool)
{
    MemoryPoolMPMCImpl::Delete((MemoryPoolMPMCImpl*)memory_pool);
}

void* MemoryPoolMPMC::NewMemoryBlock(size_t length)
{
    return ((MemoryPoolMPMCImpl*)this)->NewMemory<true>(length);
}

void* MemoryPoolMPMC::NewMemoryNonblock(size_t length)
{
    return ((MemoryPoolMPMCImpl*)this)->NewMemory<false>(length);
}

void MemoryPoolMPMC::DeleteMemory(void* buffer)
{
    ((MemoryPoolMPMCImpl*)this)->DeleteMemory(buffer);
}

}

}