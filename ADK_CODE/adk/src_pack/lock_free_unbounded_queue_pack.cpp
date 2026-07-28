#include <adk/lock_free_unbounded_queue.h>
#include <adk_pack/lock_free_unbounded_queue.h>

namespace adk
{

namespace impl
{

struct ElementHolder
{
    uint64_t empty_holder;
};

typedef void(*assign_fun)(void*, const void*);

using SPSCUnboundedQueueImpl = adk_impl::SPSCUnboundedQueue<ElementHolder>;

SPSCUnboundedQueue* SPSCUnboundedQueue::Create(const std::string& name, uint32_t cache_size, uint32_t node_size, uint32_t element_size)
{
    SPSCUnboundedQueueImpl* _queue_impl = SPSCUnboundedQueueImpl::Create(name, cache_size, node_size, element_size);
    if (nullptr != _queue_impl)
    {
        SPSCUnboundedQueue* queue = new SPSCUnboundedQueue;
        queue->queue_impl_ = _queue_impl;
        return queue;
    }

    return nullptr;
}

int32_t SPSCUnboundedQueue::Push(const void* element)
{
    ElementHolder* buffer = reinterpret_cast<SPSCUnboundedQueueImpl*>(queue_impl_)->AllocEntry();
    assert(buffer);

    reinterpret_cast<assign_fun>(assign_)(((void*)(buffer + 1)), element);
    return reinterpret_cast<SPSCUnboundedQueueImpl*>(queue_impl_)->PostEntry();
}

int32_t SPSCUnboundedQueue::Pop(void* element)
{
    ElementHolder* buffer = reinterpret_cast<SPSCUnboundedQueueImpl*>(queue_impl_)->WaitEntry();
    if (ADK_UNLIKELY(nullptr == buffer))
    {
        return adk_impl::ErrorCode::kQueueEmpty;
    }

    reinterpret_cast<assign_fun>(assign_)(element, ((void*)(buffer + 1)));
    reinterpret_cast<SPSCUnboundedQueueImpl*>(queue_impl_)->FreeEntry();
    return adk_impl::ErrorCode::kSuccess;
}

void* SPSCUnboundedQueue::Head()
{
    ElementHolder* buffer = reinterpret_cast<SPSCUnboundedQueueImpl*>(queue_impl_)->Head();
    if (nullptr != buffer)
    {
        return ((void*)(buffer + 1));
    }
    return nullptr;
}

int32_t SPSCUnboundedQueue::Pop()
{
    return reinterpret_cast<SPSCUnboundedQueueImpl*>(queue_impl_)->Pop();
}

void* SPSCUnboundedQueue::ElementAt(uint64_t index)
{
    ElementHolder* buffer = reinterpret_cast<SPSCUnboundedQueueImpl*>(queue_impl_)->ElementAt(index);
    if (nullptr != buffer)
    {
        return ((void*)(buffer + 1));
    }
    return nullptr;
}

void SPSCUnboundedQueue::ForeachElement(const std::function<bool(void*)>& callback)
{
    reinterpret_cast<SPSCUnboundedQueueImpl*>(queue_impl_)->ForeachElement([callback](ElementHolder* buffer) {
        return callback((void*)(buffer + 1));
    });
}

}

}