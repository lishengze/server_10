#include <thread>
#include <iostream>

#include <adk/lock_free_cont_memory.h>

using adk::ErrorCode;
using adk::ContEntry;
using adk::ContinueMemory;
using adk::ShmContMemManager;

uint64_t g_test_counter1 = 0;
uint64_t g_test_counter2 = 0;
uint32_t g_allocate_size = 128;
volatile bool g_is_running = false;

void Producer(ContinueMemory* continue_memory)
{
    uint64_t producer_counter = 0;
    uint64_t producer_fail_counter = 0;
    struct ContEntry* entry_ptr = nullptr;

    uint64_t backoff = 8;
    while (g_is_running)
    {
        if (ADK_UNLIKELY(ErrorCode::kSuccess != continue_memory->TryAllocEntry(g_allocate_size, &entry_ptr)))
        {
            if (ADK_UNLIKELY(++producer_fail_counter & 0x800))
            {
                g_test_counter2 = producer_fail_counter;
            }
            else
            {
                for (uint32_t i = 0; i < backoff; ++i)
                {
                    ADK_PAUSE();
                }
                //backoff <<= 1;
            }
            continue;
        }

        //backoff = 1;
        assert(entry_ptr->len >= g_allocate_size);
        *((uint64_t*)entry_ptr->GetBuffer()) = producer_counter;
        continue_memory->PostEntry(entry_ptr);

        if (ADK_UNLIKELY(++producer_counter & 0x8000))
        {
            g_test_counter1 = producer_counter;
        }
    }
}

int main()
{
    ShmContMemManager* shm_manager = ShmContMemManager::Create("cont_memory_test", 16, 24 * 1024 * 1024);
    if (nullptr == shm_manager)
    {
        std::cout << "Create share memory failed" << std::endl;
        return 0;
    }

    ContinueMemory* cont_memroy = shm_manager->CreateShmContMemory("channel1", 8 * 1024 * 1024, 1 * 1024 * 1024);
    if (nullptr == cont_memroy)
    {
        std::cout << "Create share continue memory channel failed" << std::endl;
        return 0;
    }

    g_is_running = true;
    std::thread producer_thrd = std::thread(Producer, cont_memroy);

    uint64_t print_counter1 = 0;
    uint64_t print_counter2 = 0;
    while (g_is_running)
    {
        sleep(1);
        const auto temp_counter1 = ACCESS_ONCE(g_test_counter1);
        const auto temp_counter2 = ACCESS_ONCE(g_test_counter2);
        std::cout << "Diff = " << temp_counter1 - print_counter1
            << ", Producer failed = " << temp_counter2 - print_counter2
            << std::endl;

        print_counter1 = temp_counter1;
        print_counter2 = temp_counter2;
    }

    producer_thrd.join();

    return 0;
}