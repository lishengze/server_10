#define BOOST_TEST_MODULE memory_pool_variant
#include <boost/test/included/unit_test.hpp>

#include <stdlib.h>

#include <vector>
#include <thread>
#include <iostream>

#include <adk/memory_pool_variant.h>
#include <adk/lock_free_queue_variant.h>

/**
 * memory layout
 * |<----          memory_len         ---->|
 * |<- 8bytes ->|               |<-4bytes->|
 *  memory_index                 memory_len              
 */
constexpr uint32_t kMinMemoryAllocateSize = sizeof(uint64_t) + sizeof(uint32_t);
struct MemoryTest
{
    uint64_t memory_index;
    void*    memory_buffer;
    uint32_t memory_len;

    /**
     * set index and len in memory layout
     */
    void SetValid(uint64_t index, void* buffer, uint32_t len)
    {
        memory_index = index;
        memory_buffer = buffer;
        memory_len = len;
        *((uint64_t*)buffer) = index;
        *(uint32_t*)(((char*)buffer) + len - sizeof(uint32_t)) = len;
    }

    /**
     * check value in memory 
     */
    bool CheckValid()
    {
        if (memory_len < kMinMemoryAllocateSize)
        {
            std::cout << "memory_len = " << memory_len << std::endl;
            return false;
        }

        if (*((uint64_t*)memory_buffer) != memory_index)
        {
            std::cout << "memory_index = " << memory_index 
                      << ", *((uint64_t*)memory_buffer) = " 
                      << *((uint64_t*)memory_buffer) << std::endl;
            return false;
        }

        if (memory_len != *(uint32_t*)(((char*)memory_buffer) + memory_len - sizeof(uint32_t)))
        {
            std::cout << "memory_len = " << memory_len 
                      << ", *(uint32_t*)(((char*)memory_buffer) + memory_len - sizeof(uint32_t)) = " 
                      << *(uint32_t*)(((char*)memory_buffer) + memory_len - sizeof(uint32_t)) << std::endl;
            return false;
        }

        return true;
    }
};

/**
 * main thread -> new memory thread
 *  -> new memory<thread safe = false>
 *  -> set memroy layout
 *  -> push memory into spsc queue
 *
 * delete thread
 *  -> pop memory
 *  -> check memory value
 */
BOOST_AUTO_TEST_CASE(memory_allocator)
{
    adk::variant::MemoryAllocator memory_allocator;
    memory_allocator.Init();

    adk::variant::VariantEntry* entry_ptr = nullptr;
    auto* const memory_queue = adk::variant::SPSCQueue<struct MemoryTest>::Create("memory test", 8192);

    uint64_t memory_index = 0;
    while (memory_index < 8192)
    {
        BOOST_REQUIRE(adk::ErrorCode::kSuccess == memory_queue->AllocEntry(&entry_ptr));
        char* const buffer = entry_ptr->buffer;
        
        const auto rand_len = ((uint32_t)rand() % 1024) + kMinMemoryAllocateSize;
        void* const memory_buffer = memory_allocator.NewMemory(rand_len);
        BOOST_REQUIRE(memory_buffer);

        ((struct MemoryTest*)buffer)->SetValid(memory_index, memory_buffer, rand_len);
        memory_queue->PostEntry(entry_ptr);

        ++memory_index;
    }

    volatile bool is_running = true;
    std::thread delete_thd = std::thread([&]() {
        adk::variant::VariantEntry* del_entry_ptr = nullptr;
        while (is_running)
        {
            if (adk::ErrorCode::kSuccess != memory_queue->WaitEntry(&del_entry_ptr))
            {
                continue;
            }

            char* const buffer = del_entry_ptr->buffer;
            BOOST_ASSERT(((struct MemoryTest*)buffer)->CheckValid());
            adk::variant::MemoryAllocator::DeleteMemory(((struct MemoryTest*)buffer)->memory_buffer);
            memory_queue->FreeEntry(del_entry_ptr);
        }
    });

    while (memory_index < 1000000)
    {
        if (adk::ErrorCode::kSuccess != memory_queue->AllocEntry(&entry_ptr))
        {
            continue;
        }

        char* const buffer = entry_ptr->buffer;
        const auto rand_len = ((uint32_t)rand() % (1024 * 1024)) + kMinMemoryAllocateSize;
        void* const memory_buffer = memory_allocator.NewMemory(rand_len);
        BOOST_ASSERT(memory_buffer);

        ((struct MemoryTest*)buffer)->SetValid(memory_index, memory_buffer, rand_len);
        memory_queue->PostEntry(entry_ptr);

        ++memory_index;
    }

    is_running = false;
    delete_thd.join();

    while (adk::ErrorCode::kSuccess == memory_queue->WaitEntry(&entry_ptr))
    {
        char* const buffer = entry_ptr->buffer;
        BOOST_REQUIRE(((struct MemoryTest*)buffer)->CheckValid());
        adk::variant::MemoryAllocator::DeleteMemory(((struct MemoryTest*)buffer)->memory_buffer);
        memory_queue->FreeEntry(entry_ptr);
    }

    adk::variant::SPSCQueue<struct MemoryTest>::Delete(memory_queue);
}

/**
 * new memory thread 4
 *  -> new memory<thread safe = true>
 *  -> set memroy layout
 *  -> push memory into MPMC queue
 *
 * delete thread 4
 *  -> pop memory
 *  -> check memory value
 */
BOOST_AUTO_TEST_CASE(memory_allocator_thread_safe)
{
    adk::variant::MemoryAllocator memory_allocator;
    memory_allocator.Init();

    auto* const memory_queue = adk::variant::MPMCQueue<struct MemoryTest>::Create("memory test", 8192);

    volatile bool is_running = true;
    std::vector<std::thread> new_thds_vec;
    for (size_t i = 0; i < 4; i++)
    {
        new_thds_vec.push_back(std::thread([&]() {
            adk::variant::VariantEntry* entry_ptr = nullptr;
            for (uint32_t index = 0; index < 100000; ++index)
            {
                if (adk::ErrorCode::kSuccess != memory_queue->AllocEntry(&entry_ptr))
                {
                    continue;
                }

                char* const buffer = entry_ptr->buffer;
                const auto rand_len = ((uint32_t)rand() % (1024 * 1024)) + kMinMemoryAllocateSize;
                void* const memory_buffer = memory_allocator.NewMemory<true>(rand_len);
                //BOOST_REQUIRE(memory_buffer);
                BOOST_ASSERT(memory_buffer);
                ((struct MemoryTest*)buffer)->SetValid(index, memory_buffer, rand_len);
                memory_queue->PostEntry(entry_ptr);
            }
        }));
    }

    std::vector<std::thread> del_thds_vec;
    for (size_t i = 0; i < 4; i++)
    {
        del_thds_vec.push_back(std::thread([&]() {
            adk::variant::VariantEntry* entry_ptr = nullptr;
            while (is_running)
            {
                if (adk::ErrorCode::kSuccess != memory_queue->TryWaitEntry(&entry_ptr))
                {
                    continue;
                }

                char* const buffer = entry_ptr->buffer;
                //BOOST_REQUIRE(((struct MemoryTest*)buffer)->CheckValid());
                BOOST_ASSERT(((struct MemoryTest*)buffer)->CheckValid());
                adk::variant::MemoryAllocator::DeleteMemory(((struct MemoryTest*)buffer)->memory_buffer);
                memory_queue->FreeEntry(entry_ptr);
            }
        }));
    }

    for (auto& new_thd : new_thds_vec)
    {
        new_thd.join();
    }

    is_running = false;
    for (auto& del_thd : del_thds_vec)
    {
        del_thd.join();
    }

    adk::variant::VariantEntry* entry_ptr = nullptr;
    while (adk::ErrorCode::kSuccess == memory_queue->TryWaitEntry(&entry_ptr))
    {
        char* const buffer = entry_ptr->buffer;
        //BOOST_REQUIRE(((struct MemoryTest*)buffer)->CheckValid());
        BOOST_ASSERT(((struct MemoryTest*)buffer)->CheckValid());
        adk::variant::MemoryAllocator::DeleteMemory(((struct MemoryTest*)buffer)->memory_buffer);
        memory_queue->FreeEntry(entry_ptr);
    }

    adk::variant::MPMCQueue<struct MemoryTest>::Delete(memory_queue);
}