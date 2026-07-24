
#include <map>
#include <set>
#include <utility>
#include <iostream>

#include <adk/memory_pool_variant.h>

enum MemBlockSize
{
    kMemBlockSize1K = 1024,
    kMemBlockSize2K = 2096,
    kMemBlockSize8K = 8192,
    kMemBlockSizeUp = 65536
};

enum MemBlockNum
{
    kMemBlockNum1K = 8192,
    kMemBlockNum2K = 4096,
    kMemBlockNum8K = 1024,
    kMemBlockNumUp = 256
};

const std::map<uint32_t, std::pair<uint32_t, std::string>> kMemoryPoolProperty = 
{
    { kMemBlockSize1K, std::make_pair(kMemBlockNum1K, "MemoryPool1K") },
    { kMemBlockSize2K, std::make_pair(kMemBlockNum2K, "MemoryPool2K") },
    { kMemBlockSize8K, std::make_pair(kMemBlockNum8K, "MemoryPool8K") },
    { kMemBlockSizeUp, std::make_pair(kMemBlockNumUp, "MemoryPoolUp") }
};

using MemoryPool = adk::variant::MemoryPool<adk::variant::SPSCQueue>;

int main()
{
    MemoryPool* memory_pool = MemoryPool::Create(kMemoryPoolProperty);
    if (NULL == memory_pool)
    {
        std::cout << "Create memory pool failed" << std::endl;
    }

    std::map<uint32_t, std::set<uint32_t>> indicator;
    for (uint32_t index=1; index<100000000; ++index)
    {
        void* buffer = memory_pool->NewMemory(index);
        if (NULL != buffer)
        {
            adk::variant::MemoryBlock* block = adk::variant::MemoryBlock::block(buffer);
            indicator[block->block_ctx()].insert(index);
            memory_pool->DeleteMemory(buffer);
        }
    }

    for (auto iter=indicator.begin(); iter != indicator.end(); ++iter)
    {
        const uint32_t block_ctx = iter->first;
        const uint32_t block_num = iter->second.size();
        const uint32_t min_size  = *(iter->second.begin());
        const uint32_t last_size = *(iter->second.rbegin());
        
        std::cout << "block_ctx = " << block_ctx
                  << ", block_num = " << block_num
                  << ", min_size = "  << min_size
                  << ", last_size = " << last_size
                  << std::endl;
    }

    MemoryPool::Delete(memory_pool);
    return 0;
}