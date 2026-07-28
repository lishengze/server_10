#include <thread>
#include <iostream>

#include <adk/lock_free_cont_memory.h>
#include <adk/random.h>

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
volatile uint32_t g_max_qlen = 0;

void UpdateMaxQlen()
{
    uint32_t current_qlen = *g_producer_nr - *g_consumer_nr;
    if (g_max_qlen < current_qlen)
    {
        g_max_qlen = current_qlen;
    }
}

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
            uint32_t index_max = adk::Random(0, 8);
            for (uint32_t index = 0; index < index_max; ++index)
            {
                ADK_PAUSE();
            }
            continue;
        }

        // 测 max_qlen指标时，可以打开
        uint32_t index_max = adk::Random(0, 8);
        for (uint32_t index = 0; index < index_max; ++index)
        {
            ADK_PAUSE();
        }

        assert(entry_ptr->len >= g_allocate_size);

        *(uint64_t*)buffer = producer_counter;
        memcpy(entry_ptr->GetBuffer(), buffer, g_allocate_size);
        ++producer_counter;
        UpdateMaxQlen();
        continue_memory->PostEntry(entry_ptr);
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
            uint32_t index_max = adk::Random(0, 8);
            for (uint32_t index = 0; index < index_max; ++index)
            {
                ADK_PAUSE();
            }
            continue;
        }

        assert(entry_ptr->len >= g_allocate_size);
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

    ContinueMemory* continue_memory = ContinueMemory::Create(64 * 8192, 64);
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
                  << ", max_qlen = " << g_max_qlen 
                  << std::endl;
        g_max_qlen = 0;

        std::cout << "snapshot: " << continue_memory->CollectIndicator() << std::endl;

        producer_nr = temp_producer_nr;
        producer_failed_nr = temp_producer_failed_nr;
        foreach_nr = temp_foreach_nr;
        consumer_nr = temp_consumer_nr;
        consumer_failed_nr = temp_consumer_failed_nr;
        std::cout << continue_memory->CollectIndicator() << std::endl;
    }

    consumer_thrd.join();
    producer_thrd.join();

    return 0;
}
