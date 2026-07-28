#include <adk/lock_free_queue_variant.h>
#include <adk_pack/lock_free_queue_variant.h>

namespace adk
{

namespace variant
{

struct EmptyHolder
{
};

using adk_impl::ErrorCode;
typedef void(*assign_fun)(void*, const void*);

using SPSCQueueType = adk_impl::variant::SPSCQueue<EmptyHolder>;
SPSCQueueImpl* SPSCQueueImpl::Create(const std::string &name, uint32_t queue_size, uint32_t element_size)
{
    SPSCQueueType* queue_impl = SPSCQueueType::Create(name, queue_size, element_size);
    if (nullptr != queue_impl)
    {
        SPSCQueueImpl* queue = new SPSCQueueImpl;
        queue->queue_impl_ = (void*)queue_impl;
        return queue;
    }

    return nullptr;
}

int32_t SPSCQueueImpl::Delete(SPSCQueueImpl* queue)
{
    int32_t res = ErrorCode::kFailure;
    if (nullptr != queue)
    {
        if (nullptr != queue->queue_impl_)
        {
            res = SPSCQueueType::Delete((SPSCQueueType*)(queue->queue_impl_));
        }

        delete queue;
    }

    return res;
}

int32_t SPSCQueueImpl::Push(const void* payload)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((SPSCQueueType*)queue_impl_)->AllocEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(buffer, payload);
    ((SPSCQueueType*)queue_impl_)->PostEntry(entry_ptr);
    return ErrorCode::kSuccess;
}

int32_t SPSCQueueImpl::Pop(void* payload)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((SPSCQueueType*)queue_impl_)->WaitEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(payload, buffer);
    ((SPSCQueueType*)queue_impl_)->FreeEntry(entry_ptr);
    return ErrorCode::kSuccess;
}

int32_t SPSCQueueImpl::TryPush(const void* payload)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((SPSCQueueType*)queue_impl_)->TryAllocEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(buffer, payload);
    ((SPSCQueueType*)queue_impl_)->PostEntry(entry_ptr);
    return ErrorCode::kSuccess;
}

int32_t SPSCQueueImpl::TryPop(void* payload)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((SPSCQueueType*)queue_impl_)->TryWaitEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(payload, buffer);
    ((SPSCQueueType*)queue_impl_)->FreeEntry(entry_ptr);
    return ErrorCode::kSuccess;
}

uint32_t SPSCQueueImpl::queue_size() const
{
    return ((SPSCQueueType*)(queue_impl_))->queue_size();
}

uint64_t SPSCQueueImpl::length() const
{
    return ((SPSCQueueType*)(queue_impl_))->length();
}

using MPMCQueueType = adk_impl::variant::MPMCQueue<EmptyHolder>;
MPMCQueueImpl* MPMCQueueImpl::Create(const std::string &name, uint32_t queue_size, uint32_t element_size)
{
    MPMCQueueType* queue_impl = MPMCQueueType::Create(name, queue_size, element_size);
    if (nullptr != queue_impl)
    {
        MPMCQueueImpl* queue = new MPMCQueueImpl;
        queue->queue_impl_ = (void*)queue_impl;
        return queue;
    }

    return nullptr;
}

int32_t MPMCQueueImpl::Delete(MPMCQueueImpl* queue)
{
    int32_t res = ErrorCode::kFailure;
    if (nullptr != queue)
    {
        if (nullptr != queue->queue_impl_)
        {
            res = MPMCQueueType::Delete((MPMCQueueType*)(queue->queue_impl_));
        }

        delete queue;
    }

    return res;
}

int32_t MPMCQueueImpl::Push(const void* payload)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((MPMCQueueType*)queue_impl_)->AllocEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(buffer, payload);
    ((MPMCQueueType*)queue_impl_)->PostEntry(entry_ptr);
    return ErrorCode::kSuccess;
}

int32_t MPMCQueueImpl::Pop(void* payload)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((MPMCQueueType*)queue_impl_)->WaitEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(payload, buffer);
    ((MPMCQueueType*)queue_impl_)->FreeEntry(entry_ptr);
    return ErrorCode::kSuccess;
}

int32_t MPMCQueueImpl::TryPush(const void* payload)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((MPMCQueueType*)queue_impl_)->TryAllocEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(buffer, payload);
    ((MPMCQueueType*)queue_impl_)->PostEntry(entry_ptr);
    return ErrorCode::kSuccess;
}

int32_t MPMCQueueImpl::TryPop(void* payload)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((MPMCQueueType*)queue_impl_)->TryWaitEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(payload, buffer);
    ((MPMCQueueType*)queue_impl_)->FreeEntry(entry_ptr);
    return ErrorCode::kSuccess;
}

using SPMCQueueType = adk_impl::variant::SPMCQueue<EmptyHolder>;
SPMCQueueImpl* SPMCQueueImpl::Create(const std::string &name, uint32_t queue_size, uint32_t element_size)
{
    SPMCQueueType* queue_impl = SPMCQueueType::Create(name, queue_size, element_size);
    if (nullptr != queue_impl)
    {
        SPMCQueueImpl* queue = new SPMCQueueImpl;
        queue->queue_impl_ = (void*)queue_impl;
        return queue;
    }

    return nullptr;
}

int32_t SPMCQueueImpl::Delete(SPMCQueueImpl* queue)
{
    int32_t res = ErrorCode::kFailure;
    if (nullptr != queue)
    {
        if (nullptr != queue->queue_impl_)
        {
            res = SPMCQueueType::Delete((SPMCQueueType*)(queue->queue_impl_));
        }

        delete queue;
    }

    return res;
}

int32_t SPMCQueueImpl::Push(const void* payload)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((SPMCQueueType*)queue_impl_)->AllocEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(buffer, payload);
    ((SPMCQueueType*)queue_impl_)->PostEntry(entry_ptr);
    return ErrorCode::kSuccess;
}

int32_t SPMCQueueImpl::Pop(void* payload)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((SPMCQueueType*)queue_impl_)->WaitEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(payload, buffer);
    ((SPMCQueueType*)queue_impl_)->FreeEntry(entry_ptr);
    return ErrorCode::kSuccess;
}

int32_t SPMCQueueImpl::TryPush(const void* payload)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((SPMCQueueType*)queue_impl_)->TryAllocEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(buffer, payload);
    ((SPMCQueueType*)queue_impl_)->PostEntry(entry_ptr);
    return ErrorCode::kSuccess;
}

int32_t SPMCQueueImpl::TryPop(void* payload)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((SPMCQueueType*)queue_impl_)->TryWaitEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(payload, buffer);
    ((SPMCQueueType*)queue_impl_)->FreeEntry(entry_ptr);
    return ErrorCode::kSuccess;
}


using MPSCQueueType = adk_impl::variant::MPSCQueue<EmptyHolder>;
MPSCQueueImpl* MPSCQueueImpl::Create(const std::string &name, uint32_t queue_size, uint32_t element_size)
{
    MPSCQueueType* queue_impl = MPSCQueueType::Create(name, queue_size, element_size);
    if (nullptr != queue_impl)
    {
        MPSCQueueImpl* queue = new MPSCQueueImpl;
        queue->queue_impl_ = (void*)queue_impl;
        return queue;
    }

    return nullptr;
}

int32_t MPSCQueueImpl::Delete(MPSCQueueImpl* queue)
{
    int32_t res = ErrorCode::kFailure;
    if (nullptr != queue)
    {
        if (nullptr != queue->queue_impl_)
        {
            res = MPSCQueueType::Delete((MPSCQueueType*)(queue->queue_impl_));
        }

        delete queue;
    }

    return res;
}

int32_t MPSCQueueImpl::Push(const void* payload)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((MPSCQueueType*)queue_impl_)->AllocEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(buffer, payload);
    ((MPSCQueueType*)queue_impl_)->PostEntry(entry_ptr);
    return ErrorCode::kSuccess;
}

int32_t MPSCQueueImpl::Pop(void* payload)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((MPSCQueueType*)queue_impl_)->WaitEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(payload, buffer);
    ((MPSCQueueType*)queue_impl_)->FreeEntry(entry_ptr);
    return ErrorCode::kSuccess;
}

int32_t MPSCQueueImpl::TryPush(const void* payload)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((MPSCQueueType*)queue_impl_)->TryAllocEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(buffer, payload);
    ((MPSCQueueType*)queue_impl_)->PostEntry(entry_ptr);
    return ErrorCode::kSuccess;
}

int32_t MPSCQueueImpl::TryPop(void* payload)
{
    adk_impl::variant::VariantEntry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((MPSCQueueType*)queue_impl_)->TryWaitEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(payload, buffer);
    ((MPSCQueueType*)queue_impl_)->FreeEntry(entry_ptr);
    return ErrorCode::kSuccess;
}

}

}