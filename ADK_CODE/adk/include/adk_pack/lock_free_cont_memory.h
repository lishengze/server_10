#ifndef ADK_LOCK_FREE_CONT_MEMORY_H_
#define ADK_LOCK_FREE_CONT_MEMORY_H_

#include "error_code.h"
#include <malloc.h>
#include <boost/property_tree/json_parser.hpp>

namespace adk
{

constexpr uint32_t kContMemoryDefSize = 16 * 1024 * 1024;
constexpr uint32_t kContDefReserveSize = 1 * 1024 * 1024;

struct ContEntry
{
    char* GetBuffer();

    uint64_t usage_len() const;

    uint64_t app_data_len() const;

    uint64_t GetLength();
};

struct ContMemoryHeader
{
    char* app_ctx();

    void* entries();
};

class ContinueMemory
{
public:
    static ContinueMemory* Create(uint32_t memory_size = kContMemoryDefSize, 
                                  uint32_t reserve_size = kContDefReserveSize,
                                  uint32_t app_ctx_size = 0);

    template<typename AppCtxType>
    static ContinueMemory* Create(uint32_t memory_size = kContMemoryDefSize, 
                                  uint32_t reserve_size = kContDefReserveSize)
    {
        ContinueMemory* continue_memory = ContinueMemory::Create(memory_size, reserve_size, sizeof(AppCtxType));
        if (nullptr != continue_memory)
        {
            new ((void*)continue_memory->app_ctx()) AppCtxType();
        }

        return continue_memory;
    }

    static ContinueMemory* Create(ContMemoryHeader* memory_header);

    int32_t Push(const void* buffer, uint32_t length);

    int32_t TryPush(const void* buffer, uint32_t length);

    ssize_t Pop(void* buffer, uint32_t buf_len);

    ssize_t TryPop(void* buffer, uint32_t buf_len);

    int32_t TryAllocEntry(uint32_t length, struct ContEntry** entry_pptr);

    int32_t AllocEntry(uint32_t length, struct ContEntry** entry_pptr);

    // for multi producers to alloc an entry 
    // non block mode
    template<class SpinLockT>
    int32_t TryLockAllocEntry(uint32_t length, struct ContEntry** entry_pptr, SpinLockT& lock)
    {
        return TryLockAllocEntryImpl(length, entry_pptr, [&lock](){lock.lock();}, [&lock](){lock.unlock();});
    }

    // for multi producers to alloc an entry
    // block mode
    template<class SpinLockT>
    ErrorCode LockAllocEntry(uint32_t length, struct ContEntry** entry_pptr, SpinLockT& lock)
    {
        return LockAllocEntryImpl(length, entry_pptr, [&lock](){lock.lock();}, [&lock](){lock.unlock();});
    }

    static void AdjustEntryLength(struct ContEntry* entry_ptr, uint32_t length);

    void PostEntry(struct ContEntry* entry_ptr);

    // for multi producers to post an entry
    void PostEntryThreadSafe(struct ContEntry* entry_ptr);

    void PostEntry(struct ContEntry* entry_ptr, uint32_t length);

    int32_t TryWaitEntry(struct ContEntry** entry_pptr);

    int32_t WaitEntry(struct ContEntry** entry_ptr);

    void FreeEntry(struct ContEntry* entry_ptr);

    template<typename Callback>
    void Foreach(const Callback& callback)
    {
        ForeachBase(callback);
    }



    char* app_ctx();

    const char* const_app_ctx();

    uint32_t app_ctx_size() const;

    void set_release_alert();

    void set_context(void* context);

    void* context() const;

    void Touch();

    std::string CollectIndicator() const;

    void CollectIndicator(boost::property_tree::ptree& indicator_ptree) const;

private:
    int32_t TryLockAllocEntryImpl(uint32_t length, struct ContEntry** entry_pptr, const std::function<void()>& lock, const std::function<void()>& unlock);

    int32_t LockAllocEntryImpl(uint32_t length, struct ContEntry** entry_pptr, const std::function<void()>& lock, const std::function<void()>& unlock);


    void ForeachBase(const std::function<bool(char*, uint64_t)>& callback);

};




class ShmContMemManager
{
public:

    static ShmContMemManager* Create(const std::string& shm_name, 
                                     uint32_t cont_mem_num, 
                                     uint32_t total_size, 
                                     uint32_t app_ctx_size = 0);

    template<typename AppCtxType>
    static ShmContMemManager* Create(const std::string& shm_name, 
                                     uint32_t cont_mem_num, 
                                     uint32_t total_size)
    {
		return Create(shm_name, cont_mem_num, total_size, sizeof(AppCtxType), [](void* buffer){
			new (buffer) AppCtxType();
		});
    }

    static ShmContMemManager* Attach(const std::string& shm_name);

    static int32_t Detach(const std::string& shm_name);

    static int32_t Destroy(const std::string& shm_name);

    int32_t Detach();

    int32_t Destroy();

    ContinueMemory* CreateShmContMemory(const std::string& cont_mem_name, 
                                        uint32_t memory_size, 
                                        uint32_t reserve_size);

    ContinueMemory* AttachShmContMemory(uint32_t memory_index);

    static ContinueMemory* AttachShmContMemory(const std::string& shm_name, uint32_t memory_index);

    ContinueMemory* AttachShmContMemory(const std::string& cont_mem_name);

    static ContinueMemory* AttachShmContMemory(const std::string& shm_name, const std::string& cont_mem_name);

    void set_context(void* context);

    void* context() const;


private:

    static ShmContMemManager* Create(const std::string& shm_name, 
                                                uint32_t cont_mem_num, 
                                                uint32_t total_size, 
                                                uint32_t ctx_size, 
                                                const std::function<void(void*buffer)>& callback);

};

}


#endif
