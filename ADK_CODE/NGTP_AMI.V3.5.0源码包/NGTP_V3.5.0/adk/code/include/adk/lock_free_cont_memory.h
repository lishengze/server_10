#ifndef ADK_IMPL_LOCK_FREE_CONT_MEMORY_H_
#define ADK_IMPL_LOCK_FREE_CONT_MEMORY_H_

#include "shm_ptr.h"
#include "constant.h"
#include "error_code.h"
#include "arch/generic.h"

#include <boost/property_tree/ptree.hpp>

#include <map>
#include <string>

namespace adk_impl
{

struct ContEntry
{
    uint32_t len;
    uint32_t app_len;
    char     buffer[];

    char* GetBuffer()
    {
        return buffer;
    }

    uint64_t usage_len() const
    {
        return len;
    }

    uint64_t app_data_len() const
    {
        return app_len;
    }

    uint64_t GetLength()
    {
        return usage_len();
    }
};

constexpr uint32_t kAlignedSize = sizeof(struct ContEntry);
constexpr uint32_t kAlignedBits = 0xFFFFFFF8u;

constexpr uint32_t kContMemoryDefSize = 16 * 1024 * 1024;
constexpr uint32_t kContDefReserveSize = 1 * 1024 * 1024;

struct ContMemoryHeader
{
    char        memory_name[ADK_MAX_NAME_LEN];
    uint32_t    version;
    uint32_t    memory_index;
    uint32_t    memory_entry_offset;
    uint32_t    app_ctx_size;
    uint32_t    memory_size;
    uint32_t    reserve_size;
    uint64_t    head __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint64_t    head_threshold;
    uint64_t    consume_nr;
    uint64_t    consume_fail_nr;
    bool        consume_reset;
    uint32_t    max_clen_snap;
    uint64_t    tail __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint64_t    reserved_tail;  //for multi producers
    uint64_t    tail_threshold;
    uint64_t    produce_nr;
    uint64_t    produce_fail_nr;

    inline char* app_ctx();

    void* entries()
    {
        return (void*)(ptr_add(this, memory_entry_offset));
    }
};

constexpr uint32_t kContMemoryHeaderAlignedSize = ADK_ROUND_UP(sizeof(struct ContMemoryHeader), ADK_CACHE_LINE_SIZE);

char* ContMemoryHeader::app_ctx()
{
    return (char*)(ptr_add(this, kContMemoryHeaderAlignedSize));
}

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

    static ContinueMemory* Create(ContMemoryHeader* memory_header)
    {
        ContinueMemory* continue_memory = new ContinueMemory;
        continue_memory->Init(memory_header);
        return continue_memory;
    }

    inline int32_t Push(const void* buffer, uint32_t length)
    {
        struct ContEntry* entry_ptr;
        ADK_CHECK_RET_SUCCESS(AllocEntry(length, &entry_ptr));
        memcpy(entry_ptr->GetBuffer(), buffer, length);
        PostEntry(entry_ptr);
        return ErrorCode::kSuccess;
    }

    inline int32_t TryPush(const void* buffer, uint32_t length)
    {
        struct ContEntry* entry_ptr;
        ADK_CHECK_RET_SUCCESS(TryAllocEntry(length, &entry_ptr));
        memcpy(entry_ptr->GetBuffer(), buffer, length);
        PostEntry(entry_ptr);
        return ErrorCode::kSuccess;
    }

    inline ssize_t Pop(void* buffer, uint32_t buf_len)
    {
        struct ContEntry* entry_ptr;
        if (ADK_UNLIKELY(ErrorCode::kSuccess != WaitEntry(&entry_ptr)))
        {
            return 0;
        }

        const auto copy_len = std::min<uint32_t>(buf_len, entry_ptr->app_data_len());
        memcpy(buffer, entry_ptr->GetBuffer(), copy_len);

        FreeEntry(entry_ptr);
        return (ssize_t)copy_len;
    }

    inline ssize_t TryPop(void* buffer, uint32_t buf_len)
    {
        struct ContEntry* entry_ptr;
        if (ADK_UNLIKELY(ErrorCode::kSuccess != TryWaitEntry(&entry_ptr)))
        {
            return 0;
        }

        const auto copy_len = std::min<uint32_t>(buf_len, entry_ptr->app_data_len());
        memcpy(buffer, entry_ptr->GetBuffer(), copy_len);

        FreeEntry(entry_ptr);
        return (ssize_t)copy_len;
    }

    inline int32_t TryAllocEntry(uint32_t length, struct ContEntry** entry_pptr)
    {
        const auto allocate_len = sizeof(struct ContEntry) + CalcAllocSize(length);
        assert(allocate_len <= reserve_size_);

        assert(memory_header_);
        const auto temp_tail = memory_header_->tail + allocate_len;
        if (ADK_UNLIKELY(temp_tail >= memory_header_->tail_threshold))
        {
            memory_header_->tail_threshold = ACCESS_ONCE(memory_header_->head) + memory_size_;
            if (ADK_UNLIKELY(temp_tail >= memory_header_->tail_threshold))
            {
                ++memory_header_->produce_fail_nr;
                return ErrorCode::kWouldblock;
            }
        }

        *entry_pptr = GetContEntry(memory_header_->tail);
        (*entry_pptr)->len = allocate_len;
        (*entry_pptr)->app_len = length;
        return ErrorCode::kSuccess;
    }

    inline int32_t AllocEntry(uint32_t length, struct ContEntry** entry_pptr)
    {
        while (ADK_UNLIKELY(ErrorCode::kSuccess != TryAllocEntry(length, entry_pptr)))
        {
            for (uint32_t index = 0; index < 128; ++index)
            {
                ADK_PAUSE();
            }

            if (ADK_UNLIKELY(ACCESS_ONCE(release_alert_)))
            {
                return ErrorCode::kFailure;
            }
        }
        return ErrorCode::kSuccess;
    }


    // for multi producers to alloc an entry 
    // non block mode
    template<class SpinLockT>
    inline int32_t TryLockAllocEntry(uint32_t length, struct ContEntry** entry_pptr, SpinLockT& lock)
    {
        return TryLockAllocEntryImpl(length, entry_pptr, [&lock](){lock.lock();}, [&lock](){lock.unlock();});
    }

    template<typename LockFunc, typename UnlockFunc>
    inline int32_t TryLockAllocEntryImpl(uint32_t length, struct ContEntry** entry_pptr, const LockFunc& lock, const UnlockFunc& unlock)
    {
        const auto allocate_len = sizeof(struct ContEntry) + CalcAllocSize(length);
        assert(allocate_len <= reserve_size_);
        assert(memory_header_);
        lock();
        const auto temp_tail = memory_header_->reserved_tail + allocate_len;
        if (ADK_UNLIKELY(temp_tail >= memory_header_->tail_threshold))
        {
            memory_header_->tail_threshold = ACCESS_ONCE(memory_header_->head) + memory_size_;
            if (ADK_UNLIKELY(temp_tail >= memory_header_->tail_threshold))
            {
                ++memory_header_->produce_fail_nr;
                unlock();
                return ErrorCode::kWouldblock;
            }
        }
        *entry_pptr            = GetContEntry(memory_header_->reserved_tail);
        (*entry_pptr)->len     = allocate_len;
        (*entry_pptr)->app_len = length;
        memory_header_->reserved_tail += allocate_len;
        unlock();

        return ErrorCode::kSuccess;
    }

    // for multi producers to alloc an entry
    // block mode
    template<class SpinLockT>
    inline ErrorCode LockAllocEntry(uint32_t length, struct ContEntry** entry_pptr, SpinLockT& lock)
    {
        return LockAllocEntryImpl(length, entry_pptr, [&lock](){lock.lock();}, [&lock](){lock.unlock();});
    }

    inline static void AdjustEntryLength(struct ContEntry* entry_ptr, uint32_t length)
    {
        assert(length <= entry_ptr->app_data_len());
        entry_ptr->app_len = length;

        entry_ptr->len = sizeof(struct ContEntry) + ContinueMemory::CalcAllocSize(length);
    }

    inline void PostEntry(struct ContEntry* entry_ptr)
    {
        assert(memory_header_);

        ADK_BARRIER();
        ++memory_header_->produce_nr;
        memory_header_->tail += entry_ptr->usage_len();
    }

    // for multi producers to post an entry
    inline void PostEntryThreadSafe(struct ContEntry* entry_ptr)
    {
        assert(memory_header_);
        ADK_BARRIER();
        ++memory_header_->produce_nr;
        ContEntry* tail_ptr = nullptr;
        while (ADK_UNLIKELY(entry_ptr != (tail_ptr = GetContEntry(memory_header_->tail))))
        {
            ADK_PAUSE();
        }
        memory_header_->tail += entry_ptr->len;
    }
    inline void PostEntry(struct ContEntry* entry_ptr, uint32_t length)
    {
        AdjustEntryLength(entry_ptr, length);
        PostEntry(entry_ptr);
    }

    inline int32_t TryWaitEntry(struct ContEntry** entry_pptr)
    {
        assert(memory_header_);
        if (ADK_UNLIKELY(memory_header_->head >= memory_header_->head_threshold))
        {
            memory_header_->head_threshold = ACCESS_ONCE(memory_header_->tail);
            if (ADK_UNLIKELY(memory_header_->head >= memory_header_->head_threshold))
            {
                ++memory_header_->consume_fail_nr;
                return ErrorCode::kWouldblock;
            }

            if (ADK_UNLIKELY(ACCESS_ONCE(memory_header_->consume_reset)))
            {
                memory_header_->max_clen_snap = static_cast<uint32_t>(memory_header_->head_threshold - memory_header_->head);
                memory_header_->consume_reset = false;
            }
            else
            {
                memory_header_->max_clen_snap = std::max(memory_header_->max_clen_snap, 
                                                         static_cast<uint32_t>(memory_header_->head_threshold - memory_header_->head));
            }
        }

        *entry_pptr = GetContEntry(memory_header_->head);
        return ErrorCode::kSuccess;
    }

    inline int32_t WaitEntry(struct ContEntry** entry_ptr)
    {
        do 
        {
            if (ErrorCode::kSuccess == TryWaitEntry(entry_ptr))
            {
                return ErrorCode::kSuccess;
            }

            for (uint32_t index = 0; index < 128; ++index)
            {
                ADK_PAUSE();
            }

        } while (!ACCESS_ONCE(release_alert_));

        return ErrorCode::kFailure;
    }

    inline void FreeEntry(struct ContEntry* entry_ptr)
    {
        ADK_BARRIER();

        ++memory_header_->consume_nr;
        memory_header_->head += entry_ptr->usage_len();
    }

    template<typename Callback>
    inline void Foreach(const Callback& callback)
    {
        ForeachImpl(callback);
    }

    inline void ForeachImpl(const std::function<bool(char*, uint64_t)>& callback)
    {
        assert(memory_header_);
        auto cursor = memory_header_->head;
        memory_header_->head_threshold = ACCESS_ONCE(memory_header_->tail);
        while (cursor < memory_header_->head_threshold)
        {
            auto* const entry_ptr = GetContEntry(cursor);
            assert(entry_ptr);

            if (!callback(entry_ptr->GetBuffer(), entry_ptr->app_data_len()))
            {
                break;
            }

            cursor += entry_ptr->usage_len();
        }
    }

    inline char* app_ctx()
    {
        assert(memory_header_);
        return memory_header_->app_ctx();
    }

    inline const char* const_app_ctx()
    {
        assert(memory_header_);
        return memory_header_->app_ctx();
    }

    inline uint32_t app_ctx_size() const
    {
        assert(memory_header_);
        return memory_header_->app_ctx_size;
    }

    inline void set_release_alert()
    {
        release_alert_ = true;
    }

    inline void set_context(void* context)
    {
        context_ = context;
    }

    inline void* context() const
    {
        return context_;
    }

    void Touch();

    uint64_t GetConsumeNR() { return ACCESS_ONCE(memory_header_->consume_nr); }
    uint64_t GetProduceNR() { return ACCESS_ONCE(memory_header_->produce_nr); }

    uint64_t GetCurrQlen() 
    {
        return ACCESS_ONCE(memory_header_->produce_nr) - ACCESS_ONCE(memory_header_->consume_nr);
    }

    std::string CollectIndicator() const;

    void CollectIndicator(boost::property_tree::ptree& indicator_ptree) const;

    template<typename LockFunc, typename UnlockFunc>
    inline ErrorCode LockAllocEntryImpl(uint32_t length, struct ContEntry** entry_pptr, const LockFunc& lock, const UnlockFunc& unlock)
    {
        while (ADK_UNLIKELY(ErrorCode::kSuccess != TryLockAllocEntryImpl(length, entry_pptr, lock, unlock)))
        {
            for (uint32_t index = 0; index < 128; ++index)
            {
                ADK_PAUSE();
            }

            if (ADK_UNLIKELY(ACCESS_ONCE(release_alert_)))
            {
                return ErrorCode::kFailure;
            }
        }
        return ErrorCode::kSuccess;
    }

protected:
    ContinueMemory()
    {
        memory_header_ = nullptr;
        memory_entry_ = nullptr;
        memory_size_ = 0;
        reserve_size_ = 0;
        memory_size_mask_ = 0;
        release_alert_ = false;

        context_ = nullptr;
    }

    void Init(ContMemoryHeader* memory_header)
    {
        memory_header_ = memory_header;
        memory_entry_ = memory_header->entries();
        memory_size_ = memory_header->memory_size;
        reserve_size_ = memory_header->reserve_size;
        memory_size_mask_ = memory_size_ - 1;
    }

    inline struct ContEntry* GetContEntry(uint64_t monotonic_pos)
    {
        return (struct ContEntry*)ptr_add(memory_entry_, monotonic_pos & memory_size_mask_);
    }

    inline static uint32_t CalcAllocSize(uint32_t length)
    {
        return (length + (kAlignedSize - 1)) & kAlignedBits;
    }

private:
    ContMemoryHeader* memory_header_;
    void*             memory_entry_;
    uint32_t          memory_size_;
    uint32_t          reserve_size_;
    uint32_t          memory_size_mask_;
    bool              release_alert_;

    void*             context_;

};

class ShmContMemManager
{
public:
    struct ShmManagerHeader
    {
        char     shm_name[ADK_MAX_NAME_LEN];
        uint32_t header_size;
        uint32_t cont_mem_index;
        uint32_t cont_mem_limit;
        uint32_t memory_offset_used;
        uint32_t memory_offset_limit;

        void* AllocMemory(uint32_t memory_size)
        {
            if (memory_offset_used + memory_size <= memory_offset_limit)
            {
                return ptr_add(this, memory_offset_used);
            }

            return nullptr;
        }

        void PostMemory(uint32_t memory_size)
        {
            memory_offset_used += memory_size;
        }

        void* Allocate(uint32_t memory_size)
        {
            const auto new_offset_used = memory_offset_used + memory_size;
            if (new_offset_used <= memory_offset_limit)
            {
                void* memory = ptr_add(this, memory_offset_used);
                memory_offset_used = new_offset_used;
                return memory;
            }

            return nullptr;
        }
    };

    static ShmContMemManager* Create(const std::string& shm_name, 
                                     uint32_t cont_mem_num, 
                                     uint32_t total_size, 
                                     uint32_t app_ctx_size = 0);

    template<typename AppCtxType>
    static ShmContMemManager* Create(const std::string& shm_name, 
                                     uint32_t cont_mem_num, 
                                     uint32_t total_size)
    {
		return DoCreate(shm_name, cont_mem_num, total_size, sizeof(AppCtxType),[](void* buffer){
			new (buffer) AppCtxType();
		});
    }

    static ShmContMemManager* DoCreate(const std::string& shm_name, 
                                     uint32_t cont_mem_num, 
                                     uint32_t total_size,
                                     uint32_t ctx_size,
									 const std::function<void(void*buffer)>& callback);

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

    inline void set_context(void* context)
    {
        context_ = context;
    }

    inline void* context() const
    {
        return context_;
    }


private:
    ShmContMemManager()
    {
        shm_header_ = nullptr;
        memory_headers_ = nullptr;
        memory_header_size_ = 0;
        context_ = nullptr;
    }


    static ShmManagerHeader* CreateShm(const std::string& shm_name,
                                       uint32_t cont_mem_num,
                                       uint32_t total_size,
                                       uint32_t app_ctx_size);

    void Init(ShmManagerHeader* shm_header, const std::string& shm_name)
    {
        shm_name_ = shm_name;
        shm_header_ = shm_header;
        memory_headers_ = (ContMemoryHeader*)ptr_add(shm_header, shm_header->header_size);
        memory_header_size_ = shm_header->header_size;
    }

    std::string        shm_name_;
    ShmManagerHeader*  shm_header_;
    ContMemoryHeader*  memory_headers_;
    uint32_t           memory_header_size_;
    std::map<std::string, uint32_t> name_to_index_map_;

    void*              context_;
};

}

#endif
