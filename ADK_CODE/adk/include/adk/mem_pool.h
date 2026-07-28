#ifndef ADK_IMPL_MEM_POOL_H_
#define ADK_IMPL_MEM_POOL_H_

#include "id.h"
#include "shm.h"
#include "error_code.h"
#include "arch/generic.h"
#include "memory_buffer.h"
#include "lock_free_msg_queue.h"

#include <map>
#include <string>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/property_tree/ptree.hpp>

namespace adk_impl
{

using std::map;
using std::string;

struct MemoryPoolHeader
{
    uint32_t                    mp_block_size;
    uint32_t                    mp_block_num;
    uint32_t                    mp_emergent_block_num;
    uint32_t                    mp_block_offset;                // offset start from this header
    uint32_t                    padding;
    struct QueueMemoryHeader    queue_header;
    struct QueueMemoryHeader    emergent_queue_header;

    char* blocks()
    {
        return (char*)(ptr_add(this, mp_block_offset));
    }
};

class MemoryPool
{
public:
    MemoryPool()
    {
		is_release_alert_ = false;    // FIXME : initialized in Init();
        is_release_alert_always_ = false;
	}

    ~MemoryPool()
    {}

    int32_t Init(MemoryPoolHeader* pool_header, uint16_t pool_index, bool do_init = true);

    inline MemoryBuffer* AllocBuffer(uint32_t len);
    inline int32_t FreeBuffer(MemoryBuffer* mem_buf);

    /**
     * @note MPManager::AttachSharedPool得到的MemoryPool，使用本方法
     * 得到的MemoryBuffer都是无效的
     */
    #ifdef __ADK_MP_FAILURE_TEST__
    inline MemoryBuffer* NewBuffer(uint32_t fp = 0)
    #else
    inline MemoryBuffer* NewBuffer()
    #endif
    {
        struct Entry* entry;
        if (buffer_queue_.WaitEntry(&entry) != kSuccess)
            return NULL;

        #ifdef __ADK_MP_FAILURE_TEST__
        if (fp == 1)
            return NULL;
        #endif

        char* buf = entry->buffer;
        MemoryBuffer* ret_buf = ptr_add((MemoryBuffer*)(pool_header_),
                                        *((uint64_t*)buf));        // (1) the buffer should have been reset

        buffer_queue_.FreeEntry(entry);

        return ret_buf;
    }

    #ifdef __ADK_MP_FAILURE_TEST__
    MemoryBuffer* NewEmergentBuffer(uint32_t fp = 0);
    #else
    MemoryBuffer* NewEmergentBuffer();
    #endif
    
    #ifdef __ADK_MP_FAILURE_TEST__
    inline int32_t DeleteBuffer(MemoryBuffer* mem_buf, uint32_t fp = 0)
    #else
    inline int32_t DeleteBuffer(MemoryBuffer* mem_buf)
    #endif
    {
        SPMCQueue* queue_ptr = &buffer_queue_;
        if (ADK_UNLIKELY(IS_EMERGENT_BUFFER(mem_buf)))
            queue_ptr = &emergent_buffer_queue_;

        #ifdef __ADK_MP_FAILURE_TEST__
        return DoDeleteBuffer(*queue_ptr, mem_buf, fp);
        #else
        return DoDeleteBuffer(*queue_ptr, mem_buf);
        #endif
    }

    uint16_t index()
    {
        return pool_index_;
    }

    string name()
    {
        return buffer_queue_.name();
    }

    uint32_t block_size()
    {
        return pool_header_->mp_block_size;
    }

    uint32_t block_num()
    {
        return pool_header_->mp_block_num;
    }

    uint32_t emergent_block_num()
    {
        return pool_header_->mp_emergent_block_num;
    }

	void ReleaseAllocThread()
    {
        is_release_alert_ = true;
    }

    void ReleaseAllocThreadAlways()
    {
        is_release_alert_always_ = true;
    }

    bool Empty()
    {
        return buffer_queue_.length() == 0
               && emergent_buffer_queue_.length() == 0;
    }

    /**
     * @brief      to make the share memory status consistent, after a failure recovery. 
     *
     * @return     on success, return ErrorCode::kSuccess
     */
    int32_t Consistent();

    void CollectIndicator(boost::property_tree::ptree& indicator_ptree);

#ifndef __ADK_DEBUG__
private:
#endif
    MemoryPoolHeader*           pool_header_;
    uint16_t                    pool_index_;
    SPMCQueue                   buffer_queue_;
    SPMCQueue                   emergent_buffer_queue_;
	volatile bool               is_release_alert_;
    volatile bool               is_release_alert_always_;

    #ifdef __ADK_MP_FAILURE_TEST__
    inline int32_t DoDeleteBuffer(SPMCQueue& queue, MemoryBuffer* mem_buf, uint32_t fp = 0)
    #else
    inline int32_t DoDeleteBuffer(SPMCQueue& queue, MemoryBuffer* mem_buf)
    #endif
    {
        if (mem_buf->shm_ptr.mp_index() != pool_index_)
            return kMemPoolIndexError;

        struct Entry* entry;
        if (queue.AllocEntry(&entry) != kSuccess)
            return kMemPoolDoubleDelete;

        #ifdef __ADK_MP_FAILURE_TEST__
        if (fp == 1)
            return kFailure;
        #endif

        mem_buf->reset();                               // (1)
        char* buf = entry->buffer;
        *((int64_t*)buf) = ptr_diff(mem_buf, pool_header_);
        queue.PostEntry(entry);
        return kSuccess;
    }

    friend class MPManager;
};

typedef void (*MPManagerExceptionHandler)(void* data);
extern MPManagerExceptionHandler g_mpm_except_handler;
extern void* g_mpm_except_handler_data;

class MPManager
{
public:
    typedef std::map<std::string, uint16_t> NameInexMapType;
    
    enum Const
    {
        kMaxSharedMempool      =    32,
    };

    struct MPTable
    {
        uint32_t    nr_mps;
        char        mp_name[kMaxSharedMempool][ADK_MAX_NAME_LEN];
    };

    MPManager();
    ~MPManager()
    {}

    int32_t CreateMPTable(const string& table_name);
    int32_t AttachMPTable(const string& table_name);
    int32_t DetachAll();                                   // FIXME : memory leak! MemoryPool object should be delete after Detach and Destroy
    int32_t DestroyAll();

    MemoryPool* CreateSharedPool(const string& name, uint32_t block_size, uint32_t block_num, uint32_t emergent_block_num = 0);

    /**
     *
     * @note 使用本方法得到的MemoryPool，MemoryPool::NewBuffer
     *       方法得到的buffer都是无效的buffer
     */
    MemoryPool* AttachSharedPool(uint16_t index);
    MemoryPool* AttachSharedPool(const std::string& name);

    int32_t DestroySharedPool(const string& name);

    MemoryPool* CreatePrivatePool(const string& name, uint32_t block_size, uint32_t block_num);
    uint32_t DestroyPrivatePool(const string& name);

    inline MemoryBuffer* ShmPtrToMemBuf(ShmPointer* shm_ptr)
    {
        return (MemoryBuffer*)(ptr_add(IndexToMempool<true>(shm_ptr->mp_index())->pool_header_,
                                       shm_ptr->offset()));
    }

    // Note: this method is not thread safe
    template<bool is_except = false>
    inline MemoryPool* IndexToMempool(uint16_t index)
    {
        // FIXME: check index boudary and special values
        MemoryPool* mem_pool = index_to_mempool_[index];
        if (ADK_UNLIKELY(mem_pool == NULL))
        {
            boost::recursive_mutex::scoped_lock lock_guard(mpm_create_attach_lock());
            if (is_except)
            {
                mem_pool = AttachSharedPool(index);
                if (mem_pool == NULL)
                {
                    if (g_mpm_except_handler != NULL)
                        g_mpm_except_handler(g_mpm_except_handler_data);
                    abort();
                }
                return mem_pool;
            }
            return AttachSharedPool(index);
        }
        return mem_pool;
    }

    inline MemoryPool* GetMPByMemBuf(MemoryBuffer* mem_buf)
    {
        return index_to_mempool_[mem_buf->shm_ptr.mp_index()];
    }

    #ifdef __ADK_MP_FAILURE_TEST__
    inline int32_t DeleteBuffer(MemoryBuffer* mem_buf, uint32_t fp = 0)
    #else
    inline int32_t DeleteBuffer(MemoryBuffer* mem_buf)
    #endif
    {
        #ifdef __ADK_MP_FAILURE_TEST__
        return IndexToMempool(mem_buf->shm_ptr.mp_index())->DeleteBuffer(mem_buf, fp);
        #else
        return IndexToMempool(mem_buf->shm_ptr.mp_index())->DeleteBuffer(mem_buf);
        #endif
    }

    std::string GetMPTableName() const
    { return mp_table_name_; }

    static void set_except_handler(MPManagerExceptionHandler handler, void* data);
    
private:
    struct MPTable*  mp_table_;
    MemoryPool*      index_to_mempool_[kMaxSharedMempool];
    NameInexMapType  name_to_index_map_;
    uint32_t         last_iterate_;
    int32_t          ref_counter_;
    std::string      mp_table_name_;
    static std::map<std::string, int32_t> s_mpm_ref_map;
    static boost::mutex                   s_mpm_lock;

    boost::recursive_mutex& mpm_create_attach_lock();

    void IterateMPTable();
    void Clear();
    void IncreaseReference(const std::string& table_name);
    int32_t DecreaseReference(const std::string& table_name);
};

} // adk

#endif // ADK_MEM_POOL_H_
