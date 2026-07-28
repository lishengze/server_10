#include <thread>
#include <iostream>

#include <adk/lock_free_cont_memory.h>

using adk::ErrorCode;
using adk::ContEntry;
using adk::ContinueMemory;
using adk::ShmContMemManager;

uint64_t g_test_counter1 = 0;
uint64_t g_test_counter3 = 0;
uint32_t g_allocate_size = 128;
volatile bool g_is_running = false;

void Consumer(ContinueMemory* continue_memory)
{
    uint64_t consumer_record = 0;
    uint64_t consumer_fail_counter = 0;
    struct ContEntry* entry_ptr = nullptr;

    uint64_t backoff = 2048 * 4;
    while (g_is_running)
    {
        if (ADK_UNLIKELY(ErrorCode::kSuccess != continue_memory->TryWaitEntry(&entry_ptr)))
        {
            if (ADK_UNLIKELY(++consumer_fail_counter & 0x800))
            {
                g_test_counter3 = consumer_fail_counter;
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

        //backoff = 128;
        assert(entry_ptr->len >= g_allocate_size);

        const auto temp = *((uint64_t*)entry_ptr->GetBuffer());
        if (ADK_UNLIKELY(temp != consumer_record))
        {
            std::cout << "###### counter diff = << " << temp - consumer_record << " ######" << std::endl;
        }

        consumer_record = temp + 1;
        continue_memory->FreeEntry(entry_ptr);

        if (ADK_UNLIKELY(consumer_record & 0x8000))
        {
            g_test_counter1 = consumer_record;
        }
    }
}

int main(int argc, char* argv[])
{
    ShmContMemManager* shm_manager = ShmContMemManager::Attach("cont_memory_test");
    if (nullptr == shm_manager)
    {
        std::cout << "Attach share memory failed" << std::endl;
        return 0;
    }

    ContinueMemory* cont_memroy = shm_manager->AttachShmContMemory("channel1");
    if (nullptr == cont_memroy)
    {
        std::cout << "Attach share continue memory channel failed" << std::endl;
        return 0;
    }

    g_is_running = true;

    std::thread consumer_thrd = std::thread(Consumer, cont_memroy);

    uint64_t print_counter1 = 0;
    uint64_t print_counter3 = 0;
    while (g_is_running)
    {
        sleep(1);
        const auto temp_counter1 = ACCESS_ONCE(g_test_counter1);
        const auto temp_counter3 = ACCESS_ONCE(g_test_counter3);
        std::cout << "Diff = " << temp_counter1 - print_counter1
                  << ", Consumer failed = " << temp_counter3 - print_counter3
                  << std::endl;

        print_counter1 = temp_counter1;
        print_counter3 = temp_counter3;
    }

    consumer_thrd.join();

    return 0;
}