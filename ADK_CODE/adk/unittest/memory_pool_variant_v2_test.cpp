#define BOOST_TEST_MODULE memory_pool_variant
#include <boost/test/included/unit_test.hpp>

#include <map>
#include <vector>
#include <thread>
#include <iostream>

#include <stdlib.h>
#include <adk/memory_pool_variant.h>

enum MemBlockSize
{
    kMemBlockSize2K = 2048,
    kMemBlockSize8K = 8192,
    kMemBlockSize64K = 65536,
    kMemBlockSize1M = 1048576
};

enum MemBlockNum
{
    kMemBlockNum2K = 8192,
    kMemBlockNum8K = 2048,
    kMemBlockNum64K = 1024,
    kMemBlockNum1M = 64,
};

const std::map<uint32_t, std::pair<uint32_t, std::string>> kMemoryPoolProperty =
{
    { kMemBlockSize2K,  std::make_pair(kMemBlockNum2K,  "MemoryPool2K") },
    { kMemBlockSize8K,  std::make_pair(kMemBlockNum8K,  "MemoryPool8K") },
    { kMemBlockSize64K, std::make_pair(kMemBlockNum64K, "MemoryPool64K") },
    { kMemBlockSize1M,  std::make_pair(kMemBlockNum1M,  "MemoryPool1M") }
};

using MemoryPool = adk::variant::MemoryPool<adk::variant::MPMCQueue>;

constexpr int32_t kTestLoopCount = 10000;

void RepeatNewDelete(MemoryPool* memory_pool)
{
    const auto pid = std::this_thread::get_id();
    std::cout << "thread<" << pid << "> start to run loops: " << kTestLoopCount << std::endl;

    constexpr int32_t buffered_size = 0xFF;
    std::vector<void*> buffered_memory;
    buffered_memory.resize(buffered_size);

    for (int32_t index=0; index<kTestLoopCount; ++index)
    {
        const int32_t inner_index = (index & buffered_size);
        if (inner_index < buffered_size)
        {
            void* const memory = memory_pool->NewMemory((random() & 0x1FFFFF) + 1);
            assert(memory);
            buffered_memory[inner_index] = memory;
        }
        else
        {
            for (auto iter = buffered_memory.begin(); iter != buffered_memory.end(); ++iter)
            {
                assert(*iter);
                memory_pool->DeleteMemory(*iter);
                *iter = nullptr;
            }
        }    
    }

    for (auto iter = buffered_memory.begin(); iter != buffered_memory.end(); ++iter)
    {
        if (nullptr != *iter)
        {
            memory_pool->DeleteMemory(*iter);
        }
    }

    std::cout << "thread<" << pid << "> exit" << std::endl;
}


BOOST_AUTO_TEST_CASE(memory_pool_new_delete)
{
    constexpr uint32_t kTestThreadCount = 8;
    std::thread test_thread[kTestThreadCount];

    MemoryPool* memory_pool = MemoryPool::Create(kMemoryPoolProperty);
    BOOST_CHECK(memory_pool);

    for (uint32_t index=0; index<kTestThreadCount; ++index)
    {
        test_thread[index] = std::thread(RepeatNewDelete, memory_pool);
    }

    for (uint32_t index=0; index<kTestThreadCount; ++index)
    {
        test_thread[index].join();
    }

    MemoryPool::Delete(memory_pool);
}