#include <adk/lock_free_msg_queue.h>
#include <adk_pack/lock_free_msg_queue.h>

namespace adk
{

typedef void(*assign_fun)(void*, const void*);

struct EmptyHolder
{
};

MPSCQueue* MPSCQueue::Create(const std::string& name, uint32_t entry_payload_size, uint32_t queue_size)
{
    return (MPSCQueue*)(adk_impl::MPSCQueue::Create(name, entry_payload_size, queue_size));
}

int32_t MPSCQueue::Push(const void* payload, void* assign)
{
    adk_impl::Entry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((adk_impl::MPSCQueue*)this)->AllocEntry(&entry_ptr));
    char* buffer = (char*)entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign)(buffer, payload);
    return ((adk_impl::MPSCQueue*)this)->PostEntry(entry_ptr);
}

int32_t MPSCQueue::Pop(void* payload, void* assign)
{
    adk_impl::Entry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((adk_impl::MPSCQueue*)this)->WaitEntry(&entry_ptr));
    char* buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign)(payload, buffer);
    return ((adk_impl::MPSCQueue*)this)->FreeEntry(entry_ptr);
}

SPMCQueue* SPMCQueue::Create(const string& name, uint32_t entry_payload_size, uint32_t queue_size)
{
    return (SPMCQueue*)(adk_impl::SPMCQueue::Create(name, entry_payload_size, queue_size));
}

int32_t SPMCQueue::Push(const void* payload, void* assign)
{
    adk_impl::Entry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((adk_impl::SPMCQueue*)this)->AllocEntry(&entry_ptr));
    char* buffer = (char*)entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign)(buffer, payload);
    return ((adk_impl::SPMCQueue*)this)->PostEntry(entry_ptr);
}

int32_t SPMCQueue::Pop(void* payload, void* assign)
{
    adk_impl::Entry* entry_ptr;
    ADK_CHECK_RET_SUCCESS(((adk_impl::SPMCQueue*)this)->WaitEntry(&entry_ptr));
    char* buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign)(payload, buffer);
    return ((adk_impl::SPMCQueue*)this)->FreeEntry(entry_ptr);
}

void* SPSCQueueBase::Create(const string& name, uint32_t entry_payload_size, uint32_t queue_size)
{
    adk_impl::MPSCQueue* mpsc_queue = adk_impl::MPSCQueue::Create(name, entry_payload_size, queue_size);
    return reinterpret_cast<void*>(mpsc_queue);
}

int32_t SPSCQueueBase::Push(const void* payload, void* assign)
{
    adk_impl::Entry* entry_ptr;
    adk_impl::SPSCQueue<EmptyHolder>* const queue_impl = reinterpret_cast<adk_impl::SPSCQueue<EmptyHolder>*>(this);

    ADK_CHECK_RET_SUCCESS(queue_impl->AllocEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign)(buffer, payload);
    return queue_impl->PostEntry(entry_ptr);
}

int32_t SPSCQueueBase::Pop(void* payload, void* assign)
{
    adk_impl::Entry* entry_ptr;
    adk_impl::SPSCQueue<EmptyHolder>* const queue_impl = reinterpret_cast<adk_impl::SPSCQueue<EmptyHolder>*>(this);

    ADK_CHECK_RET_SUCCESS(queue_impl->WaitEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign)(payload, buffer);
    return queue_impl->FreeEntry(entry_ptr);
}

ConcurrentQueueBase::ConcurrentQueueBase(int32_t nr_queue, uint32_t payload_size)
{
    nr_queues_ = nr_queue;
    payload_size_ = payload_size;
    release_alert_ = false;
    producer_cursor_ = 0;
    consumer_cursor_ = 0;
    spsc_queues_ = nullptr;
}

int32_t ConcurrentQueueBase::TryPush(void* const queue_impl, const void* payload)
{
    adk_impl::Entry* entry_ptr;
    adk_impl::SPSCQueue<EmptyHolder>* const queue = reinterpret_cast<adk_impl::SPSCQueue<EmptyHolder>*>(queue_impl);
    ADK_CHECK_RET_SUCCESS(queue->AllocEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(buffer, payload);
    return queue->PostEntry(entry_ptr);
}

int32_t ConcurrentQueueBase::Push(void* const queue_impl, const void* payload)
{
    adk_impl::Entry* entry_ptr;
    adk_impl::SPSCQueue<EmptyHolder>* queue = reinterpret_cast<adk_impl::SPSCQueue<EmptyHolder>*>(queue_impl);

    int32_t ec;
    while (ADK_UNLIKELY((ec = queue->AllocEntry(&entry_ptr)) == ErrorCode::kQueueFull))
    {
        if (release_alert_)
        {
            break;
        }

        ADK_PAUSE();
    }

    if (ErrorCode::kSuccess == ec)
    {
        char* const buffer = entry_ptr->buffer;
        reinterpret_cast<assign_fun>(assign_)(buffer, payload);
        queue->PostEntry(entry_ptr);
    }
    return ec;
}

int32_t ConcurrentQueueBase::TryPop(void* const queue_impl, void* payload)
{
    adk_impl::Entry* entry_ptr;
    adk_impl::SPSCQueue<EmptyHolder>* const queue = reinterpret_cast<adk_impl::SPSCQueue<EmptyHolder>*>(queue_impl);
    ADK_CHECK_RET_SUCCESS(queue->WaitEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(payload, buffer);
    return queue->FreeEntry(entry_ptr);
}

int32_t ConcurrentQueueBase::Init(void* queue)
{
    assert(nr_queues_ == 1);

    spsc_queues_ = reinterpret_cast<QueueContainerImpl*>(malloc(sizeof(QueueContainerImpl)));
    spsc_queues_[0].queue_impl = queue;
    return ErrorCode::kSuccess;
}

int32_t ConcurrentQueueBase::Init(const string& name, uint32_t queue_size)
{
    queue_name_ = name;
    char name_buf[1024];
    spsc_queues_ = reinterpret_cast<QueueContainerImpl*>(malloc(sizeof(QueueContainerImpl) * nr_queues_));
    for (int32_t i = 0; i < nr_queues_; ++i)
    {
        snprintf(name_buf, 1023, "%s_%d", name.c_str(), i + 1);
        name_buf[1023] = 0;
        spsc_queues_[i].queue_impl = (void*)(adk_impl::SPSCQueue<EmptyHolder>::Create(&name_buf[0], payload_size_, queue_size));
    }
    return ErrorCode::kSuccess;
}

void* ConcurrentQueueBase::GetProducerByIndex(int32_t index, void* assign)
{
    if (ADK_UNLIKELY(index >= nr_queues_))
    {
        return nullptr;
    }

    ConcurrentQueueBase* ccq = new ConcurrentQueueBase(1, payload_size_);
    ccq->assign_ = assign;
    ccq->Init(spsc_queues_[index].queue_impl);

    return ccq;
}

void* ConcurrentQueueBase::GetConsumerByIndex(int32_t index, void* assign)
{
    return GetProducerByIndex(index, assign);
}

int32_t ConcurrentQueueBase::TryPush(const void* payload)
{
    if (nr_queues_ == 1)
    {
        return TryPush(spsc_queues_[0].queue_impl, payload);
    }

    ++producer_cursor_;
    if (producer_cursor_ == (uint32_t)nr_queues_)
    {
        producer_cursor_ = 0;
    }

    return TryPush(spsc_queues_[producer_cursor_].queue_impl, payload);
}

int32_t ConcurrentQueueBase::Push(const void* payload)
{
    if (nr_queues_ == 1)
    {
        return Push(spsc_queues_[0].queue_impl, payload);
    }

    if (producer_cursor_ == (uint32_t)nr_queues_)
    {
        producer_cursor_ = 0;
    }

    int32_t ec = Push(spsc_queues_[producer_cursor_].queue_impl, payload);

    ++producer_cursor_;
    return ec;
}

int32_t ConcurrentQueueBase::ReorderPush(const void* payload, uint64_t seq)
{
    assert(nr_queues_ == 1);

    int32_t ec;

    adk_impl::Entry* entry_ptr;
    adk_impl::SPSCQueue<EmptyHolder>* queue = reinterpret_cast<adk_impl::SPSCQueue<EmptyHolder>*>(spsc_queues_[0].queue_impl);
    while (ADK_UNLIKELY((ec = queue->ReorderAllocEntry(&entry_ptr, seq)) == ErrorCode::kQueueFull))
    {
        if (release_alert_)
        {
            break;
        }

        ADK_PAUSE();
    }

    if (ErrorCode::kSuccess == ec)
    {
        char* const buffer = entry_ptr->buffer;
        reinterpret_cast<assign_fun>(assign_)(buffer, payload);
        queue->RecorderPostEntry(entry_ptr, seq);
    }
    return ec;
}

int32_t ConcurrentQueueBase::TryPush(const void* payload, int index)
{
    assert(index < nr_queues_);

    return TryPush(spsc_queues_[index].queue_impl, payload);
}

int32_t ConcurrentQueueBase::BroadCast(const void* payload)
{
    int32_t ec;
    for (int32_t index = 0; index < nr_queues_; ++index)
    {
        if (ADK_UNLIKELY((ec = Push(spsc_queues_[index].queue_impl, payload)) != ErrorCode::kSuccess))
        {
            return ec;
        }
    }

    return ErrorCode::kSuccess;
}

int32_t ConcurrentQueueBase::TryPop(void* payload)
{
    if (nr_queues_ == 1)
    {
        return TryPop(spsc_queues_[0].queue_impl, payload);
    }

    ++consumer_cursor_;
    if (consumer_cursor_ == (uint32_t)nr_queues_)
    {
        consumer_cursor_ = 0;
    }

    return TryPop(spsc_queues_[consumer_cursor_].queue_impl, payload);
}

int32_t ConcurrentQueueBase::TryPop(void* payload, int index)
{
    assert(index < nr_queues_);
    return TryPop(spsc_queues_[index].queue_impl, payload);
}

int32_t ConcurrentQueueBase::ReorderPop(void* payload)
{
    assert(nr_queues_ == 1);
    adk_impl::SPSCQueue<EmptyHolder>* queue = reinterpret_cast<adk_impl::SPSCQueue<EmptyHolder>*>(spsc_queues_[0].queue_impl);

    adk_impl::Entry* entry_ptr;

    ADK_CHECK_RET_SUCCESS(queue->RecorderWaitEntry(&entry_ptr));
    char* const buffer = entry_ptr->buffer;
    reinterpret_cast<assign_fun>(assign_)(payload, buffer);
    return queue->RecorderFreeEntry(entry_ptr);
}

void ConcurrentQueueBase::reset_consumer_cursor()
{
    consumer_cursor_ = 0;
}

void ConcurrentQueueBase::reset_producer_cursor()
{
    producer_cursor_ = 0;
}

void ConcurrentQueueBase::GetStats(std::vector<QueueStats>& stats)
{
    stats.resize(nr_queues_);

    for (int32_t index = 0; index < nr_queues_; ++index)
    {
        adk_impl::QueueStats& queue_stats = (adk_impl::QueueStats&)(stats[index]);
        reinterpret_cast<adk_impl::SPSCQueue<EmptyHolder>*>(spsc_queues_[index].queue_impl)->GetStats(queue_stats);
    }
}

void ConnectorBase::Delay()
{
    usleep(0);
}

}