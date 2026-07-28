#include <malloc.h>

#include <boost/property_tree/json_parser.hpp>

#include <adk/shm_ptr.h>
#include <adk/constant.h>
#include <adk/shm.h>
#include <adk/util.h>
#include <adk_pack/error_code.h>
#include <adk/lock_free_cont_memory.h>
#include <adk_pack/lock_free_cont_memory.h>

namespace adk
{


char* ContEntry::GetBuffer()
{
    return ((adk_impl::ContEntry*)(this))->GetBuffer();
}
uint64_t ContEntry::usage_len() const
{
    return ((adk_impl::ContEntry*)(this))->usage_len();
}
uint64_t ContEntry::app_data_len() const
{
    return ((adk_impl::ContEntry*)(this))->app_data_len();
}
uint64_t ContEntry::GetLength()
{
    return ((adk_impl::ContEntry*)(this))->GetLength();
}
char* ContMemoryHeader::app_ctx()
{
    return ((adk_impl::ContMemoryHeader*)(this))->app_ctx();
}
void* ContMemoryHeader::entries()
{
    return ((adk_impl::ContMemoryHeader*)(this))->entries();
}



ContinueMemory* ContinueMemory::Create(uint32_t memory_size, 
                                uint32_t reserve_size,
                                uint32_t app_ctx_size)
{
    return (ContinueMemory*)adk_impl::ContinueMemory::Create(memory_size, reserve_size, app_ctx_size);
}


ContinueMemory* ContinueMemory::Create(ContMemoryHeader* memory_header)
{
    return (ContinueMemory*)adk_impl::ContinueMemory::Create((adk_impl::ContMemoryHeader*)memory_header);
}

int32_t ContinueMemory::Push(const void* buffer, uint32_t length)
{
    return ((adk_impl::ContinueMemory*)(this))->Push(buffer, length);
}

int32_t ContinueMemory::TryPush(const void* buffer, uint32_t length)
{
    return ((adk_impl::ContinueMemory*)(this))->TryPush(buffer, length);
}

ssize_t ContinueMemory::Pop(void* buffer, uint32_t buf_len)
{
    return ((adk_impl::ContinueMemory*)(this))->Pop(buffer, buf_len);
}

ssize_t ContinueMemory::TryPop(void* buffer, uint32_t buf_len)
{
    return ((adk_impl::ContinueMemory*)(this))->TryPop(buffer, buf_len);
}

int32_t ContinueMemory::TryAllocEntry(uint32_t length, struct ContEntry** entry_pptr)
{
    return ((adk_impl::ContinueMemory*)(this))->TryAllocEntry(length, (adk_impl::ContEntry**)entry_pptr);
}

int32_t ContinueMemory::AllocEntry(uint32_t length, struct ContEntry** entry_pptr)
{
    return ((adk_impl::ContinueMemory*)(this))->AllocEntry(length, (adk_impl::ContEntry**)entry_pptr);
}

int32_t ContinueMemory::TryLockAllocEntryImpl(uint32_t length, 
                                    struct ContEntry** entry_pptr,
                                    const std::function<void()>& lock,
                                    const std::function<void()>& unlock)
{
    return (((adk_impl::ContinueMemory*)(this)))->TryLockAllocEntryImpl(length, (adk_impl::ContEntry**)entry_pptr, lock, unlock);
}

int32_t ContinueMemory::LockAllocEntryImpl(uint32_t length, struct ContEntry** entry_pptr, const std::function<void()>& lock, const std::function<void()>& unlock)
{
    return (((adk_impl::ContinueMemory*)(this)))->LockAllocEntryImpl(length, (adk_impl::ContEntry**)entry_pptr, lock, unlock);
}

void ContinueMemory::AdjustEntryLength(struct ContEntry* entry_ptr, uint32_t length)
{
    adk_impl::ContinueMemory::AdjustEntryLength((adk_impl::ContEntry*)entry_ptr, length);
}

void ContinueMemory::PostEntry(struct ContEntry* entry_ptr)
{
    ((adk_impl::ContinueMemory*)(this))->PostEntry((adk_impl::ContEntry*)entry_ptr);
}

// for multi producers to post an entry
void ContinueMemory::PostEntryThreadSafe(struct ContEntry* entry_ptr)
{
    ((adk_impl::ContinueMemory*)(this))->PostEntryThreadSafe((adk_impl::ContEntry*)entry_ptr);
}
void ContinueMemory::PostEntry(struct ContEntry* entry_ptr, uint32_t length)
{
    ((adk_impl::ContinueMemory*)(this))->PostEntry((adk_impl::ContEntry*)entry_ptr, length);
}

int32_t ContinueMemory::TryWaitEntry(struct ContEntry** entry_pptr)
{
    return ((adk_impl::ContinueMemory*)(this))->TryWaitEntry((adk_impl::ContEntry**)entry_pptr);
}

int32_t ContinueMemory::WaitEntry(struct ContEntry** entry_ptr)
{
    return ((adk_impl::ContinueMemory*)(this))->WaitEntry((adk_impl::ContEntry**)entry_ptr);
}

void ContinueMemory::FreeEntry(struct ContEntry* entry_ptr)
{
    ((adk_impl::ContinueMemory*)(this))->FreeEntry((adk_impl::ContEntry*)entry_ptr);
}

void ContinueMemory::ForeachBase(const std::function<bool(char*, uint64_t)>& callback)
{
    ((adk_impl::ContinueMemory*)(this))->ForeachImpl(callback);
}

char* ContinueMemory::ContinueMemory::app_ctx()
{
    return ((adk_impl::ContinueMemory*)(this))->app_ctx();
}

const char* ContinueMemory::ContinueMemory::const_app_ctx()
{
    return ((adk_impl::ContinueMemory*)(this))->const_app_ctx();
}

uint32_t ContinueMemory::app_ctx_size() const
{
    return ((adk_impl::ContinueMemory*)(this))->app_ctx_size();
}

void ContinueMemory::set_release_alert()
{
    ((adk_impl::ContinueMemory*)(this))->set_release_alert();
}

void ContinueMemory::set_context(void* context)
{
    ((adk_impl::ContinueMemory*)(this))->set_context(context);
}

void* ContinueMemory::context() const
{
    return ((adk_impl::ContinueMemory*)(this))->context();
}

void ContinueMemory::Touch()
{
    ((adk_impl::ContinueMemory*)(this))->Touch();
}

std::string ContinueMemory::CollectIndicator() const
{
    return ((adk_impl::ContinueMemory*)(this))->CollectIndicator();
}

void ContinueMemory::CollectIndicator(boost::property_tree::ptree& indicator_ptree) const
{
    return ((adk_impl::ContinueMemory*)(this))->CollectIndicator(indicator_ptree);
}

// struct ShmManagerHeader
// {
//     void* AllocMemory(uint32_t memory_size)
//     {
//         return ((adk_impl::ShmContMemManager::ShmManagerHeader*)(this))->AllocMemory(memory_size);
//     }

//     void PostMemory(uint32_t memory_size)
//     {
//         ((adk_impl::ShmContMemManager::ShmManagerHeader*)(this))->PostMemory(memory_size);
//     }

//     void* Allocate(uint32_t memory_size)
//     {
//         return ((adk_impl::ShmContMemManager::ShmManagerHeader*)(this))->Allocate(memory_size);
//     }
// };

ShmContMemManager* ShmContMemManager::Create(const std::string& shm_name, 
                                    uint32_t cont_mem_num, 
                                    uint32_t total_size, 
                                    uint32_t app_ctx_size)
{
    return (ShmContMemManager*)adk_impl::ShmContMemManager::Create(shm_name, cont_mem_num, total_size, app_ctx_size);
}

ShmContMemManager* ShmContMemManager::Create(const std::string& shm_name, 
                                            uint32_t cont_mem_num, 
                                            uint32_t total_size, 
                                            uint32_t ctx_size, 
                                            const std::function<void(void*buffer)>& callback)
{
    return (ShmContMemManager*)adk_impl::ShmContMemManager::DoCreate(shm_name, cont_mem_num, total_size, ctx_size, callback);
}

ShmContMemManager* ShmContMemManager::Attach(const std::string& shm_name)
{
    return (ShmContMemManager*)adk_impl::ShmContMemManager::Attach(shm_name);
}

int32_t ShmContMemManager::Detach(const std::string& shm_name)
{
    return adk_impl::ShmContMemManager::Detach(shm_name);
}

int32_t ShmContMemManager::Destroy(const std::string& shm_name)
{
    return adk_impl::ShmContMemManager::Destroy(shm_name);
}

int32_t ShmContMemManager::Detach()
{
    return ((adk_impl::ShmContMemManager*)(this))->Detach();
}

int32_t ShmContMemManager::Destroy()
{
    return ((adk_impl::ShmContMemManager*)(this))->Destroy();
}

ContinueMemory* ShmContMemManager::CreateShmContMemory(const std::string& cont_mem_name, 
                                    uint32_t memory_size, 
                                    uint32_t reserve_size)
{
    return (ContinueMemory*)(((adk_impl::ShmContMemManager*)(this))->CreateShmContMemory(cont_mem_name, memory_size, reserve_size));
}

ContinueMemory* ShmContMemManager::AttachShmContMemory(uint32_t memory_index)
{
    return (ContinueMemory*)(((adk_impl::ShmContMemManager*)(this))->AttachShmContMemory(memory_index));
}

ContinueMemory* ShmContMemManager::AttachShmContMemory(const std::string& shm_name, uint32_t memory_index)
{
    return (ContinueMemory*)adk_impl::ShmContMemManager::AttachShmContMemory(shm_name, memory_index);
}

ContinueMemory* ShmContMemManager::AttachShmContMemory(const std::string& cont_mem_name)
{
    return (ContinueMemory*)(((adk_impl::ShmContMemManager*)(this))->AttachShmContMemory(cont_mem_name));
}

ContinueMemory* ShmContMemManager::AttachShmContMemory(const std::string& shm_name, const std::string& cont_mem_name)
{
    return (ContinueMemory*)adk_impl::ShmContMemManager::AttachShmContMemory(shm_name, cont_mem_name);
}

void ShmContMemManager::set_context(void* context)
{
    ((adk_impl::ShmContMemManager*)(this))->set_context(context);
}

void* ShmContMemManager::context() const
{
    return (void*)(((adk_impl::ShmContMemManager*)(this))->context());
}

}




