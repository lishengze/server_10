#ifndef ADK_IMPL_IO_ENGINE_MESSAGE_POOL_H_
#define ADK_IMPL_IO_ENGINE_MESSAGE_POOL_H_

#include "message_impl.h"

#include <adk/memory_pool_variant.h>

namespace adk_impl
{

namespace io_engine
{

class RxMemoryPool
{
public:
    using CacheQueueType = variant::MPSCQueue<void*>;

    static RxMemoryPool* Create(uint32_t block_size, uint32_t block_num);

    static void Destroy(RxMemoryPool* memory_pool);

    inline void* NewMemory()
    {
        assert(memory_cache_queue_);

        void* buffer;
        if (ErrorCode::kSuccess == memory_cache_queue_->TryPop(buffer))
        {
            return buffer;
        }

        return default_allocator_.NewMemory<false>(block_size_);
    }

    static inline void* NewMemory(uint32_t len)
    {
        return variant::MemoryAllocator::NewRawMemory(len);
    }

    static inline void DeleteMemory(void* buffer)
    {
        auto* const memory_block = variant::MemoryBlock::block(buffer);
        const auto block_ctx = memory_block->block_ctx();

        if (block_ctx > 0)
        {
        retry:
            const auto ec = reinterpret_cast<CacheQueueType*>(block_ctx)->Push(buffer);
            if (ADK_UNLIKELY(ErrorCode::kSuccess != ec))
            {
                ADK_PAUSE();
                goto retry;
            }
        }
        else
        {
            variant::MemoryAllocator::DeleteBlock(memory_block);
        }
    }

    uint32_t block_size() const
    {
        return block_size_;
    }

#ifndef __ADK_UNIT_TEST__
private:
#endif

    uint32_t        block_size_;
    void*           memory_header_;
    CacheQueueType* memory_cache_queue_;
    variant::MemoryAllocator default_allocator_;
};

class TxMessagePool
{
public:
    TxMessagePool();
    ~TxMessagePool();

    inline MessageImpl* NewMessage(uint32_t len)
    {
        assert(tx_memory_pool_);
        auto* const message_impl = (MessageImpl*)tx_memory_pool_->NewMemory(MessageImpl::PrefixSize() + len);
        assert(message_impl);

        message_impl->Reset(len);
        return message_impl;
    }

    inline static void DeleteMessage(MessageImpl* message_impl)
    {
        assert(message_impl);
        assert(message_impl->is_direction_tx());
        TxMemoryPool::DeleteMemory(message_impl);
    }

private:
    using TxMemoryPool = variant::MemoryPool<variant::MPMCQueue>;

    TxMemoryPool* tx_memory_pool_;
};

class RxMessagePool
{
public:
    RxMessagePool();
    ~RxMessagePool();

    void Init(uint32_t block_size, uint32_t block_num);

    uint32_t message_capacity() const
    {
        assert(rx_memory_pool_);
        return rx_memory_pool_->block_size() - MessageImpl::PrefixSize();
    }

    template<bool kDirectCtxSet = false>
    inline MessageImpl* NewMessage(void* context)
    {
        assert(rx_memory_pool_);
        auto* const message_impl = (MessageImpl*)rx_memory_pool_->NewMemory();
        assert(message_impl);

        message_impl->Reset<kDirectCtxSet>(message_capacity(), context);
        return message_impl;
    }

    template<bool kDirectCtxSet>
    inline static MessageImpl* NewMessage(uint32_t len, void* context)
    {
        auto* const message_impl = (MessageImpl*)(RxMemoryPool::NewMemory(len + MessageImpl::PrefixSize()));
        assert(message_impl);

        message_impl->Reset<kDirectCtxSet>(len, context);
        return message_impl;
    }

    inline static void DeleteMessage(MessageImpl* message_impl)
    {
        assert(message_impl);
        assert(!message_impl->is_direction_tx());

        if (ADK_UNLIKELY(message_impl->is_resized_message()))
        {
            /**
             * delete memory can not use MessageImpl::data()
             * 
             * member function data() may not equsl to app_data 
             */
            RxMemoryPool::DeleteMemory(message_impl->app_data());
        }

        RxMemoryPool::DeleteMemory(message_impl);
    }

private:
    RxMemoryPool* rx_memory_pool_;
};

}

}

#endif