#ifndef ADK_IMPL_SHM_CHANNEL_H_
#define ADK_IMPL_SHM_CHANNEL_H_

#include "error_code.h"
#include "index_allocator.h"
#include "singleton_process.h"

#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <functional>

#include <assert.h>

namespace adk_impl
{

namespace sccl
{

constexpr uint32_t kShmSize = 16 * 1024 * 1024;
constexpr uint32_t kReserveSize = 1 * 1024 * 1024;

constexpr uint32_t kAlignedAdden = sizeof(uint64_t) - 1;
constexpr uint32_t kAlignedMask = ~kAlignedAdden;

constexpr uint32_t kIndexShift = 48;
constexpr uint64_t kCursorMask = (static_cast<uint64_t>(1) << kIndexShift) - 1;

constexpr uint32_t kMaxProxySize = 1024;

constexpr uint32_t Aligned(uint32_t length)
{
    return (length + kAlignedAdden) & kAlignedMask;
}

constexpr uint64_t Restore(uint64_t mixed)
{
    return mixed & kCursorMask;
}

constexpr uint16_t Unmixed(uint64_t mixed)
{
    return static_cast<uint16_t>(mixed >> kIndexShift);
}

constexpr bool IsMixed(uint64_t mixed)
{
    return 0 != Unmixed(mixed);
}

constexpr uint64_t Mix(uint16_t index, uint64_t cursor)
{
    return (static_cast<uint64_t>(index) << kIndexShift) + cursor;
}

#define ADK_SCCL_BACKOFF(backoff, limit, action)            \
{                                                           \
    if (backoff < limit)                                    \
    {                                                       \
        for (uint32_t index = 0; index < backoff; ++index)  \
        {                                                   \
            ADK_PAUSE();                                    \
        }                                                   \
        backoff <<= 1;                                      \
        action;                                             \
    }                                                       \
}

/**
 * ShmContHeader    (64)
 * WAL              (64) [1024]
 * MemoryEntries    power of two
 * ReserveMemroy
 */

struct WAL
{
    uint16_t index;
    uint32_t size_limit;
    uint32_t memory_size;
    uint32_t memory_mask;

    uint64_t cursor;
    uint64_t threshold;
};

struct ShmContHeader
{
    //> consumer
    uint64_t    head __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint64_t    head_threshold;
    uint32_t    memory_mask_c;
    uint32_t    reserve_size_c;

    //> producer
    uint64_t    tail __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
};

struct Entry
{
public:
    inline void* Buffer()
    {
        return &buffer[0];
    }

    inline uint32_t BufferSize() const
    {
        return atomic_mixed & kLengthMask;
    }

    inline uint16_t Index() const
    {
        return atomic_mixed >> kIndexShift;
    }

private:
    static constexpr uint32_t kLengthShift = kIndexShift / 2;
    static constexpr uint32_t kMaxLength = static_cast<uint32_t>(1) << kLengthShift;
    static constexpr uint32_t kLengthMask = kMaxLength - 1;

    static constexpr uint64_t Acquire(uint16_t index, uint32_t len)
    {
        return (static_cast<uint64_t>(index) << kIndexShift) | (static_cast<uint64_t>(len) << kLengthShift);
    }

    static constexpr size_t Size()
    {
        return ADK_OFFSET_OF(Entry, buffer);
    }

    static inline Entry* GetEntry(void* buffer)
    {
        return reinterpret_cast<Entry*>(reinterpret_cast<char*>(buffer) - Size());
    }

    inline void Set(uint64_t mixed)
    {
        atomic_mixed = mixed;
    }

    inline void SetBufferSize(uint32_t length)
    {
        assert(0 == BufferSize());
        assert(length <= kMaxLength);
        assert(length <= AllocatedLength());

        atomic_mixed += length;
    }

    inline void Release()
    {
        atomic_mixed &= kCursorMask;
    }

    inline uint32_t AllocatedLength() const
    {
        return (atomic_mixed >> kLengthShift) & kLengthMask;
    }

    inline bool IsAcquired(uint16_t index) const
    {
        return (index == Index()) && (0 == BufferSize());
    }

    uint64_t atomic_mixed;
    char     buffer[];

    friend class Agent;
    friend class Proxy;
};

class AgentEventHandler
{
public:
    /**
     * @brief   有新的Proxy连接
     *
     * @param   process   proxy名称
     * @param   pid       proxy进程的进程ID
     * @param   tid       proxy线程的线程ID
     *
     * @return  true  : 接受该Proxy的数据
     *          false : 拒绝该Proxy的数据
     */
    virtual bool OnNewProxy(const std::string& process, int32_t pid, int32_t tid) = 0;

    /**
     * @brief   Proxy断开连接
     *
     * @param   process   proxy名称
     * @param   pid       proxy进程的进程ID
     * @param   tid       proxy线程的线程ID
     */
    virtual void OnProxyBroken(const std::string& process, int32_t pid, int32_t tid) = 0;
};

class Agent
{
public:
    static Agent* Create(const std::string& name, 
                         AgentEventHandler* event_handler = nullptr,
                         bool do_recovery = true,
                         uint32_t memory_size = kShmSize, 
                         uint32_t max_message_size = kReserveSize);

    static void Destroy(Agent* agent, bool do_shm_destroy = false);

    struct Entry* TryWaitEntry()
    {
        assert(shm_header_);

    retry:
        if (ADK_UNLIKELY(shm_header_->head >= shm_header_->head_threshold))
        {
            shm_header_->head_threshold = Restore(ACCESS_ONCE(shm_header_->tail));
            if (ADK_UNLIKELY(shm_header_->head >= shm_header_->head_threshold))
            {
                return nullptr;
            }
        }

        auto* const entry = GetEntry(shm_header_->head);
        if (entry->BufferSize() > 0)
        {
            return entry;
        }

        if (0 == entry->Index())
        {
            FreeEntry(entry);
            goto retry;
        }

        return nullptr;
    }

    void FreeEntry(struct Entry* entry_ptr)
    {
        shm_header_->head += entry_ptr->AllocatedLength();
    }

private:
    Agent(const std::string& name, AgentEventHandler* event_handler);
    ~Agent();

    bool Init(bool do_recovery, uint32_t memory_size, uint32_t max_message_size);

    uint16_t AllocWAL();

    void FreeWAL(uint16_t index);

    void AllocProxy(int32_t fd);

    void FreeProxy(int32_t fd);

    void OnMessage(int32_t fd, void* buffer);

    Entry* GetEntry(uint64_t cursor)
    {
        return reinterpret_cast<Entry*>(shm_entries_ + (cursor & shm_header_->memory_mask_c));
    }

    struct ProxyInfo
    {
        int32_t     pid;
        std::string name;
        std::map<uint16_t, int32_t> index2tid_map;
    };

    ShmContHeader*      shm_header_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    char*               shm_entries_;

    bool                is_running_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    int32_t             dmsk_fd_;
    int32_t             epoll_fd_;
    std::string         name_;
    SingletonProcess*   singleton_process_;
    AgentEventHandler*  event_handler_;
    std::thread         actor_thrd_;
    std::map<int32_t, ProxyInfo>  proxy_map_;
    IndexAllocator<kMaxProxySize> index_allocator_;
};

class Proxy
{
public:
    static Proxy* Create(const std::string& agent_name, 
                         const std::string& proxy_name = "",
                         const std::function<bool()>& broken_cb = []() { return true; });

    static void Destroy(Proxy* proxy);

    void* AllocBuffer(uint32_t length)
    {
        auto* wal = ACCESS_ONCE(wal_);
        if (ADK_UNLIKELY(nullptr == wal))
        {
            if (ADK_UNLIKELY(nullptr == (wal = CreateWAL())))
            {
                return nullptr;
            }
        }

        const auto alloc_len = Aligned(Entry::Size() + length);
        if (ADK_UNLIKELY(alloc_len > wal->size_limit))
        {
            return nullptr;
        }

        assert(shm_header_);
        uint64_t backoff = 32;
        do
        {
            const auto tail = ACCESS_ONCE(shm_header_->tail);
            if (ADK_UNLIKELY(IsMixed(tail)))
            {
                ADK_SCCL_BACKOFF(backoff, 1024, continue);
                return nullptr;
            }

            const auto new_tail = tail + alloc_len;
            if (ADK_UNLIKELY(new_tail > wal->threshold))
            {
                wal->threshold = ACCESS_ONCE(shm_header_->head) + wal->memory_size;
                if (ADK_UNLIKELY(new_tail > wal->threshold))
                {
                    return nullptr;
                }
            }

            wal->cursor = tail;
            Entry* const entry = GetEntry(wal, tail);
            const auto acquire = Entry::Acquire(wal->index, alloc_len);
            if (__sync_bool_compare_and_swap(&(shm_header_->tail), tail, Mix(wal->index, tail)))
            {
                entry->Set(acquire);

                ADK_BARRIER();
                shm_header_->tail = new_tail;
                return entry->Buffer();
            }
        } while (ACCESS_ONCE(status_valid_));
        return nullptr;
    } 

    void PostBuffer(void* buffer, uint32_t buf_size)
    {
        auto* const entry = Entry::GetEntry(buffer);

        ADK_BARRIER();
        entry->SetBufferSize(buf_size);
    }

    inline bool status_valid() const
    {
        return ACCESS_ONCE(status_valid_);
    }

private:
    Proxy(const std::string& agent_name, 
          const std::string& proxy_name, 
          const std::function<bool()>& broken_cb);
    virtual ~Proxy();

    bool Init();

    bool DoConnect();

    WAL* CreateWAL();

    void DestroyWAL();

    inline Entry* GetEntry(WAL* wal, uint64_t cursor)
    {
        return reinterpret_cast<Entry*>(shm_entries_ + (cursor & wal->memory_mask));
    }

    static thread_local struct WAL* wal_;

    struct ShmContHeader* shm_header_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    char*                 shm_entries_;
    bool                  status_valid_;

    bool                  is_running_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));;
    int32_t               dmsk_fd_;
    std::string           agent_name_;
    std::string           proxy_name_;
    std::mutex            ipc_lock_;
    std::thread           ipc_actor_;
    std::vector<void*>    wal_registry_vec_;
    std::function<bool()> broken_cb_;

    friend class Registry;
};

}

}

#undef ADK_SCCL_BACKOFF
#endif