#include "test_case.h"

#include <thread>
#include <iostream>

#include <adk/util.h>
#include <adk/lock_free_cont_memory.h>

int main()
{
    auto* const shm_manager = adk::ShmContMemManager::Create("cont_memory_test", 16, 24 * 1024 * 1024);
    if (nullptr == shm_manager)
    {
        std::cout << "Create share memory failed" << std::endl;
        return 0;
    }

    auto* const producer = shm_manager->CreateShmContMemory("channel1", 8 * 1024 * 1024, 1 * 1024 * 1024);
    if (nullptr == producer)
    {
        std::cout << "Create share continue memory channel failed" << std::endl;
        return 0;
    }

    auto* const consumer = shm_manager->AttachShmContMemory("channel1");
    if (nullptr == consumer)
    {
        std::cout << "Attach share continue memory channel failed" << std::endl;
        return 0;
    }

    adk::EnableShareMemoryDump(nullptr);
    constexpr uint32_t kTestBufferSize = 128;
    volatile uint64_t* volatile consumer_nr = nullptr;
    volatile uint64_t* volatile consumer_failed_nr = nullptr;
    std::thread consumer_thrd = std::thread([&]() {
        char payload[kTestBufferSize];
        consumer_nr = reinterpret_cast<uint64_t*>(&payload[0]);

        uint64_t counter = 0;
        consumer_failed_nr = &counter;

        uint64_t expected = 0;
        adk::ContEntry* entry_ptr = nullptr;
        do
        {
            if (ADK_UNLIKELY(adk::ErrorCode::kSuccess != consumer->TryWaitEntry(&entry_ptr)))
            {
                ++counter;
                usleep(0);
                continue;
            }

            if (kTestBufferSize != entry_ptr->app_data_len())
            {
                std::cout << "### BUG ###" << __LINE__ << std::endl;
                abort();
            }

            shm_memory(payload, entry_ptr->GetBuffer(), kTestBufferSize);
            consumer->FreeEntry(entry_ptr);

            if (expected != *consumer_nr)
            {
                std::cout << "### BUG ### value = " << *consumer_nr
                          << ", expected value = " << expected << std::endl;
                abort();
            }

            expected = *consumer_nr + 1;
        } while (true);
    });

    volatile uint64_t* volatile producer_nr = nullptr;
    volatile uint64_t* volatile producer_failed_nr = nullptr;
    std::thread producer_thrd = std::thread([&]() {
        char payload[kTestBufferSize];
        producer_nr = reinterpret_cast<uint64_t*>(&payload[0]);

        uint64_t counter = 0;
        producer_failed_nr = &counter;
        adk::ContEntry* entry_ptr = nullptr;
        do
        {
            if (adk::ErrorCode::kSuccess == producer->TryAllocEntry(kTestBufferSize, &entry_ptr))
            {
                shm_memory(entry_ptr->GetBuffer(), payload, kTestBufferSize);
                producer->PostEntry(entry_ptr, kTestBufferSize);
                ++(*producer_nr);
            }
            else
            {
                ++counter;
                ADK_PAUSE();
            }
        } while (true);
    });

    while (nullptr == consumer_nr);
    while (nullptr == consumer_failed_nr);
    while (nullptr == producer_nr);
    while (nullptr == producer_failed_nr);

    uint64_t consumer1 = 0;
    uint64_t consumer2 = 0;
    uint64_t producer1 = 0;
    uint64_t producer2 = 0;

    while (true)
    {
        sleep(1);

        const auto temp_nr1 = *consumer_nr;
        const auto temp_nr2 = *consumer_failed_nr;
        const auto temp_nr3 = *producer_nr;
        const auto temp_nr4 = *producer_failed_nr;
        std::cout << "consumer_diff = " << temp_nr1 - consumer1
            << ", consumer_failed_diff = " << temp_nr2 - consumer2
            << ", producer_diff = " << temp_nr3 - producer1
            << ", producer_failed_diff = " << temp_nr4 - producer2 << std::endl;

        consumer1 = temp_nr1;
        consumer2 = temp_nr2;
        producer1 = temp_nr3;
        producer2 = temp_nr4;
    }
}