#include <malloc.h>

#include <boost/property_tree/json_parser.hpp>

#include <adk/shm.h>
#include <adk/util.h>
#include <adk/lock_free_cont_memory.h>

namespace adk_impl
{

ContinueMemory* ContinueMemory::Create(uint32_t memory_size, uint32_t reserve_size, uint32_t app_ctx_size)
{
    const auto memory_header_size = ADK_ROUND_UP(kContMemoryHeaderAlignedSize + app_ctx_size, ADK_PAGE_SIZE);
    const auto real_memory_size = ADK_ROUND_TO_POWER_OF_2(std::max<uint32_t>(sizeof(uint64_t), memory_size));
    const auto total_memory_size = ADK_ROUND_UP(real_memory_size + reserve_size, ADK_PAGE_SIZE);

    const auto total_size = memory_header_size + total_memory_size;

    ContMemoryHeader* memory_header = (ContMemoryHeader*)memalign(ADK_PAGE_SIZE, (size_t)total_size);
    if (ADK_UNLIKELY(nullptr == memory_header))
    {
        return nullptr;
    }

    char* env_str = std::getenv("ADK_MLOCK");
    if (env_str != NULL
        && (*env_str == 'Y' || *env_str == 'y'))
    {
        mlock(memory_header, total_size);
    }

    memory_header->version = 0;
    memory_header->memory_index = 0;
    memory_header->memory_entry_offset = memory_header_size;
    memory_header->app_ctx_size = app_ctx_size;
    memory_header->memory_size = real_memory_size;
    memory_header->reserve_size = total_memory_size - real_memory_size;

    memory_header->head = 0;
    memory_header->consume_nr = 0;
    memory_header->consume_fail_nr = 0;
    memory_header->tail = 0;
    memory_header->reserved_tail   = 0;
    memory_header->produce_nr = 0;
    memory_header->produce_fail_nr = 0;
    memory_header->head_threshold = memory_header->tail;
    memory_header->tail_threshold = memory_header->head + real_memory_size;

    memset(memory_header->app_ctx(), 0, app_ctx_size);
    memset(memory_header->entries(), 0, total_memory_size);

    ContinueMemory* continue_memory = new ContinueMemory;
    continue_memory->Init(memory_header);

    return continue_memory;
}

void ContinueMemory::Touch()
{
    uint64_t* buffer_entry = (uint64_t*)memory_entry_;
    const uint32_t loop_counter = (memory_size_ + reserve_size_) / sizeof(uint64_t);
    for (uint32_t index = 0; index < loop_counter; ++index)
    {
    #if defined(__x86_64__)
        asm volatile ("" : : "r" (*buffer_entry));
    #elif defined(__aarch64__)
        ADK_BARRIER();
    #endif
        ++buffer_entry;
    }
}

std::string ContinueMemory::CollectIndicator() const
{
    boost::property_tree::ptree indicator_ptree;
    CollectIndicator(indicator_ptree);

    std::ostringstream oss;
    boost::property_tree::json_parser::write_json(oss, indicator_ptree);
    return oss.str();
}

void ContinueMemory::CollectIndicator(boost::property_tree::ptree& indicator_ptree) const
{
    const auto produce_cur = ACCESS_ONCE(memory_header_->tail);
    const auto consume_cur = ACCESS_ONCE(memory_header_->head);

    uint64_t current_consume = ACCESS_ONCE(memory_header_->consume_nr);
    uint64_t current_produce = ACCESS_ONCE(memory_header_->produce_nr);

    indicator_ptree.put("consume_cur", consume_cur);
    indicator_ptree.put("produce_cur", produce_cur);
    indicator_ptree.put("consume_nr", current_consume);
    indicator_ptree.put("produce_nr", current_produce);
    indicator_ptree.put("consume_fail_nr", ACCESS_ONCE(memory_header_->consume_fail_nr));
    indicator_ptree.put("produce_fail_nr", ACCESS_ONCE(memory_header_->produce_fail_nr));
    const auto max_len = ACCESS_ONCE(memory_header_->max_clen_snap);
    const auto current_len = (produce_cur > consume_cur) ? (produce_cur - consume_cur) : 0;
    indicator_ptree.put("max_len_bytes", std::max(max_len, static_cast<uint32_t>(current_len)));
    memory_header_->consume_reset = true;

    indicator_ptree.put("cur_qlen", current_produce - current_consume);
}

ShmContMemManager* ShmContMemManager::Create(const std::string& shm_name, 
                                             uint32_t cont_mem_num, 
                                             uint32_t total_size, 
                                             uint32_t app_ctx_size)
{
    ShmManagerHeader* shm_header = ShmContMemManager::CreateShm(shm_name, cont_mem_num, total_size, app_ctx_size);
    if (ADK_UNLIKELY(nullptr == shm_header))
    {
        return nullptr;
    }

    for (uint32_t cont_mem_index = 0; cont_mem_index < shm_header->cont_mem_limit; ++cont_mem_index)
    {
        const auto memory_offset = (cont_mem_index + 1) * shm_header->header_size;
        ContMemoryHeader* const memory_header = (ContMemoryHeader*)ptr_add(shm_header, memory_offset);
        memory_header->app_ctx_size = app_ctx_size;
        memset(memory_header->app_ctx(), 0, app_ctx_size);
    }

    ADK_BARRIER();
    NameCopy(shm_header->shm_name, shm_name);

    ShmContMemManager* manager = new ShmContMemManager;
    manager->Init(shm_header, shm_name);
    return manager;
}

ShmContMemManager* ShmContMemManager::Attach(const std::string& shm_name)
{
    ShmManagerHeader* const shm_header = (ShmManagerHeader*)ShmFactory::Attach(shm_name);
    if (nullptr == shm_header)
    {
        return nullptr;
    }

    char* const shm_name_in_header = shm_header->shm_name;
    const auto cmp_len = std::min<uint32_t>(ADK_MAX_NAME_LEN, shm_name.length() + 1);
    if (0 != memcmp(shm_name_in_header, shm_name.c_str(), cmp_len))
    {
        return nullptr;
    }

    ShmContMemManager* manager = new ShmContMemManager;
    manager->Init(shm_header, shm_name);
    return manager;
}

int32_t ShmContMemManager::Detach(const std::string& shm_name)
{
    return ShmFactory::Detach(shm_name);
}

int32_t ShmContMemManager::Destroy(const std::string& shm_name)
{
    void* shm_header = ShmFactory::Attach(shm_name);
    if (nullptr == shm_header)
    {
        return ErrorCode::kSuccess;
    }

    if (0 != ShmFactory::Destroy(shm_name))
    {
        return ErrorCode::kFailure;
    }
    return ErrorCode::kSuccess;
}

int32_t ShmContMemManager::Detach()
{
    return ShmContMemManager::Detach(shm_name_);
}

int32_t ShmContMemManager::Destroy()
{
    return ShmContMemManager::Destroy(shm_name_);
}

ContinueMemory* ShmContMemManager::CreateShmContMemory(const std::string& cont_mem_name, 
                                                       uint32_t memory_size, 
                                                       uint32_t reserve_size)
{
    if (ADK_UNLIKELY(shm_header_->cont_mem_index >= shm_header_->cont_mem_limit))
    {
        return nullptr;
    }

    const auto real_memory_size = ADK_ROUND_TO_POWER_OF_2(std::max<uint32_t>(sizeof(uint64_t), memory_size));
    const auto total_memory_size = ADK_ROUND_UP(real_memory_size + reserve_size, ADK_PAGE_SIZE);

    void* memory = shm_header_->AllocMemory(total_memory_size);
    if (ADK_UNLIKELY(nullptr == memory))
    {
        return nullptr;
    }

    memset(memory, 0, total_memory_size);

    const auto cont_mem_index = shm_header_->cont_mem_index;
    ContMemoryHeader* memory_header = ptr_add(memory_headers_, cont_mem_index * memory_header_size_);

    NameCopy(memory_header->memory_name, cont_mem_name);
    name_to_index_map_[cont_mem_name] = cont_mem_index;

    memory_header->version = 0;
    memory_header->memory_index = cont_mem_index;
    memory_header->memory_entry_offset = ptr_diff(memory, memory_header);
    memory_header->memory_size = real_memory_size;
    memory_header->reserve_size = total_memory_size - real_memory_size;

    memory_header->head = 0;
    memory_header->consume_nr = 0;
    memory_header->consume_fail_nr = 0;
    memory_header->consume_reset = false;
    memory_header->max_clen_snap = 0;
    memory_header->tail = 0;
    memory_header->produce_nr = 0;
    memory_header->produce_fail_nr = 0;
    memory_header->head_threshold = memory_header->tail;
    memory_header->tail_threshold = memory_header->head + real_memory_size;

    ContinueMemory* const cont_memory = ContinueMemory::Create(memory_header);

    assert(cont_memory);
    cont_memory->set_context(context_);
    shm_header_->PostMemory(total_memory_size);

    ADK_BARRIER();
    ++shm_header_->cont_mem_index;
    return cont_memory;
}

ContinueMemory* ShmContMemManager::AttachShmContMemory(uint32_t memory_index)
{
    assert(shm_header_);
    if (memory_index >= shm_header_->cont_mem_index)
    {
        return nullptr;
    }

    ContMemoryHeader* memory_header = ptr_add(memory_headers_, memory_header_size_ * memory_index);
    auto* const cont_memory = ContinueMemory::Create(memory_header);
    assert(cont_memory);

    cont_memory->set_context(context_);
    cont_memory->Touch();
    return cont_memory;
}

ContinueMemory* ShmContMemManager::AttachShmContMemory(const std::string& shm_name, uint32_t memory_index)
{
    ShmManagerHeader* const shm_header = (ShmManagerHeader*)ShmFactory::Attach(shm_name);
    if (nullptr == shm_header)
    {
        return nullptr;
    }

    ContMemoryHeader* memory_headers = (ContMemoryHeader*)ptr_add(shm_header, shm_header->header_size);
    uint32_t memory_header_size = shm_header->header_size;

    if (memory_index >= shm_header->cont_mem_index)
    {
        return nullptr;
    }

    ContMemoryHeader* memory_header = ptr_add(memory_headers, memory_header_size* memory_index);
    auto* const cont_memory = ContinueMemory::Create(memory_header);
    assert(cont_memory);

    cont_memory->Touch();
    return cont_memory;
}

ContinueMemory* ShmContMemManager::AttachShmContMemory(const std::string& cont_mem_name)
{
    auto iter = name_to_index_map_.find(cont_mem_name);
    if (name_to_index_map_.end() == iter)
    {
        const auto upper_index = shm_header_->cont_mem_index;
        for (uint32_t index = 0; index < upper_index; ++index)
        {
            ContMemoryHeader* const memory_header = ptr_add(memory_headers_,
                index * memory_header_size_);

            name_to_index_map_[std::string(memory_header->memory_name)] = index;
        }

        iter = name_to_index_map_.find(cont_mem_name);
        if (name_to_index_map_.end() == iter)
        {
            return nullptr;
        }
    }

    return AttachShmContMemory(iter->second);
}

ContinueMemory* ShmContMemManager::AttachShmContMemory(const std::string& shm_name, const std::string& cont_mem_name)
{
    ShmManagerHeader* const shm_header = (ShmManagerHeader*)ShmFactory::Attach(shm_name);
    if (nullptr == shm_header)
    {
        return nullptr;
    }

    ContMemoryHeader* memory_headers = (ContMemoryHeader*)ptr_add(shm_header, shm_header->header_size);
    uint32_t memory_header_size = shm_header->header_size;

    const auto upper_index = shm_header->cont_mem_index;
    for (uint32_t index = 0; index < upper_index; ++index)
    {
        ContMemoryHeader* const memory_header = ptr_add(memory_headers, index * memory_header_size);
        if (std::string(memory_header->memory_name) == cont_mem_name)
        {
            auto* const cont_memory = ContinueMemory::Create(memory_header);
            assert(cont_memory);

            cont_memory->Touch();
            return cont_memory;
        }
    }

    return nullptr;
}

ShmContMemManager* ShmContMemManager::DoCreate(const std::string& shm_name, 
                                    uint32_t cont_mem_num, 
                                    uint32_t total_size,
                                    uint32_t ctx_size,
                                    const std::function<void(void* buffer)>& callback)
{
    ShmManagerHeader* shm_header = ShmContMemManager::CreateShm(shm_name, cont_mem_num, total_size, ctx_size);
    if (ADK_UNLIKELY(nullptr == shm_header))
    {
        return nullptr;
    }

    for (uint32_t cont_mem_index = 0; cont_mem_index < shm_header->cont_mem_limit; ++cont_mem_index)
    {
        const auto memory_offset = (cont_mem_index + 1) * shm_header->header_size;
        ContMemoryHeader* const memory_header = (ContMemoryHeader* const)(ptr_add(shm_header, memory_offset));
        callback(memory_header->app_ctx());
    }

    ADK_BARRIER();
    const auto copy_len = std::min<uint32_t>(ADK_MAX_NAME_LEN - 1, shm_name.length());
    memcpy(shm_header->shm_name, shm_name.c_str(), copy_len);
    shm_header->shm_name[copy_len] = '\0';

    ShmContMemManager* manager = new ShmContMemManager;
    manager->Init(shm_header, shm_name);
    return manager;
}

ShmContMemManager::ShmManagerHeader* ShmContMemManager::CreateShm(const std::string& shm_name,
                                               uint32_t cont_mem_num, 
                                               uint32_t total_size, 
                                               uint32_t app_ctx_size)
{
    const auto memory_header_size = ADK_ROUND_UP(kContMemoryHeaderAlignedSize + app_ctx_size, ADK_CACHE_LINE_SIZE);

    const auto header_size = std::max<size_t>(sizeof(struct ShmManagerHeader), memory_header_size);
    const auto real_header_size = ADK_ROUND_UP(header_size, ADK_CACHE_LINE_SIZE);
    const auto total_header_size = ADK_ROUND_UP(real_header_size * (cont_mem_num + 1), ADK_PAGE_SIZE);
    const auto total_payload_size = ADK_ROUND_UP(total_size, ADK_PAGE_SIZE);

    ShmManagerHeader* shm_header = (ShmManagerHeader*)ShmFactory::Create(shm_name, total_header_size + total_payload_size);
    if (ADK_UNLIKELY(nullptr == shm_header))
    {
        ShmFactory::Destroy(shm_name);
        shm_header = (ShmManagerHeader*)ShmFactory::Create(shm_name, total_header_size + total_payload_size);
        if (ADK_UNLIKELY(nullptr == shm_header))
        {
            return nullptr;
        }
    }

    shm_header->cont_mem_index = 0;
    shm_header->cont_mem_limit = cont_mem_num;
    shm_header->header_size = real_header_size;
    shm_header->memory_offset_used = total_header_size;
    shm_header->memory_offset_limit = total_header_size + total_payload_size;

    return shm_header;
}

}
