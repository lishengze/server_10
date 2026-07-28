#define BOOST_TEST_MODULE memory_pool_variant
#include <boost/test/included/unit_test.hpp>

#include <map>
#include <atomic>
#include <vector>
#include <thread>
#include <iostream>

#include <stdlib.h>
#include <adk_pack/memory_pool_variant.h>
#include <adk_pack/lock_free_queue_variant.h>

volatile bool g_has_error = false;
volatile bool g_test_running = false;

enum MemBlockSize
{
    kMemBlockSize2K = 2048,
    kMemBlockSize8K = 8192,
    kMemBlockSize64K = 65536,
    kMemBlockSize1M = 1048576
};

enum MemBlockNum
{
    kMemBlockNum2K = 512,
    kMemBlockNum8K = 256,
    kMemBlockNum64K = 128,
    kMemBlockNum1M = 64,
};

const std::map<uint32_t, std::pair<uint32_t, std::string>> kMemoryPoolProperty =
{
    { kMemBlockSize2K,  std::make_pair(kMemBlockNum2K,  "MemoryPool2K") },
    { kMemBlockSize8K,  std::make_pair(kMemBlockNum8K,  "MemoryPool8K") },
    { kMemBlockSize64K, std::make_pair(kMemBlockNum64K, "MemoryPool64K") },
    { kMemBlockSize1M,  std::make_pair(kMemBlockNum1M,  "MemoryPool1M") }
};


struct Element
{
    uint64_t cnt;
    char data[];
};

const uint32_t kReservedSize = 64; 

using Queue = adk::variant::MPMCQueue<Element*>;
using adk::variant::SPSCQueue;
using adk::variant::SPMCQueue;
using adk::variant::MPSCQueue;
using adk::variant::MPMCQueue;
using adk::variant::MemoryPool;

constexpr int32_t kTestLoopCount = 5000000;     // 500w

// Consumer : 从内存池中申请内存，放入队列
template<template<typename> class CacheQueue>
void Consumer(MemoryPool<CacheQueue>* memory_pool, Queue* queue, std::atomic<int64_t>* test_count)
{
    uint64_t i = 1;

    while (g_test_running == false)
    {
        usleep(0);
    }
    
    while (test_count->load(std::memory_order_acquire) > 0) 
    {
        // 0x1FFFFF + kReservedSize 小于 kMemBlockSize1M
        void *const memory = memory_pool->NewMemory((random() & 0x1FFFFF) + kReservedSize);
        assert(memory);
        Element *elem = (Element *)(memory);
        elem->cnt = i;
        memset(elem->data, (char)(i % 256), kReservedSize - sizeof(Element));
        if (test_count->fetch_sub(1, std::memory_order_acquire) >= 1) 
        {
            queue->Push(elem);
            ++i;
        }
        else
        {
            break;
        }

    }
}

// Producer : 从队列出队，检查内容并释放内存
template<template<typename> class CacheQueue>
void Producer(MemoryPool<CacheQueue>* memory_pool, Queue* queue, std::atomic<int64_t>* test_count)
{
    Element *elem = nullptr;
    while (test_count->load(std::memory_order_acquire) > 0) 
    {
        if (queue->TryPop(elem) != adk::ErrorCode::kSuccess)
        {
            usleep(1000);
            continue;
        }

        if (elem->cnt == 0)
        {
            // 检查是否合法， 若为0表示之前已经释放的内存块
            g_has_error = true;
            std::cout << "element from queue may be deleted." << std::endl;
            abort();
        }

        if ((char)((elem->cnt) % 256) != elem->data[1])
        {
            g_has_error = true;
            std::cout << "element check failed, "
                      << std::to_string((elem->cnt) % 256)
                      << " != " << std::to_string((int)(elem->data[1]))
                      << std::endl;
            abort();
        }

        elem->cnt = 0;  // 标记该内存块被释放
        memory_pool->DeleteMemory((void *)elem);
        --(*test_count);
    }
}


/**
 * @brief 内存池 单线程申请 单线程释放的测试样例
 */
BOOST_AUTO_TEST_CASE(spsc_memory_pool)
{
    constexpr uint32_t kProducerThreadCount = 1;
    constexpr uint32_t kConsumerThreadCount = 1;

    std::vector<std::thread> test_threads;
    std::atomic<int64_t> test_count_p(kTestLoopCount);
    std::atomic<int64_t> test_count_c(kTestLoopCount);

    auto memory_pool = adk::variant::MemoryPool<SPSCQueue>::Create(kMemoryPoolProperty);
    BOOST_REQUIRE(memory_pool);
    g_test_running = false;
    Queue* queue = Queue::Create("mpmc_queue", 1024);

    BOOST_CHECK(test_count_p == test_count_c);
    g_has_error = false;

    for (uint32_t index=0; index < kConsumerThreadCount; ++index)
    {
        test_threads.emplace_back(std::thread(Consumer<SPSCQueue>, memory_pool, queue, &test_count_c));
    }

    for (uint32_t index=0; index < kProducerThreadCount; ++index)
    {
        test_threads.emplace_back(std::thread(Producer<SPSCQueue>, memory_pool, queue, &test_count_p));
    }

    g_test_running = true;

    for (auto& thread : test_threads)
    {
        thread.join();
    }

    BOOST_CHECK(g_has_error == false);
    adk::variant::MemoryPool<SPSCQueue>::Delete(memory_pool);
    Queue::Delete(queue);
    std::cout << "case: spsc_memory_pool success" << std::endl;
}

/**
 * @brief 内存池 单线程申请 多线程释放的测试样例
 */
BOOST_AUTO_TEST_CASE(spmc_memory_pool)
{
    constexpr uint32_t kProducerThreadCount = 1;
    constexpr uint32_t kConsumerThreadCount = 6;

    std::vector<std::thread> test_threads;
    std::atomic<int64_t> test_count_p(kTestLoopCount);
    std::atomic<int64_t> test_count_c(kTestLoopCount);


    auto memory_pool = adk::variant::MemoryPool<SPMCQueue>::Create(kMemoryPoolProperty);
    BOOST_REQUIRE(memory_pool);
    g_test_running = false;
    Queue* queue = Queue::Create("mpmc_queue", 1024);

    BOOST_CHECK(test_count_p == test_count_c);
    g_has_error = false;

    for (uint32_t index=0; index < kConsumerThreadCount; ++index)
    {
        test_threads.emplace_back(std::thread(Consumer<SPMCQueue>, memory_pool, queue, &test_count_c));
    }

    for (uint32_t index = 0; index < kProducerThreadCount; ++index) 
    {
        test_threads.emplace_back(std::thread(Producer<SPMCQueue>, memory_pool, queue, &test_count_p));
    }

    g_test_running = true;

    for (auto& thread : test_threads)
    {
        thread.join();
    }

    BOOST_CHECK(g_has_error == false);
    adk::variant::MemoryPool<SPMCQueue>::Delete(memory_pool);
    Queue::Delete(queue);
    std::cout << "case: spmc_memory_pool success" << std::endl;
}

/**
 * @brief 内存池 多线程申请 单线程释放的测试样例
 */
BOOST_AUTO_TEST_CASE(mpsc_memory_pool)
{
    constexpr uint32_t kProducerThreadCount = 6;
    constexpr uint32_t kConsumerThreadCount = 1;

    std::vector<std::thread> test_threads;
    std::atomic<int64_t> test_count_p(kTestLoopCount);
    std::atomic<int64_t> test_count_c(kTestLoopCount);


    auto memory_pool = adk::variant::MemoryPool<MPSCQueue>::Create(kMemoryPoolProperty);
    BOOST_REQUIRE(memory_pool);
    g_test_running = false;
    Queue* queue = Queue::Create("mpmc_queue", 1024);

    BOOST_CHECK(test_count_p == test_count_c);
    g_has_error = false;

    for (uint32_t index=0; index < kConsumerThreadCount; ++index)
    {
        test_threads.emplace_back(std::thread(Consumer<MPSCQueue>, memory_pool, queue, &test_count_c));
    }

    for (uint32_t index = 0; index < kProducerThreadCount; ++index) 
    {
        test_threads.emplace_back(std::thread(Producer<MPSCQueue>, memory_pool, queue, &test_count_p));
    }

    g_test_running = true;

    for (auto& thread : test_threads)
    {
        thread.join();
    }

    BOOST_CHECK(g_has_error == false);
    adk::variant::MemoryPool<MPSCQueue>::Delete(memory_pool);
    Queue::Delete(queue);
    std::cout << "case: mpsc_memory_pool success" << std::endl;
}

/**
 * @brief 内存池 多线程申请 多线程释放的测试样例
 */
BOOST_AUTO_TEST_CASE(mpmc_memory_pool)
{
    constexpr uint32_t kProducerThreadCount = 4;
    constexpr uint32_t kConsumerThreadCount = 4;

    std::vector<std::thread> test_threads;
    std::atomic<int64_t> test_count_p(kTestLoopCount);
    std::atomic<int64_t> test_count_c(kTestLoopCount);


    auto memory_pool = adk::variant::MemoryPool<MPMCQueue>::Create(kMemoryPoolProperty);
    BOOST_REQUIRE(memory_pool);
    g_test_running = false;
    Queue* queue = Queue::Create("mpmc_queue", 1024);

    BOOST_CHECK(test_count_p == test_count_c);
    g_has_error = false;

    for (uint32_t index=0; index < kConsumerThreadCount; ++index)
    {
        test_threads.emplace_back(std::thread(Consumer<MPMCQueue>, memory_pool, queue, &test_count_c));
    }


    for (uint32_t index = 0; index < kProducerThreadCount; ++index) 
    {
        test_threads.emplace_back(std::thread(Producer<MPMCQueue>, memory_pool, queue, &test_count_p));
    }

    g_test_running = true;

    for (auto& thread : test_threads)
    {
        thread.join();
    }

    BOOST_CHECK(g_has_error == false);
    adk::variant::MemoryPool<MPMCQueue>::Delete(memory_pool);
    Queue::Delete(queue);
    std::cout << "case: mpmc_memory_pool success" << std::endl;
}