/**
 * @file
 * @brief      单生产者消费者对象池，单生产者消费者共享内存对象池
 * @author     zhaonan, zhaonan@archforce.com.cn
 * @date       2017/01/23
 */

#ifndef ADK_IMPL_OBJECT_POOL_H_
#define ADK_IMPL_OBJECT_POOL_H_

#include "mem_pool.h"

namespace adk_impl
{

/**
 * @brief      对象池接口
 */
class IPool
{
public:
    IPool() {}
    ~IPool() {}

    /**
     * @brief      删除对象所对应的内存块
     *
     * @param      mem_buf  内存块地址
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    int32_t Delete(MemoryBuffer* mem_buf)
    {
        assert(mem_buf != NULL);

        return mem_pool_->DeleteBuffer(mem_buf);
    }

protected:
    MemoryPool* mem_pool_;
};

/**
 * @brief      对象池对象接口
 */
class IObject
{
public:
    virtual ~IObject() {}

    /**
     * @brief      删除对象
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    int32_t Delete()
    {
        MemoryBuffer* mem_buf = ADK_CONTAINER_OF(this, MemoryBuffer, data);

        Reset();

        if (ADK_UNLIKELY(IS_SYSMEM_BUFFER(mem_buf)))
        {
            this->~IObject();
            free(mem_buf);
            return ErrorCode::kSuccess;
        }
        
        return obj_pool_->Delete(mem_buf);
    }

    void set_obj_pool(IPool* obj_pool)
    {
        obj_pool_ = obj_pool;
    }

    /**
     * @brief      重置对象
     */
    virtual void Reset() {};

private:
    IPool*  obj_pool_;
};

typedef void(*construct_type)(void*);

template<typename ElementType>
void DefaultConstructor(void* buffer)
{
    new (buffer) ElementType();
}

/**
 * @brief      对象池
 *
 * @tparam     ElementType  对象池中缓存的对象类型
 */
template<typename ElementType>
class ObjectPool : public IPool
{
public:
    ObjectPool() {}
    ~ObjectPool() {}

    /**
     * @brief      创建对象池
     *
     * @param[in]  pool_name  对象池名称
     * @param[in]  size       对象池容量
     * @param[in]  reserve    保留块大小
     * @param[in]  constructor  构造函数
     *
     * @return     成功时返回对象池引用，失败时返回NULL
     */
    static ObjectPool<ElementType>* Create(const string& pool_name, 
                                           uint32_t size, 
                                           uint32_t reserve = 0, 
                                           construct_type constructor = DefaultConstructor<ElementType>)
    {
        uint32_t block_size = sizeof(ElementType) + reserve + sizeof(MemoryBuffer);       // FIXME: cache alignment?
        uint32_t block_num = ADK_ROUND_TO_POWER_OF_2(size);

        // ------>|memorypoolheader|xxx------>|mq_mem|xxx------>|pool_mem|
        MemoryPoolHeader* mem_pool_header = (MemoryPoolHeader*)malloc(
            sizeof(MemoryPoolHeader) + Entry::CalcEntrySize(sizeof(MemoryBuffer*)) * block_num
            + block_size* block_num + ADK_PAGE_SIZE * 3);

        if (mem_pool_header == NULL)
            return NULL;

        mem_pool_header->mp_block_size = block_size;
        mem_pool_header->mp_block_num = block_num;
        mem_pool_header->mp_emergent_block_num = 0;
        mem_pool_header->mp_block_offset = 0;

        QueueMemoryHeader& mq_header = mem_pool_header->queue_header;
        NameCopy(mq_header.queue_name, pool_name);

        mq_header.entry_size = Entry::CalcEntrySize(sizeof(MemoryBuffer*));
        mq_header.queue_mask = block_num - 1;
        mq_header.queue_size = block_num;
        mq_header.queue_entry_offset = ptr_diff(ADK_PAGE_ALIGN(ptr_add(mem_pool_header, sizeof(MemoryPoolHeader))),
                                                &mq_header);
        mq_header.reserve = 0;
        mq_header.tail = 0;
        mq_header.head = 0;
        mq_header.release = 0;

        // init emergent queue
        QueueMemoryHeader& emergent_mq_header = mem_pool_header->emergent_queue_header;
        NameCopy(emergent_mq_header.queue_name, "object pool unused");

        emergent_mq_header.entry_size = 0;
        emergent_mq_header.queue_mask = 0;
        emergent_mq_header.queue_size = 0;
        emergent_mq_header.queue_entry_offset = 0;

        emergent_mq_header.reserve = 0;
        emergent_mq_header.tail = 0;
        emergent_mq_header.head = 0;
        emergent_mq_header.release = 0;

        struct Entry* entry_end = MPSCQueue::InitEntries(mq_header.entries(), block_num, sizeof(MemoryBuffer*));
        mem_pool_header->mp_block_offset = ptr_diff(ADK_PAGE_ALIGN(entry_end), mem_pool_header);

        MemoryPool* mem_pool = (MemoryPool*)malloc(sizeof(MemoryPool));
        mem_pool->Init(mem_pool_header, -1);

        ObjectPool<ElementType>* object_pool = new ObjectPool<ElementType>();

        for (uint32_t i = 0 ; i < block_num; ++i)
        {
            MemoryBuffer* mem_buf = mem_pool->NewBuffer();
            constructor(mem_buf->data);
            ElementType* obj_ptr = (ElementType*)(mem_buf->data);
            obj_ptr->Reset();
            obj_ptr->set_obj_pool(object_pool);
            mem_pool->DeleteBuffer(mem_buf);
        }

        object_pool->mem_pool_ = mem_pool;
        object_pool->constructor_ = constructor;
        return object_pool;
    }

    /**
     * @brief      从对象池申请一个对象
     *
     * @return     成功时返回对象引用，失败时返回NULL
     */
    ElementType* NewObject()
    {
        MemoryBuffer* mem_buf = mem_pool_->NewBuffer();
        if (ADK_UNLIKELY(mem_buf == NULL))
        {
            return NULL;
        }

        return (ElementType*)(mem_buf->data);
    }

    ElementType* NewObjectEx(bool is_force_new = true, uint32_t reserve = 0)
    {
        MemoryBuffer* mem_buf = mem_pool_->NewBuffer();
        if (ADK_UNLIKELY(mem_buf == NULL))
        {
            if (!is_force_new)
            {
                return NULL;
            }

            mem_buf = (MemoryBuffer*)malloc(Entry::CalcEntrySize(sizeof(MemoryBuffer) + sizeof(ElementType) + reserve));
            if (ADK_UNLIKELY(mem_buf == NULL))
            {
                return NULL;
            }

            #ifdef __ADK_DEBUG__
            ++nr_sys_mem_;
            #endif

            mem_buf->mem_buf_flag |= ADK_MEMORY_BUFFER_SYS;
            ElementType* obj_ptr = (ElementType*)mem_buf->data;
            constructor_(obj_ptr);
            obj_ptr->Reset();
        }

        return (ElementType*)(mem_buf->data);
    }

#ifdef __ADK_DEBUG__
    uint64_t nr_sys_mem_ = 0;
#endif
private:
    construct_type constructor_;
};

/**
 * @brief      共享内存对象接口
 */
class ISharedObject
{
public:
    ISharedObject()
    {
        share_proc_counter_ = 0;
    }

    virtual ~ISharedObject() {}

    /**
     * @brief      重置对象
     */
    virtual void Reset() {}

    /**
     * @brief      返回对象序列化后的Byte Buffer首地址
     *
     * @return     成功时返回Byte Buffer首地址，失败时返回NULL
     */
    virtual void* Serialize()
    {
        return this;
    }

    /**
     * @brief      删除共享内存对象
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    int32_t Delete()
    {
        clear_share_proc_counter();
        return ErrorCode::kSuccess;
    }

    /**
     * @brief      获取该对象的共享内存指针
     *
     * @return     共享内存指针
     */
    ShmPointer& GetShmPtr()
    {
        MemoryBuffer* mem_buf = ADK_CONTAINER_OF(this, MemoryBuffer, data);
        return mem_buf->shm_ptr;
    }

    void set_share_proc_counter()
    {
        share_proc_counter_ = 1;
    }

    void clear_share_proc_counter()
    {
        ADK_BARRIER();
        share_proc_counter_ = 0;
    }

    /**
     * @brief      检测共享内存对象是否还在使用
     *
     * @return     如果共享内存对象在被其它进程使用则返回true
     */
    bool IsUsed() 
    {
        return share_proc_counter_ != 0; 
    }

private:
    uint32_t    share_proc_counter_;

    template<typename ElementType>
    friend class ObjectPool;
};

/**
 * @brief      共享内存对象池，该对象池中的对象可以在父子进程间交换，一般由父进程递交给子进程
 *
 * @tparam     ElementType  对象池中的对象类型
 */
template<typename ElementType>
class SharedObjectPool : public IPool
{
public:
    SharedObjectPool() {}
    ~SharedObjectPool() {}

    static uint32_t calc_block_size() { return sizeof(ElementType) + sizeof(MemoryBuffer); }

    /**
     * @brief      创建共享内存对象池
     *
     * @param      mem_pool  共享内存池
     *
     * @return     成功时返回对象池引用，失败时返回NULL
     */
    static SharedObjectPool<ElementType>* Create(MemoryPool* mem_pool)
    {
        SharedObjectPool<ElementType>* object_pool = new SharedObjectPool<ElementType>();

        for (int32_t i = 0 ; i < mem_pool->block_num(); ++i)
        {
            MemoryBuffer* mem_buf = mem_pool->NewBuffer();
            new (mem_buf->data) ElementType();
            ElementType* obj_ptr = (ElementType*)(mem_buf->data);
            obj_ptr->Reset();
            mem_pool->DeleteBuffer(mem_buf);
        }

        object_pool->mem_pool_ = mem_pool;
        return object_pool;
    }

    /**
     * @brief      从对象池申请一个共享内存对象
     *
     * @return     成功时返回共享内存对象引用，失败时返回NULL
     */
    ElementType* NewObject()
    {
        MemoryBuffer* mem_buf = mem_pool_->NewBuffer();
        if (ADK_UNLIKELY(mem_buf == NULL))
            return NULL;

        ElementType* obj_ptr = (ElementType*)(mem_buf->data);
        obj_ptr->set_share_proc_counter();
        return obj_ptr;
    }
};

} // adk

#endif // ADK_OBJECT_POOL_H_
