#ifndef ADK_IMPL_MEMORY_POOL_VARIANT_H_
#define ADK_IMPL_MEMORY_POOL_VARIANT_H_

#include "arch/generic.h"
#include "lock_free_queue_variant.h"

#include <map>
#include <vector>
#include <string>
#include <utility>
#include <algorithm>

namespace adk_impl
{

namespace variant
{

class MemoryAllocator;

/**
 * std::map<block_size, std::pair<block_num, pool_namme>>
 */
using PoolProperty = std::map<uint32_t, std::pair<uint32_t, std::string>>;

class MemoryBlock
{
public:
    void set_block_ctx(void* block_ctx)
    {
        block_ctx_ = reinterpret_cast<int64_t>(block_ctx);
    }

    void set_block_ctx(int64_t block_ctx)
    {
        block_ctx_ = block_ctx;
    }

    int64_t block_ctx() const
    {
        return block_ctx_;
    }

    char* buffer() const
    {
        return ((char*)this) + ADK_OFFSET_OF(MemoryBlock, buffer_);
    }

    const char* const_buffer() const
    {
        return buffer_;
    }

    static MemoryBlock* block(void* buf)
    {
        return ADK_CONTAINER_OF(buf, MemoryBlock, buffer_);
    }

    static constexpr size_t reserve_size()
    {
        return ADK_OFFSET_OF(MemoryBlock, buffer_);
    }

private:
    int64_t block_ctx_;
    char    buffer_[];
};

class MemoryAllocator
{
public:
    MemoryAllocator()
    {
        ///> 128 * 1024 default glibc mmap_threshold
        incre_size_ = 128 * 1024;
        node_lock_ = 0;
        memory_chunk_ = nullptr;
    }

    ~MemoryAllocator()
    {
        if (nullptr != memory_chunk_)
        {
            if (1 == __sync_fetch_and_sub(&(memory_chunk_->ref), 1))
            {
                DestroyChunk(memory_chunk_);
            }
        }
    }

    void Init(uint32_t batching_incre = 128 * 1024)
    {
        if (nullptr == memory_chunk_)
        {
            incre_size_ = decltype(incre_size_)(ADK_ROUND_UP(std::max<uint32_t>(incre_size_, batching_incre), ADK_PAGE_SIZE));
            memory_chunk_ = MemoryAllocator::CreateChunk(incre_size_);
        }
    }

    template<bool kThreadSafe = false>
    inline void* NewMemory(uint32_t len)
    {
        const auto allocate_len = ADK_ROUND_UP(MemoryBlock::reserve_size() + len, sizeof(nullptr));
        if (kThreadSafe)
        {
        retry_lock_r:
            if (ADK_UNLIKELY(!try_lock_r()))
            {
                goto retry_lock_r;
            }

        rlocked_retry_new:
            auto* const memory_chunk = ACCESS_ONCE(memory_chunk_);
            auto memory_cur = ACCESS_ONCE(memory_chunk->cur);
            if (memory_cur + allocate_len <= memory_chunk->thrd)
            {
                auto memory_cur = __sync_fetch_and_add(&(memory_chunk->cur), allocate_len);
                if (memory_cur + allocate_len <= memory_chunk->thrd)
                {
                    __sync_fetch_and_add(&memory_chunk->ref, 1);

                    /**
                     * unlock read lock with reference counter
                     */
                    unlock_r();

                    MemoryBlock* const memory_block = (MemoryBlock*)(((char*)memory_chunk) + memory_cur);
                    memory_block->set_block_ctx(-reinterpret_cast<int64_t>(memory_chunk));

                    ADK_BARRIER();
                    return memory_block->buffer();
                }
            }

            if (ADK_UNLIKELY(!try_lock_w(1)))
            {
                unlock_r();
                if (ADK_UNLIKELY(!try_lock_w()))
                {
                    goto retry_lock_r;
                }

                /**
                 * under write locked check chunk changed by another thread
                 * 
                 * if chunk changed, retry new with read locked
                 */
                if (ADK_UNLIKELY(memory_chunk != ACCESS_ONCE(memory_chunk_)))
                {
                    wlock_decay_r();
                    goto rlocked_retry_new;
                }
            }

            if (ADK_UNLIKELY(allocate_len + sizeof(struct MemoryChunk) > incre_size_))
            {
                memory_chunk_ = CreateChunk(ADK_ROUND_UP(allocate_len + sizeof(struct MemoryChunk), ADK_PAGE_SIZE));
            }
            else
            {
                memory_chunk_ = CreateChunk(incre_size_);
            }

            unlock_w();

            if (1 == __sync_fetch_and_sub(&(memory_chunk->ref), 1))
            {
                DestroyChunk(memory_chunk);
            }

            goto retry_lock_r;
        }
        else
        {
        retry_new:
            const auto offset = memory_chunk_->cur;

            memory_chunk_->cur += allocate_len;
            if (memory_chunk_->cur <= memory_chunk_->thrd)
            {
                __sync_fetch_and_add(&memory_chunk_->ref, 1);
                MemoryBlock* const memory_block = (MemoryBlock*)(((char*)memory_chunk_) + offset);
                memory_block->set_block_ctx(-reinterpret_cast<int64_t>(memory_chunk_));

                ADK_BARRIER();
                return memory_block->buffer();
            }

            if ((1 == memory_chunk_->ref) || (1 == __sync_fetch_and_sub(&memory_chunk_->ref, 1)))
            {
                DestroyChunk(memory_chunk_);
            }

            if (ADK_UNLIKELY(sizeof(struct MemoryChunk) + allocate_len > incre_size_))
            {
                memory_chunk_ = CreateChunk(ADK_ROUND_UP(allocate_len + sizeof(struct MemoryChunk), ADK_PAGE_SIZE));
            }
            else
            {
                memory_chunk_ = CreateChunk(incre_size_);
            }

            goto retry_new;
        }
    }

    static void* NewRawMemory(uint32_t len)
    {
        const auto allocate_len = sizeof(ref_type) + MemoryBlock::reserve_size() + len;
        char* const raw_memory = new char[allocate_len];

        *((ref_type*)raw_memory) = 1;
        MemoryBlock* const memory_block = (MemoryBlock*)(raw_memory + sizeof(ref_type));
        memory_block->set_block_ctx(-reinterpret_cast<int64_t>(raw_memory));

        ADK_BARRIER();
        return memory_block->buffer();
    }

    static void DeleteMemory(void* buffer)
    {
        DeleteBlock(MemoryBlock::block(buffer));
    }

    static void DeleteBlock(MemoryBlock* memory_block)
    {
        assert(memory_block);

        const auto block_ctx = memory_block->block_ctx();
        assert(block_ctx < 0);

        auto* const memory_chunk = reinterpret_cast<struct MemoryChunk*>(-block_ctx);
        if (1 == __sync_fetch_and_sub(&(memory_chunk->ref), 1))
        {
            DestroyChunk(memory_chunk);
        }
    }

private:
    typedef int32_t ref_type;

    struct MemoryChunk
    {
        ref_type ref;
        uint32_t cur;
        uint32_t thrd;
        char     memory[];
    };

    static MemoryChunk* CreateChunk(uint32_t memory_size)
    {
        struct MemoryChunk* const memory_chunk = (struct MemoryChunk*)(new char[memory_size]);
        memory_chunk->ref = 1;
        memory_chunk->cur = ADK_OFFSET_OF(struct MemoryChunk, memory);
        memory_chunk->thrd = memory_size;
        memset(memory_chunk->memory, 0, memory_size - ADK_OFFSET_OF(struct MemoryChunk, memory));
        return memory_chunk;
    }

    static void DestroyChunk(struct MemoryChunk* memory_chunk)
    {
        delete[]((char*)memory_chunk);
    }

    inline bool try_lock_r()
    {
        const auto node_lock = ACCESS_ONCE(node_lock_);
        if (node_lock >= 0)
        {
            return __sync_bool_compare_and_swap(&node_lock_, node_lock, node_lock + 1);
        }

        return false;
    }

    inline void unlock_r()
    {
        __sync_fetch_and_sub(&node_lock_, 1);
    }

    inline bool try_lock_w(int32_t exp_lock = 0)
    {
        return __sync_bool_compare_and_swap(&node_lock_, exp_lock, -1);
    }

    /**
     * write lock directly change to read lock
     */
    inline void wlock_decay_r()
    {
        assert(-1 == node_lock_);
        node_lock_ = 1;
    }

    inline void unlock_w()
    {
        ADK_BARRIER();
        node_lock_ = 0;
    }

    uint32_t            incre_size_;

    int32_t             node_lock_;
    struct MemoryChunk* memory_chunk_;
};

template<ADK_TTPARAM(CacheQueue)>
class MemoryPool
{
public:
    using MemoryPoolNode = CacheQueue<void*>;

    struct MemoryNode
    {
        void*           memory_header;
        uint32_t        memory_block_num;
        MemoryPoolNode* memory_pool_node;
        MemoryAllocator memory_allocator;
    };

    // min size should be a power of 2, other size should be integral multiple of min size
    static MemoryPool* Create(const PoolProperty& memory_pool_property)
    {
        if (ADK_UNLIKELY(0 == memory_pool_property.size()))
        {
            return nullptr;
        }

        MemoryPool* memory_pool = new MemoryPool;
        for (const auto& pool_property : memory_pool_property)
        {
            const auto  mem_block_size = pool_property.first + MemoryBlock::reserve_size();
            const auto  mem_block_num = pool_property.second.first;
            const auto& pool_name = pool_property.second.second;

            const uint32_t total_size = ADK_ROUND_UP(mem_block_size * mem_block_num, ADK_PAGE_SIZE);
            const uint32_t valid_block_num = total_size / mem_block_size;

            auto& memory_node = memory_pool->memory_pool_map_[mem_block_size];

            memory_node.memory_header = aligned_malloc(ADK_PAGE_SIZE, (size_t)total_size);
            assert(memory_node.memory_header);

            memset(memory_node.memory_header, 0, total_size);

            memory_node.memory_block_num = valid_block_num;

            memory_node.memory_pool_node = MemoryPoolNode::Create(pool_name, valid_block_num);
            assert(memory_node.memory_pool_node);

            char* temp_block = (char*)memory_node.memory_header;
            for (uint32_t index = 0; index < valid_block_num; ++index)
            {
                ((MemoryBlock*)temp_block)->set_block_ctx(memory_node.memory_pool_node);
                ADK_UNUSED const auto ec = memory_node.memory_pool_node->Push(((MemoryBlock*)temp_block)->buffer());
                assert(ErrorCode::kSuccess == ec);

                temp_block += mem_block_size;
            }

            memory_node.memory_allocator.Init(mem_block_size << 3);
        }

        const uint32_t min_size = memory_pool_property.begin()->first;
        const uint32_t max_size = memory_pool_property.rbegin()->first;
        if (kSuccess != memory_pool->MemorySizeMapping(min_size, max_size))
        {
            Destroy(memory_pool);
            return nullptr;
        }

        memory_pool->default_allocator_.Init(max_size << 3);
        return memory_pool;
    }

    static void Destroy(MemoryPool* memory_pool)
    {
        if (NULL != memory_pool)
        {
            if (nullptr != memory_pool->pool_node_mapping_)
            {
                delete[] memory_pool->pool_node_mapping_;
                memory_pool->pool_node_mapping_ = nullptr;
            }

            for (auto& memory_node : memory_pool->memory_pool_map_)
            {
                assert(memory_node.second.memory_header);
                aligned_free(memory_node.second.memory_header);

                assert(memory_node.second.memory_pool_node);
                MemoryPoolNode::Delete(memory_node.second.memory_pool_node);
            }

            memory_pool->memory_pool_map_.clear();
            delete memory_pool;
        }
    }

    static void Delete(MemoryPool* memory_pool)
    {
        Destroy(memory_pool);
    }

    template<bool kBlock = false>
    void* NewMemory(size_t length)
    {
        assert(length > 0);
        const auto pool_index = (length - 1) >> block_bits_;
        if (pool_index < pool_node_num_)
        {
            void* buffer;
            auto* const memory_node = pool_node_mapping_[pool_index];
            if (kBlock)
            {
                if (kSuccess == memory_node->memory_pool_node->Pop(buffer))
                {
                    return buffer;
                }
            }
            else
            {
                if (kSuccess == memory_node->memory_pool_node->TryPop(buffer))
                {
                    return buffer;
                }
            }

            if (MemoryPoolNode::kSingleConsumer)
            {
                return memory_node->memory_allocator.template NewMemory<false>(length);
            }

            return memory_node->memory_allocator.template NewMemory<true>(length);
        }

        if (MemoryPoolNode::kSingleConsumer)
        {
            return default_allocator_.template NewMemory<false>(length);
        }

        return default_allocator_.template NewMemory<true>(length);
    }

    static void DeleteMemory(void* buffer)
    {
        auto* const memory_block = MemoryBlock::block(buffer);
        const auto block_ctx = memory_block->block_ctx();
        if (block_ctx > 0)
        {
        retry:
            const auto ec = reinterpret_cast<MemoryPoolNode*>(block_ctx)->Push(buffer);
            if (ADK_UNLIKELY(ErrorCode::kSuccess != ec))
            {
                ADK_PAUSE();
                goto retry;
            }
        }
        else
        {
            MemoryAllocator::DeleteBlock(memory_block);
        }
    }

protected:
    MemoryPool()
    {
        block_bits_ = 0;
        pool_node_num_ = 0;
        pool_node_mapping_ = nullptr;
    }

    int32_t MemorySizeMapping(uint32_t min_size, uint32_t max_size)
    {
        block_bits_ = GetBits(min_size);

        // max_size should be a multiple of min_size, otherwise the max_size blocks will not be used.
        if (((1 << block_bits_) - 1) & max_size)
        {
            return ErrorCode::kInvalidParameters;
        }

        pool_node_num_ = max_size >> block_bits_;
        pool_node_mapping_ = new MemoryNode*[pool_node_num_];

        auto iter = memory_pool_map_.begin();
        for (uint32_t pool_index = 0; pool_index < pool_node_num_; ++pool_index)
        {
            // Calc segment<pool_index> block max_size;
            const uint32_t block_size = (pool_index + 1) << block_bits_;

            // traverse ordered map from small to big
            while (iter != memory_pool_map_.end())
            {
                if (block_size <= iter->first)
                {
                    break;
                }
                ++iter;
            }

            // can not find suitable size pool node
            if (memory_pool_map_.end() == iter)
            {
                return ErrorCode::kFailure;
            }

            pool_node_mapping_[pool_index] = &(iter->second);
        }

        return kSuccess;
    }

private:
    uint32_t        block_bits_;
    uint32_t        pool_node_num_;
    MemoryNode**    pool_node_mapping_;
    MemoryAllocator default_allocator_;
    std::map<uint32_t, MemoryNode> memory_pool_map_;
};

}

}

#endif
