#include "message_pool.h"

#include <adk/util.h>
#include <adk/shm_ptr.h>

namespace adk_impl
{

namespace io_engine
{

RxMemoryPool* RxMemoryPool::Create(uint32_t block_size, uint32_t block_num)
{
    const auto mem_block_size = ADK_ROUND_UP(block_size + variant::MemoryBlock::reserve_size(),
                                             sizeof(nullptr));
    const auto total_size = ADK_ROUND_UP(mem_block_size * block_num, ADK_PAGE_SIZE);
    const auto effective_block_num = total_size / mem_block_size;

    RxMemoryPool* memory_pool = (RxMemoryPool*)aligned_malloc(ADK_CACHE_LINE_SIZE, sizeof(RxMemoryPool));
    new ((void*)memory_pool) RxMemoryPool();

    memory_pool->block_size_ = mem_block_size - variant::MemoryBlock::reserve_size();
    memory_pool->memory_header_ = aligned_malloc(ADK_PAGE_SIZE, (size_t)total_size);
    memory_pool->default_allocator_.Init(mem_block_size << 3);

    assert(memory_pool->memory_header_);
    memory_pool->memory_cache_queue_ = CacheQueueType::Create("TCP-E-RX_POOL", effective_block_num);
    assert(memory_pool->memory_cache_queue_);

    auto* memory_header = memory_pool->memory_header_;
    auto* const memory_cache_queue = memory_pool->memory_cache_queue_;
    for (uint32_t index = 0; index < effective_block_num; ++index)
    {
        ((variant::MemoryBlock*)memory_header)->set_block_ctx(memory_cache_queue);
        ADK_ASSERT_SUCCESS(memory_cache_queue->Push(((variant::MemoryBlock*)memory_header)->buffer()));
        memory_header = ptr_add(memory_header, mem_block_size);
    }

    return memory_pool;
}

void RxMemoryPool::Destroy(RxMemoryPool* memory_pool)
{
    if (nullptr != memory_pool)
    {
        assert(memory_pool->memory_cache_queue_);
        CacheQueueType::Delete(memory_pool->memory_cache_queue_);

        assert(memory_pool->memory_header_);
        aligned_free(memory_pool->memory_header_);

        memory_pool->~RxMemoryPool();
        aligned_free(memory_pool);
    }
}

using PoolProperty = std::map<uint32_t, std::pair<uint32_t, std::string>>;
const PoolProperty kTcpTxMemoryPoolProperty =
{
    { 1024,  std::make_pair(16384, "MemoryPool1K") },
    { 2048,  std::make_pair(1024,  "MemoryPool2K") },
    { 8192,  std::make_pair(512,   "MemoryPool8K") },
    { 65536, std::make_pair(256,   "MemoryPool64K") }
};

TxMessagePool::TxMessagePool()
{
    tx_memory_pool_ = TxMemoryPool::Create(kTcpTxMemoryPoolProperty);
    assert(tx_memory_pool_);
}

TxMessagePool::~TxMessagePool()
{
    assert(tx_memory_pool_);
    TxMemoryPool::Destroy(tx_memory_pool_);
}

RxMessagePool::RxMessagePool()
{
    rx_memory_pool_ = nullptr;
}

RxMessagePool::~RxMessagePool()
{
    assert(rx_memory_pool_);
    RxMemoryPool::Destroy(rx_memory_pool_);
}

void RxMessagePool::Init(uint32_t block_size, uint32_t block_num)
{
    if (nullptr != rx_memory_pool_)
    {
        RxMemoryPool::Destroy(rx_memory_pool_);
    }

    rx_memory_pool_ = RxMemoryPool::Create(MessageImpl::PrefixSize() + block_size, block_num);
    assert(rx_memory_pool_);
}

}

}