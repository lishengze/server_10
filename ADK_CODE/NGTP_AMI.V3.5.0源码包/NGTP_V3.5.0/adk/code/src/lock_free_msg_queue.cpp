#include <iostream>

#include <adk/shm.h>
#include <adk/lock_free_msg_queue.h>

namespace adk_impl
{

#define QUEUE_CURSOR_INIT_VALUE      1

__thread uint64_t g_enqueue_sqn = 0;
__thread Entry*   g_head_entry = NULL;

// note: the constructor will not be called !!!
MPSCQueue::MPSCQueue()
    :   mem_header_(NULL),
        entries_(NULL),
        entry_size_(0),
        entry_bits_(0),
        queue_mask_(0),
        queue_size_(0),
        release_alert_(false),
        reserve_threshold_(0),
        monotonic_reserve_index_(QUEUE_CURSOR_INIT_VALUE),
        head_threshold_(0),
        monotonic_head_index_(QUEUE_CURSOR_INIT_VALUE)
{
    // Note: ctor of MPSCQueue will NEVER been executed.(memalign -> MPSCQueue::Init)
    avg_buf_ = nullptr;
    avg_lat_buf_ = nullptr;
    avg_lat_buf_index_ = 0;
    avg_lat_buf_index_save_ = 0;
}

MPSCQueue::~MPSCQueue()
{}

int32_t MPSCQueue::Init(struct QueueMemoryHeader* header)
{
    mem_header_ = header;
    entries_ = header->entries();
    entry_size_ = mem_header_->entry_size;
    entry_bits_ = GetBits(entry_size_);
    queue_size_ = mem_header_->queue_size;
    queue_mask_ = mem_header_->queue_mask;
    release_alert_ = false;
    monotonic_reserve_index_ = mem_header_->reserve;
    monotonic_head_index_ = mem_header_->head;

    reserve_threshold_ = mem_header_->queue_size;
    head_threshold_ = 0;
    avg_buf_ = nullptr;
    avg_lat_buf_ = nullptr;
    avg_lat_buf_index_ = 0;
    avg_lat_buf_index_save_ = 0;
    return kSuccess;
}

int32_t MPSCQueue::ConsumerEndConsistent(bool fix_elem_leak)
{
    if (mem_header_ == NULL)
        return kFailure;

    #ifdef __ADK_MQ_TEST__
    std::cout << "message queue " << mem_header_->queue_name << " roll back consumer" << std::endl;
    #endif

    if (fix_elem_leak)
    {
        mem_header_->head = mem_header_->release;    
    }
    else
    {
        mem_header_->release = mem_header_->head;
    }

    return kSuccess;
}

int32_t MPSCQueue::ProducerEndConsistent()
{
    if (mem_header_ == NULL)
        return kFailure;

    #ifdef __ADK_MQ_TEST__
    std::cout << "message queue " << mem_header_->queue_name << " roll back producer" << std::endl;
    #endif

    mem_header_->reserve = mem_header_->tail;
    return kSuccess;
}

int32_t MPSCQueue::Consistent(bool fix_elem_leak)
{
    int32_t ret = ConsumerEndConsistent(fix_elem_leak);
    if (ret != kSuccess)
        return ret;

    return ProducerEndConsistent();    
}

struct Entry* MPSCQueue::InitEntries(struct Entry* entry, uint32_t entry_num, uint32_t payload_size)
{
    uint32_t entry_size = Entry::CalcEntrySize(payload_size);
    struct Entry* current_entry = entry;
    for (uint32_t i = 0; i < entry_num; ++i)
    {
        current_entry->pos = 0;
        current_entry = ptr_add(current_entry, entry_size);
    }

    return current_entry;
}

MPSCQueue* MPSCQueue::Create(const string& name, uint32_t entry_payload_size, uint32_t queue_size, QueueFlags flags)
{
    auto* queue = Create(name, entry_payload_size, queue_size);
    if (queue == nullptr)
    {
        return nullptr;
    }
    
    if (flags & ADK_LFMQ_FLAG_CREATE_AVG_BUF)
    {
        queue->avg_buf_ = new uint32_t[ADK_LFMQ_AVG_BUF_SIZE];    
    }

    if (flags & ADK_LFMQ_FLAG_CREATE_AVG_LAT_BUF)
    {
        queue->avg_lat_buf_ = new uint32_t[ADK_LFMQ_AVG_LAT_BUF_SIZE];
        memset(queue->avg_lat_buf_, 0x00, sizeof(uint32_t) * ADK_LFMQ_AVG_LAT_BUF_SIZE);
    }
    
    return queue;
}

MPSCQueue* MPSCQueue::Create(const string& name, uint32_t entry_payload_size, uint32_t queue_size)
{
    queue_size = ADK_ROUND_TO_POWER_OF_2(queue_size);
    uint32_t entry_size = Entry::CalcEntrySize(entry_payload_size);
    uint32_t queue_header_size = std::max(ADK_PAGE_SIZE, sizeof(QueueMemoryHeader) + sizeof(MPSCQueue));
    uint64_t total_size = queue_header_size + queue_size * entry_size;
    MPSCQueue* mpsc_queue = (MPSCQueue*)memalign(ADK_CACHE_LINE_SIZE, total_size);
    if (mpsc_queue == NULL)
        return NULL;

    char* env_str = std::getenv("ADK_MLOCK");
    if (env_str != NULL
        && (*env_str == 'Y' || *env_str == 'y'))
    {
        mlock(mpsc_queue, total_size);
    }
    
    QueueMemoryHeader& mq_header = *(QueueMemoryHeader*)(mpsc_queue + 1);

    NameCopy(mq_header.queue_name, name);
    mq_header.entry_size = entry_size;
    mq_header.queue_mask = queue_size - 1;
    mq_header.queue_size = queue_size;
    mq_header.queue_entry_offset = ptr_diff(ptr_add(mpsc_queue, queue_header_size), &mq_header);
    mq_header.reserve = QUEUE_CURSOR_INIT_VALUE;
    mq_header.tail = QUEUE_CURSOR_INIT_VALUE;
    mq_header.head = QUEUE_CURSOR_INIT_VALUE;
    mq_header.release = QUEUE_CURSOR_INIT_VALUE;
    mq_header.nr_forward_fail = 0;
    mq_header.saved_nr_forward_fail = 0;
    mq_header.max_queue_length = 0;
    mq_header.avg_buf_index = 0;
    mq_header.avg_buf_index_save = 0;

    struct Entry* entries = (struct Entry*)((char*)&mq_header + mq_header.queue_entry_offset);
    MPSCQueue::InitEntries(entries, queue_size, entry_payload_size);

    mpsc_queue->set_mq_index(-1);
    mpsc_queue->Init(&mq_header);

    return mpsc_queue;
}

#ifdef __ADK_DEBUG__
void MPSCQueue::ReinitQueueStatus(uint64_t cursor)
{
    mem_header_->reserve = cursor;
    mem_header_->tail = cursor;
    mem_header_->head = cursor;
    mem_header_->release = cursor;
    reserve_threshold_ = cursor;
    head_threshold_ = cursor;
    monotonic_reserve_index_ = cursor;
    monotonic_head_index_ = cursor;
}
#endif

void MPSCQueue::Create(const string& name, uint32_t entry_payload_size, uint32_t queue_size, MPSCQueue* mpsc_queue)
{
    queue_size = ADK_ROUND_TO_POWER_OF_2(queue_size);
    uint32_t entry_size = Entry::CalcEntrySize(entry_payload_size);
    uint32_t queue_header_size = std::max(ADK_PAGE_SIZE, sizeof(QueueMemoryHeader));
    uint64_t total_size = queue_header_size + queue_size * entry_size;
    QueueMemoryHeader& mq_header = *(QueueMemoryHeader*)memalign(ADK_CACHE_LINE_SIZE, total_size);
	
	char* env_str = std::getenv("ADK_MLOCK");
    if (env_str != NULL
        && (*env_str == 'Y' || *env_str == 'y'))
    {
        mlock(&mq_header, total_size);
    }

    NameCopy(mq_header.queue_name, name);
    mq_header.entry_size = entry_size;
    mq_header.queue_mask = queue_size - 1;
    mq_header.queue_size = queue_size;
    mq_header.queue_entry_offset = queue_header_size;
    mq_header.reserve = QUEUE_CURSOR_INIT_VALUE;
    mq_header.tail = QUEUE_CURSOR_INIT_VALUE;
    mq_header.head = QUEUE_CURSOR_INIT_VALUE;
    mq_header.release = QUEUE_CURSOR_INIT_VALUE;
    mq_header.nr_forward_fail = 0;
    mq_header.saved_nr_forward_fail = 0;
    mq_header.max_queue_length = 0;
    mq_header.avg_buf_index = 0;
    mq_header.avg_buf_index_save = 0;

    struct Entry* entries = (struct Entry*)((char*)&mq_header + mq_header.queue_entry_offset);
    MPSCQueue::InitEntries(entries, queue_size, entry_payload_size);

    mpsc_queue->set_mq_index(-1);
    mpsc_queue->Init(&mq_header);    
}

MPSCQueue* MPSCQueue::Duplicate(MPSCQueue& queue)
{
    QueueMemoryHeader* mq_header = queue.mem_header_;

    MPSCQueue* mpsc_queue = (MPSCQueue*)memalign(ADK_CACHE_LINE_SIZE, sizeof(MPSCQueue));
    mpsc_queue->set_mq_index(-1);
    mpsc_queue->Init(mq_header);

    mpsc_queue->avg_buf_ = queue.avg_buf_;
    mpsc_queue->avg_lat_buf_ = queue.avg_lat_buf_;

    return mpsc_queue;
}

void MPSCQueue::Duplicate(MPSCQueue& queue, MPSCQueue* dup_queue)
{
    QueueMemoryHeader* mq_header = queue.mem_header_;
    dup_queue->set_mq_index(-1);
    dup_queue->Init(mq_header);

    dup_queue->avg_buf_ = queue.avg_buf_;
    dup_queue->avg_lat_buf_ = queue.avg_lat_buf_;
}

/////////////////////////////////////////////---SPMCQueue---/////////////////////////////////////////////////////
SPMCQueue::~SPMCQueue()
{}

SPMCQueue* SPMCQueue::Create(const string& name, uint32_t entry_payload_size, uint32_t queue_size)
{
    MPSCQueue* mpsc_queue = MPSCQueue::Create(name, entry_payload_size, queue_size);
    return reinterpret_cast<SPMCQueue*>(mpsc_queue);
}

SPMCQueue* SPMCQueue::Duplicate(SPMCQueue& spmc_queue)
{
    MPSCQueue* mpsc_queue = reinterpret_cast<MPSCQueue*>(&spmc_queue);
    MPSCQueue* dup_mpsc_queue = MPSCQueue::Duplicate(*mpsc_queue);
    return reinterpret_cast<SPMCQueue*>(dup_mpsc_queue);
}

int32_t SPMCQueue::Init(struct QueueMemoryHeader* header)
{
    MPSCQueue* mpsc_queue = reinterpret_cast<MPSCQueue*>(this);
    return mpsc_queue->Init(header);
}

int32_t SPMCQueue::ConsumerEndConsistent(bool fix_elem_leak)
{
    return ((MPSCQueue*)(this))->ConsumerEndConsistent(fix_elem_leak);
}

int32_t SPMCQueue::ProducerEndConsistent()
{
    return ((MPSCQueue*)(this))->ProducerEndConsistent();
}

int32_t SPMCQueue::Consistent(bool fix_elem_leak)
{
    return ((MPSCQueue*)(this))->Consistent(fix_elem_leak);
}

int32_t SCSequentialQueue::Seek(uint64_t enqueue_seq)
{
    if (mem_header_ == NULL)
        return ErrorCode::kFailure;

    mem_header_->reserve = enqueue_seq;
    mem_header_->tail = enqueue_seq;
    mem_header_->head = enqueue_seq;
    mem_header_->release = enqueue_seq;
    return ErrorCode::kSuccess;
}

/////////////////////////////////////////////---SPSCQueue---/////////////////////////////////////////////////////

int32_t DoInit3(void* queue)
{
    MPSCQueue* queue_ptr = (MPSCQueue*)queue;
    if (queue_ptr->queue_mask_ != (8192 - 1) || queue_ptr->queue_size_ != 8192)
        return ErrorCode::kFailure;
    if (queue_ptr->entry_bits_ != 4 || queue_ptr->entry_size_ != 16)
        return ErrorCode::kFailure;

    memset(queue_ptr->entries_, 0x00, 1024 * 8 * 16);
    return ErrorCode::kSuccess;
}

MQManager::MQManager()
    :   mq_table_(NULL),
        queue_headers_(NULL),
        queue_header_size_(0),
        entry_payload_size_(0),
        entry_size_(0)
{
    last_iterate_ = 0;
}

/*
 *  |QueueMemoryHeader|...|QueueMemoryHeader|<---page align--->|ring_buffer|...|ring_buffer|
 */
MQManager* MQManager::Create(const string& name, uint32_t entry_payload_size, uint32_t queue_size, uint32_t extra_size)
{
    if (name.size() > ADK_NAME_LEN_LIMIT)
        return NULL;

    MQManager* mq = new MQManager();

    uint32_t total_entry_num = queue_size * kMaxSharedMsgQueues + extra_size;

    mq->entry_payload_size_ = entry_payload_size;
    mq->queue_header_size_ = std::max(std::max(ADK_PAGE_SIZE, sizeof(QueueMemoryHeader)), sizeof(MQTable));
    uint64_t total_header_size = mq->queue_header_size_ * (kMaxSharedMsgQueues + 1);        // 1 for MQTable

    mq->entry_size_ = Entry::CalcEntrySize(entry_payload_size);
    uint64_t total_queue_size = (uint64_t)(mq->entry_size_) * queue_size * kMaxSharedMsgQueues 
                                + (uint64_t)(mq->entry_size_) * extra_size;

    NameCopy(mq->mqm_name_, name);

    mq->mq_table_ = (MQTable*)ShmFactory::Create(name, total_header_size + total_queue_size);
    if (mq->mq_table_ == NULL)
    {
        delete mq;
        return NULL;
    }

    mq->mq_table_->mq_num = 0;
    mq->mq_table_->entry_payload_size = mq->entry_payload_size_;
    mq->mq_table_->queue_header_size = mq->queue_header_size_;
    mq->mq_table_->total_entry_num = total_entry_num;
    mq->mq_table_->total_entry_num_used = 0;

    mq->queue_headers_ = (QueueMemoryHeader*)ptr_add(mq->mq_table_, mq->queue_header_size_);
    return mq;
}

MQManager* MQManager::CreateExt
(const string& name, uint32_t entry_payload_size, SizeType total_size)
{
    const auto entry_size = Entry::CalcEntrySize(entry_payload_size);
    const uint32_t queue_size = total_size / entry_size / kMaxSharedMsgQueues;
    return Create(name, entry_payload_size, queue_size, 0);
}


MQManager* MQManager::Attach(const string& name)
{
    if (name.size() > ADK_MAX_NAME_LEN - 1)
        return NULL;

    MQManager* mq = new MQManager();
    NameCopy(mq->mqm_name_, name);

    mq->mq_table_ = (MQTable*)ShmFactory::Attach(name);
    if (mq->mq_table_ == NULL)
    {
        delete mq;
        return NULL;
    }

    mq->entry_payload_size_ = mq->mq_table_->entry_payload_size;
    mq->queue_header_size_ = mq->mq_table_->queue_header_size;
    mq->queue_headers_ = (QueueMemoryHeader*)ptr_add(mq->mq_table_, mq->queue_header_size_);
    mq->entry_size_ = Entry::CalcEntrySize(mq->entry_payload_size_);
    return mq;
}

int32_t MQManager::Destroy(const string& name)
{
    void* shm_addr = ShmFactory::Attach(name);
    if (shm_addr == NULL)
    {
        return ErrorCode::kSuccess;
    }

    if (ShmFactory::Destroy(name) != 0)
    {
        return ErrorCode::kFailure;   
    }
    return ErrorCode::kSuccess;
}

int32_t MQManager::Detach(const string& name)
{
    return ShmFactory::Detach(name);
}

#ifdef __ADK_MQM_FAILURE_TEST__
MPSCQueue* MQManager::CreateSharedMPSCQueue(const string& name, uint32_t block_num, uint32_t fp)
#else
MPSCQueue* MQManager::CreateSharedMPSCQueue(const string& name, uint32_t block_num)
#endif
{
    auto it = name_to_index_map_.find(name);
    if (it != name_to_index_map_.end())
    {
        return AttachSharedMPSCQueue(it->second);
    }

    block_num = ADK_ROUND_TO_POWER_OF_2(block_num);

    if (mq_table_->mq_num >= kMaxSharedMsgQueues
        || mq_table_->total_entry_num_used + block_num > mq_table_->total_entry_num)
        return NULL;

    QueueMemoryHeader& mq_header = *(ptr_add(queue_headers_, queue_header_size_ * mq_table_->mq_num));

    struct Entry* entries = (struct Entry*)ptr_add(
                                  ptr_add(queue_headers_, queue_header_size_ * kMaxSharedMsgQueues),
                                  mq_table_->total_entry_num_used * entry_size_);

    NameCopy(mq_header.queue_name, name);
    mq_header.entry_size = Entry::CalcEntrySize(entry_payload_size_);
    mq_header.queue_mask = block_num - 1;
    mq_header.queue_size = block_num;
    mq_header.queue_entry_offset = ptr_diff(entries, &mq_header);
    mq_header.reserve = QUEUE_CURSOR_INIT_VALUE;
    mq_header.tail = QUEUE_CURSOR_INIT_VALUE;
    mq_header.head = QUEUE_CURSOR_INIT_VALUE;
    mq_header.release = QUEUE_CURSOR_INIT_VALUE;
    mq_header.nr_forward_fail = 0;
    mq_header.saved_nr_forward_fail = 0;
    mq_header.max_queue_length = 0;
    mq_header.avg_buf_index = 0;
    mq_header.avg_buf_index_save = 0;

    #ifdef __ADK_MQM_FAILURE_TEST__
    if (fp == 1)
        return nullptr;
    #endif

    MPSCQueue::InitEntries(entries, block_num, entry_payload_size_);
    mq_table_->total_entry_num_used += block_num;
    MPSCQueue* mpsc_queue = (MPSCQueue*)memalign(ADK_CACHE_LINE_SIZE, sizeof(MPSCQueue));
    mpsc_queue->set_mq_index(mq_table_->mq_num);
    mpsc_queue->Init(&mq_header);

    ADK_BARRIER();
    ++(mq_table_->mq_num);

    #ifdef __ADK_MQM_FAILURE_TEST__
    if (fp == 2)
        return nullptr;
    #endif

    return mpsc_queue;
}

MPSCQueue* MQManager::AttachSharedMPSCQueue(uint32_t queue_index)
{
    if (queue_index >= mq_table_->mq_num)
        return NULL;

    QueueMemoryHeader* mq_header = ptr_add(queue_headers_, queue_header_size_ * queue_index);

    MPSCQueue* mpsc_queue = (MPSCQueue*)memalign(ADK_CACHE_LINE_SIZE, sizeof(MPSCQueue));
    mpsc_queue->set_mq_index(queue_index);
    mpsc_queue->Init(mq_header);

    return mpsc_queue;
}

void MQManager::IterateMQTable()
{
    uint32_t iterate_end = mq_table_->mq_num;
    for (uint32_t i = last_iterate_; i < iterate_end; ++i)
    {
        QueueMemoryHeader* mq_header = ptr_add(queue_headers_, queue_header_size_ * i);
        name_to_index_map_.insert(std::pair<std::string, uint32_t>(std::string(&(mq_header->queue_name[0])), i));
    }
    last_iterate_ = iterate_end;
}

MPSCQueue* MQManager::AttachSharedMPSCQueue(const std::string& name)
{
    auto it = name_to_index_map_.find(name);
    if (it == name_to_index_map_.end())
    {
        IterateMQTable();
        it = name_to_index_map_.find(name);
        if (it == name_to_index_map_.end())
            return NULL;
    }
    return AttachSharedMPSCQueue(it->second);
}

} // adk
