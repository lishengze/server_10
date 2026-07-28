#include <adk/shm.h>
#include <adk/mem_pool.h>

namespace adk_impl
{

#ifdef __ADK_MEM_POOL_TEST__
#define ADK_TEST_COUT std::cout << "adk_impl::mem_pool "
#endif

int32_t MemoryPool::Consistent()
{
    int32_t ret = buffer_queue_.Consistent(false);
    if (ret != kSuccess)
        return ret;

    return emergent_buffer_queue_.Consistent(false);
}


void MemoryPool::CollectIndicator(boost::property_tree::ptree& indicator_ptree)
{
    indicator_ptree.put("pool_length", buffer_queue_.length());
    indicator_ptree.put("cur_tail", buffer_queue_.mem_header_->tail - block_num());

    indicator_ptree.put("emg_pool_length", emergent_buffer_queue_.length());
    indicator_ptree.put("emg_cur_tail",
                        emergent_buffer_queue_.mem_header_->tail - emergent_block_num());
}

int32_t MemoryPool::Init(MemoryPoolHeader* pool_header, uint16_t pool_index, bool do_init)
{
    is_release_alert_ = false;
    is_release_alert_always_ = false;
    pool_header_ = pool_header;
    pool_index_ = pool_index;
    buffer_queue_.Init(&(pool_header->queue_header));
    emergent_buffer_queue_.Init(&(pool_header->emergent_queue_header));

    if (!do_init)
        return kSuccess;

    char* block = pool_header->blocks();
    for (uint32_t i = 0; i < pool_header->mp_block_num; ++i, block += pool_header->mp_block_size)
    {
        MemoryBuffer* mem_buf = (MemoryBuffer*)block;
        mem_buf->shm_ptr.make_ptr((uint16_t)pool_index, ptr_diff(block, pool_header));
        mem_buf->set_mem_buf_size(pool_header->mp_block_size);
        mem_buf->reset();

        struct Entry* entry;
        if (buffer_queue_.AllocEntry(&entry) != ErrorCode::kSuccess)
        {
            return ErrorCode::kFailure;
        }

        char* buf = entry->buffer;
        *((int64_t*)buf) = ptr_diff(block, pool_header);
        buffer_queue_.PostEntry(entry);
    }

    for (uint32_t i = 0; i < pool_header->mp_emergent_block_num; ++i, block += pool_header->mp_block_size)
    {
        MemoryBuffer* mem_buf = (MemoryBuffer*)block;
        mem_buf->shm_ptr.make_ptr((uint16_t)pool_index, ptr_diff(block, pool_header));
        mem_buf->set_mem_buf_size(pool_header->mp_block_size);
        mem_buf->reset();

        struct Entry* entry;
        if (emergent_buffer_queue_.AllocEntry(&entry) != ErrorCode::kSuccess)
        {
            return ErrorCode::kFailure;
        }

        char* buf = entry->buffer;
        *((int64_t*)buf) = ptr_diff(block, pool_header);
        emergent_buffer_queue_.PostEntry(entry);
    }

    return kSuccess;
}

#ifdef __ADK_MP_FAILURE_TEST__
MemoryBuffer* MemoryPool::NewEmergentBuffer(uint32_t fp)
#else
MemoryBuffer* MemoryPool::NewEmergentBuffer()
#endif
{
    assert(pool_header_->mp_emergent_block_num > 0);

    struct Entry* entry;
    while (emergent_buffer_queue_.WaitEntry(&entry) != kSuccess)
    {
        if (buffer_queue_.WaitEntry(&entry) == kSuccess)
        {
            char* buf = entry->buffer;
            MemoryBuffer* ret_buf = ptr_add((MemoryBuffer*)(pool_header_),
                                            *((uint64_t*)buf));
            buffer_queue_.FreeEntry(entry);
            return ret_buf;
        }

        if (is_release_alert_)
        {
            is_release_alert_ = false;
            return NULL;
        }

        if (is_release_alert_always_)
        {
            return NULL;   
        }
        ADK_PAUSE();                                                 // FIXME: deadlock!
    }

    #ifdef __ADK_MP_FAILURE_TEST__
    if (fp == 1)
        return NULL;
    #endif

    char* buf = entry->buffer;
    MemoryBuffer* ret_buf = ptr_add((MemoryBuffer*)(pool_header_),
                                    *((uint64_t*)buf));  // (1) the buffer should have been reset
    ret_buf->mem_buf_flag |= ADK_MEMORY_BUFFER_EMERGENT;

    emergent_buffer_queue_.FreeEntry(entry);

    return ret_buf;
}

std::map<std::string, int32_t> MPManager::s_mpm_ref_map;
boost::mutex MPManager::s_mpm_lock;

boost::recursive_mutex& MPManager::mpm_create_attach_lock()
{
    static boost::recursive_mutex* s_mpm_create_attach_lock = new boost::recursive_mutex();
    return *s_mpm_create_attach_lock;
}

MPManager::MPManager()
    :   mp_table_(NULL)
{
    memset(&index_to_mempool_[0], 0x00, sizeof(MemoryPool*) * kMaxSharedMempool);
    last_iterate_ = 0;
    ref_counter_ = 0;
}


/*
 *  |MemoryPoolHeader|mq_mem|pool_mem|
 */
MemoryPool* MPManager::CreateSharedPool(const string& name, uint32_t block_size, uint32_t block_num, uint32_t emergent_block_num)
{
    boost::recursive_mutex::scoped_lock lock_guard(mpm_create_attach_lock());

    block_size = ADK_ROUND_TO_POWER_OF_2(block_size);
    block_num = ADK_ROUND_TO_POWER_OF_2(block_num);
    emergent_block_num = ADK_ROUND_TO_POWER_OF_2(emergent_block_num);

    uint32_t max_size_blocks = (((1ul << 48) - 1) - ADK_PAGE_SIZE * 3 - sizeof(MemoryPoolHeader))
                               / (Entry::CalcEntrySize(sizeof(MemoryBuffer*)) + block_size);
    if (max_size_blocks < block_num)
    {
        return nullptr;
    }

    // ------>|memorypoolheader|xxx------>|mq_mem|xxx------>|pool_mem|
    MemoryPoolHeader* mem_pool_header = (MemoryPoolHeader*)ShmFactory::Create(name, 
        sizeof(MemoryPoolHeader) + ((uint64_t)Entry::CalcEntrySize(sizeof(MemoryBuffer*))) * (block_num + emergent_block_num)
        + ((uint64_t)block_size) * (block_num + emergent_block_num) + ADK_PAGE_SIZE * 3);

    if (mem_pool_header == NULL)
        return NULL;

    mem_pool_header->mp_block_size = block_size;
    mem_pool_header->mp_block_num = block_num;
    mem_pool_header->mp_emergent_block_num = emergent_block_num;
    mem_pool_header->mp_block_offset = 0;

    QueueMemoryHeader& mq_header = mem_pool_header->queue_header;
    NameCopy(mq_header.queue_name, name);
    
    mq_header.entry_size = Entry::CalcEntrySize(sizeof(MemoryBuffer*));
    mq_header.queue_mask = block_num - 1;
    mq_header.queue_size = block_num;
    mq_header.queue_entry_offset = ptr_diff(ADK_PAGE_ALIGN(ptr_add(mem_pool_header, sizeof(MemoryPoolHeader))),
                                            &mq_header);
    mq_header.reserve = 0;
    mq_header.tail = 0;
    mq_header.head = 0;
    mq_header.release = 0;
    // FIXME: we don't care about the queue index
    QueueMemoryHeader& emergent_mq_header = mem_pool_header->emergent_queue_header;
    if (emergent_block_num > 0)
    {
        NameCopy(emergent_mq_header.queue_name, name);
        
        emergent_mq_header.entry_size = Entry::CalcEntrySize(sizeof(MemoryBuffer*));
        emergent_mq_header.queue_mask = emergent_block_num - 1;
        emergent_mq_header.queue_size = emergent_block_num;
        emergent_mq_header.queue_entry_offset = ptr_diff(ADK_PAGE_ALIGN(ptr_add(mem_pool_header, sizeof(MemoryPoolHeader))),
                                                         &emergent_mq_header) + emergent_mq_header.entry_size * block_num;
        emergent_mq_header.reserve = 0;
        emergent_mq_header.tail = 0;
        emergent_mq_header.head = 0;
        emergent_mq_header.release = 0;
    }

    struct Entry* entry_end = MPSCQueue::InitEntries(mq_header.entries(), block_num, sizeof(MemoryBuffer*));
    if (emergent_block_num > 0)
    {
        entry_end = MPSCQueue::InitEntries(emergent_mq_header.entries(), emergent_block_num, sizeof(MemoryBuffer*));
    }

    mem_pool_header->mp_block_offset = ptr_diff(ADK_PAGE_ALIGN(entry_end), mem_pool_header);

    MemoryPool* mem_pool = (MemoryPool*)memalign(ADK_CACHE_LINE_SIZE, sizeof(MemoryPool));
    if (mem_pool == NULL)
    {
        ShmFactory::Destroy(name);
        return NULL;
    }

    if (mem_pool->Init(mem_pool_header, mp_table_->nr_mps) != ErrorCode::kSuccess)       // indirectly call buffer_queue_.Init();
    {
        ShmFactory::Destroy(name);
        free(mem_pool);
        return NULL;
    }

    index_to_mempool_[mp_table_->nr_mps] = mem_pool;

    NameCopy(mp_table_->mp_name[mp_table_->nr_mps], name);

    ADK_BARRIER();
    ++(mp_table_->nr_mps);
    return mem_pool;
}

MPManagerExceptionHandler g_mpm_except_handler = NULL;
void* g_mpm_except_handler_data = NULL;
void MPManager::set_except_handler(MPManagerExceptionHandler handler, void* data)
{
    g_mpm_except_handler = handler;
    g_mpm_except_handler_data = data;
}

MemoryPool* MPManager::AttachSharedPool(uint16_t index)
{
    boost::recursive_mutex::scoped_lock lock_guard(mpm_create_attach_lock());
    if (index_to_mempool_[index] != NULL)
        return index_to_mempool_[index];

    string name;
    NameCopy(mp_table_->mp_name[index], &name);

    MemoryPoolHeader* mem_pool_header = (MemoryPoolHeader*)ShmFactory::Attach(name);
    if (mem_pool_header == NULL)
        return NULL;

    MemoryPool* mem_pool = (MemoryPool*)memalign(ADK_CACHE_LINE_SIZE, sizeof(MemoryPool));
    mem_pool->Init(mem_pool_header, index, false);
    index_to_mempool_[index] = mem_pool;
    return mem_pool;
}

void MPManager::IterateMPTable()
{
    uint32_t iterate_end = mp_table_->nr_mps;
    for (uint32_t i = last_iterate_; i < iterate_end; ++i)
    {
        name_to_index_map_.insert(std::pair<std::string, uint32_t>(std::string(&(mp_table_->mp_name[i][0])), i));
    }
    last_iterate_ = iterate_end;
}

MemoryPool* MPManager::AttachSharedPool(const std::string& name)
{
    auto it = name_to_index_map_.find(name);
    if (it == name_to_index_map_.end())
    {
        IterateMPTable();
        it = name_to_index_map_.find(name); 
        if (it == name_to_index_map_.end())
            return NULL;
    }
    return AttachSharedPool(it->second);
}

int32_t MPManager::DestroySharedPool(const string& name)
{
    return ShmFactory::Destroy(name);
}

void MPManager::IncreaseReference(const std::string& table_name)
{
    auto it = s_mpm_ref_map.find(table_name);
    if (it == s_mpm_ref_map.end())
    {
        #ifdef __ADK_MEM_POOL_TEST__
        ADK_TEST_COUT << __FUNCTION__ << ", table_name = " << table_name << ", init mpm_ref = 1" << std::endl;
        #endif
        s_mpm_ref_map[table_name] = 1;
    }
    else
    {
        ++(it->second);
        #ifdef __ADK_MEM_POOL_TEST__
        ADK_TEST_COUT << __FUNCTION__ << ", table_name = " << table_name << ", mpm_ref = " << it->second << std::endl;
        #endif
    }
}

int32_t MPManager::DecreaseReference(const std::string& table_name)
{
    auto& mpm_ref = s_mpm_ref_map[mp_table_name_];
    --mpm_ref;

    #ifdef __ADK_MEM_POOL_TEST__
    ADK_TEST_COUT << __FUNCTION__ << ", mp_table_name_ = " << mp_table_name_ << ", mpm_ref = " << mpm_ref << std::endl;
    #endif

    return mpm_ref;
}

/*
 *  |MemoryPoolName|.....|
 */
int32_t MPManager::CreateMPTable(const string& table_name)
{
    boost::mutex::scoped_lock lock_guard(s_mpm_lock);
    #ifdef __ADK_MEM_POOL_TEST__
    ADK_TEST_COUT << __FUNCTION__ << ", do create table, table_name = " << table_name << std::endl;
    #endif

    mp_table_ = (struct MPTable*)ShmFactory::Create(table_name, sizeof(struct MPTable));
    if (mp_table_ == NULL)
    {
        return kFailure;
    }

    mp_table_name_ = table_name;
    mp_table_->nr_mps = 0;
    ++ref_counter_;

    IncreaseReference(table_name);
    return kSuccess;
}

int32_t MPManager::AttachMPTable(const string& table_name)
{
    boost::mutex::scoped_lock lock_guard(s_mpm_lock);
    #ifdef __ADK_MEM_POOL_TEST__
    ADK_TEST_COUT << __FUNCTION__ << ", do attach table_name = " << table_name << std::endl;
    #endif

    mp_table_ = (struct MPTable*)ShmFactory::Attach(table_name);
    if (mp_table_ == NULL)
    {
        return kFailure;
    }

    mp_table_name_ = table_name;
    ++ref_counter_;

    IncreaseReference(table_name);
    return kSuccess;   
}

void MPManager::Clear()
{
    mp_table_ = NULL;
    last_iterate_ = 0;
    memset(&index_to_mempool_[0], 0x00, sizeof(MemoryPool*) * kMaxSharedMempool);
    mp_table_name_.clear();
    name_to_index_map_.clear();
}

int32_t MPManager::DetachAll()
{
    boost::mutex::scoped_lock lock_guard(s_mpm_lock);
    if (mp_table_name_.empty())
    {
        #ifdef __ADK_MEM_POOL_TEST__
        ADK_TEST_COUT << __FUNCTION__ << ", mp_table_name_ is empty " << std::endl;
        #endif

        return kFailure;
    }

    if (mp_table_ == NULL)
    {
        #ifdef __ADK_MEM_POOL_TEST__
        ADK_TEST_COUT << __FUNCTION__ << ", mp_table_ is empty " << std::endl;
        #endif

        return kFailure;
    }

    if ((--ref_counter_) != 0)
    {
        return kSuccess;
    }

    if (DecreaseReference(mp_table_name_) != 0)
    {
        #ifdef __ADK_MEM_POOL_TEST__
        ADK_TEST_COUT << __FUNCTION__ << ", mp_table_name_ " << mp_table_name_ << " has reference, Clear()" << std::endl;
        #endif

        Clear();
        return kSuccess;
    }

    #ifdef __ADK_MEM_POOL_TEST__
    ADK_TEST_COUT << __FUNCTION__ << ", do detach mp_table_name_ = " << mp_table_name_ << std::endl;
    #endif

    for (uint32_t i = 0; i < mp_table_->nr_mps; ++i)
    {
        ShmFactory::Detach(&(mp_table_->mp_name[i][0]));
    }

    ShmFactory::Detach(mp_table_name_);
    Clear();
    return kSuccess;
}

int32_t MPManager::DestroyAll()
{
    boost::mutex::scoped_lock lock_guard(s_mpm_lock);

    if (mp_table_name_.empty())
    {
        #ifdef __ADK_MEM_POOL_TEST__
        ADK_TEST_COUT << __FUNCTION__ << ", mp_table_name_ is empty " << std::endl;
        #endif

        return kFailure;
    }

    if (mp_table_ == NULL)
    {
        #ifdef __ADK_MEM_POOL_TEST__
        ADK_TEST_COUT << __FUNCTION__ << ", mp_table_ is empty " << std::endl;
        #endif

        return kFailure;
    }

    if ((--ref_counter_) != 0)
    {
        return kSuccess;
    }
    
    if (DecreaseReference(mp_table_name_) != 0)
    {
        #ifdef __ADK_MEM_POOL_TEST__
        ADK_TEST_COUT << __FUNCTION__ << ", mp_table_name_ " << mp_table_name_ << " has reference, Clear()" << std::endl;
        #endif

        Clear();
        return kSuccess;
    }

    #ifdef __ADK_MEM_POOL_TEST__
    ADK_TEST_COUT << __FUNCTION__ << ", do destroy mp_table_name_ = " << mp_table_name_ << std::endl;
    #endif

    for (uint32_t i = 0; i < mp_table_->nr_mps; ++i)
    {
        DestroySharedPool(&(mp_table_->mp_name[i][0]));
    }

    ShmFactory::Destroy(mp_table_name_);
    Clear();
    return kSuccess;
}

} // adk




