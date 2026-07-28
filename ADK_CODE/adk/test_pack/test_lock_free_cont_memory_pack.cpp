#include <thread>
#include <iostream>
#include <adk_pack/error_code.h>
#include <adk_pack/arch/generic.h>
#include <adk_pack/lock_free_cont_memory.h>

using adk::ErrorCode;
using adk::ContEntry;
using adk::ContinueMemory;

uint32_t g_allocate_size = 128;
volatile bool g_is_running = false;

volatile uint64_t* volatile g_producer_nr = nullptr;
volatile uint64_t* volatile g_producer_failed_nr = nullptr;

volatile uint64_t* volatile g_foreach_nr = nullptr;
volatile uint64_t* volatile g_consumer_nr = nullptr;
volatile uint64_t* volatile g_consumer_failed_nr = nullptr;

void Producer(ContinueMemory* continue_memory)
{
    uint64_t producer_counter = 0;
    uint64_t producer_fail_counter = 0;

    g_producer_nr = &producer_counter;
    g_producer_failed_nr = &producer_fail_counter;

    struct ContEntry* entry_ptr = nullptr;
    char* buffer = new char[g_allocate_size];

    while (g_is_running)
    {
        if (ADK_UNLIKELY(ErrorCode::kSuccess != continue_memory->TryAllocEntry(g_allocate_size, &entry_ptr)))
        {
            ++producer_fail_counter;
            for (uint32_t index = 0; index < 8; ++index)
            {
                ADK_PAUSE();
            }
            continue;
        }

        assert(entry_ptr->usage_len() >= g_allocate_size);

        *(uint64_t*)buffer = producer_counter;
        memcpy(entry_ptr->GetBuffer(), buffer, g_allocate_size);
        continue_memory->PostEntry(entry_ptr);
        ++producer_counter;
    }
}

void Consumer(ContinueMemory* continue_memory)
{
    uint64_t foreach_nr = 0;
    uint64_t consumer_record = 0;
    uint64_t consumer_fail_counter = 0;

    g_foreach_nr = &foreach_nr;
    g_consumer_nr = &consumer_record;
    g_consumer_failed_nr = &consumer_fail_counter;

    struct ContEntry* entry_ptr = nullptr;
    char* buffer = new char[g_allocate_size];

    while (g_is_running)
    {
        if (65535 == (65535 & consumer_record))
        {
            auto foreach_consumer_record = consumer_record;
            continue_memory->Foreach([&](char* buffer, uint32_t buffer_size) {
                if (ADK_UNLIKELY(*((uint64_t*)buffer) != foreach_consumer_record++))
                {
                    g_is_running = false;
                    std::cout << "###### Bug on ######" << std::endl;
                    return false;
                }

                return true;
            });

            foreach_nr += foreach_consumer_record - consumer_record;
        }

        if (ADK_UNLIKELY(ErrorCode::kSuccess != continue_memory->TryWaitEntry(&entry_ptr)))
        {
            ++consumer_fail_counter;
            for (uint32_t index = 0; index < 32; ++index)
            {
                ADK_PAUSE();
            }
            continue;
        }

        assert(entry_ptr->usage_len() >= g_allocate_size);
        memcpy(buffer, entry_ptr->GetBuffer(), g_allocate_size);
        if (ADK_UNLIKELY(*((uint64_t*)buffer) != consumer_record++))
        {
            g_is_running = false;
            std::cout << "###### Bug on ######" << std::endl;
        }

        continue_memory->FreeEntry(entry_ptr);
    }
}

int main(int argc, char* argv[])
{
    if (argc > 1)
    {
        g_allocate_size = atoi(argv[1]);
    }

    std::cout << "allocate size = " << g_allocate_size << std::endl;

    ContinueMemory* continue_memory = ContinueMemory::Create(16 * 8192, 64);
    assert(continue_memory);

    g_is_running = true;

    std::thread consumer_thrd = std::thread(Consumer, continue_memory);
    std::thread producer_thrd = std::thread(Producer, continue_memory);

    uint64_t producer_nr = 0;
    uint64_t producer_failed_nr = 0;

    uint64_t foreach_nr = 0;
    uint64_t consumer_nr = 0;
    uint64_t consumer_failed_nr = 0;

    while (g_is_running)
    {
        usleep(0);

        if (nullptr == g_producer_nr)
        {
            continue;
        }

        if (nullptr == g_producer_failed_nr)
        {
            continue;
        }

        if (nullptr == g_foreach_nr)
        {
            continue;
        }

        if (nullptr == g_consumer_nr)
        {
            continue;
        }

        if (nullptr == g_consumer_failed_nr)
        {
            continue;
        }

        break;
    }
    
    while (g_is_running)
    {
        sleep(1);
        const auto temp_producer_nr = *g_producer_nr;
        const auto temp_producer_failed_nr = *g_producer_failed_nr;

        const auto temp_foreach_nr = *g_foreach_nr;
        const auto temp_consumer_nr = *g_consumer_nr;
        const auto temp_consumer_failed_nr = *g_consumer_failed_nr;

        std::cout << "producer_nr = " << temp_producer_nr - producer_nr
                  << ", consumer_nr = " << temp_consumer_nr - consumer_nr
                  << ", producer_failed_nr = " << temp_producer_failed_nr - producer_failed_nr
                  << ", consumer_failed_nr = " << temp_consumer_failed_nr - consumer_failed_nr
                  << ", foreach_nr = " << temp_foreach_nr - foreach_nr
                  << std::endl;

        producer_nr = temp_producer_nr;
        producer_failed_nr = temp_producer_failed_nr;
        foreach_nr = temp_foreach_nr;
        consumer_nr = temp_consumer_nr;
        consumer_failed_nr = temp_consumer_failed_nr;
    }

    consumer_thrd.join();
    producer_thrd.join();

    return 0;
}