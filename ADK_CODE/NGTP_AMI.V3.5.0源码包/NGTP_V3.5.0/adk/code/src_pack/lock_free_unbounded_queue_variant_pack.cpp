#include <adk/lock_free_unbounded_queue_variant.h>
#include <adk_pack/lock_free_unbounded_queue_variant.h>

namespace adk
{

namespace variant
{

struct EmptyHolder
{
    uint64_t empty_holder;
};

using adk_impl::ErrorCode;
typedef void(*assign_fun)(void*, const void*);

constexpr uint32_t kEmptyHolderSize = sizeof(EmptyHolder);

using MPSCUbdQueue = adk_impl::variant::MPSCUnboundedQueue<EmptyHolder>;

MPSCUnboundedQueueImpl* MPSCUnboundedQueueImpl::Create(const string& name, uint32_t node_size, uint32_t element_size)
{
    element_size = ADK_ROUND_UP(element_size, 8);
    MPSCUbdQueue* queue_cache = MPSCUbdQueue::Create(name, node_size, 10, 2, element_size);
    if (nullptr != queue_cache)
    {
        MPSCUnboundedQueueImpl* queue_impl = new MPSCUnboundedQueueImpl;
        assert(queue_impl);
        queue_impl->queue_cache_ = queue_cache;
        return queue_impl;
    }
    return nullptr;
}

void MPSCUnboundedQueueImpl::Delete(MPSCUnboundedQueueImpl* queue_impl)
{
    if (nullptr != queue_impl)
    {
        if (nullptr != queue_impl->queue_cache_)
        {
            MPSCUbdQueue::Delete((MPSCUbdQueue*)(queue_impl->queue_cache_));
        }
        delete queue_impl;
    }
}

int32_t MPSCUnboundedQueueImpl::Push(const void* element)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((MPSCUbdQueue*)queue_cache_)->AllocEntry(&entry_ptr));
    char* buffer = (char*)entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(buffer + kEmptyHolderSize, element);
    return ((MPSCUbdQueue*)queue_cache_)->PostEntry(entry_ptr);
}

int32_t MPSCUnboundedQueueImpl::Pop(void* element)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((MPSCUbdQueue*)queue_cache_)->WaitEntry(&entry_ptr));
    char* buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(element, buffer + kEmptyHolderSize);
    return ((MPSCUbdQueue*)queue_cache_)->FreeEntry(entry_ptr);
}

uint64_t MPSCUnboundedQueueImpl::length()
{
    return ((MPSCUbdQueue*)queue_cache_)->length();
}

void* MPSCUnboundedQueueImpl::Alloc()
{
    adk_impl::variant::VariantEntry* entry_ptr;
    if(ErrorCode::kSuccess != ((MPSCUbdQueue*)queue_cache_)->AllocEntry(&entry_ptr))
    {
        return nullptr;
    }
    char* buffer = (char*)entry_ptr->buffer;
    return buffer + kEmptyHolderSize;
}

int32_t MPSCUnboundedQueueImpl::Post(const void* buffer)
{
    const char* entry_ptr = ((const char*)buffer) - kEmptyHolderSize - offsetof(adk_impl::variant::VariantEntry, buffer); 
    return ((MPSCUbdQueue*)queue_cache_)->PostEntry((adk_impl::variant::VariantEntry*)entry_ptr);
}

void* MPSCUnboundedQueueImpl::Top()
{
    adk_impl::variant::VariantEntry* entry_ptr;
    if(ErrorCode::kSuccess != ((MPSCUbdQueue*)queue_cache_)->WaitEntry(&entry_ptr))
    {
        return nullptr;
    }
    char* buffer = entry_ptr->buffer;
    return  buffer + kEmptyHolderSize;
}


int32_t MPSCUnboundedQueueImpl::Commit(const void* buffer)
{
    const char* entry_ptr = ((const char*)buffer) - kEmptyHolderSize - offsetof(adk_impl::variant::VariantEntry, buffer); 
    return ((MPSCUbdQueue*)queue_cache_)->FreeEntry((adk_impl::variant::VariantEntry*)entry_ptr);
}

}

}