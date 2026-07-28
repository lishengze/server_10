#include <stdlib.h>

#include <thread>
#include <vector>
#include <iostream>
#include <adk/lock_free_queue_variant.h>

int main(int argc, char* argv[])
{
    uint32_t test_thread_num = 4;
    if (argc > 1)
    {
        test_thread_num = atoi(argv[1]);
    }

    uint32_t queue_size = 32;
    if (argc > 2)
    {
        queue_size = atoi(argv[2]);
    }

    std::cout << "test thread number = " << test_thread_num << std::endl;
    std::cout << "test queue size = " << queue_size << std::endl;

    auto* const test_pool = adk::variant::SPMCQueue<uint64_t>::Create("pool", queue_size);
    for (uint32_t index = 0; index < queue_size; ++index)
    {
        if (ADK_UNLIKELY(adk::ErrorCode::kSuccess != test_pool->Push((uint64_t)index)))
        {
            std::cout << ">>>> " << __LINE__ << " <<<< BUG ON " << std::endl;
            abort();
        }
    }

    auto* const garbage_queue = adk::variant::MPSCQueue<uint64_t>::Create("gc", queue_size);

    std::vector<std::thread> consume_threads;
    for (uint32_t thread_index = 0; thread_index < test_thread_num; ++thread_index)
    {
        consume_threads.push_back(std::thread([&]() {
            do
            {
                uint64_t value;
                if (adk::ErrorCode::kSuccess == test_pool->Pop(value))
                {
                    if (ADK_UNLIKELY(adk::ErrorCode::kSuccess != garbage_queue->Push(value)))
                    {
                        std::cout << ">>>> " << __LINE__ << " <<<< BUG ON " << std::endl;
                        abort();
                    }
                }
            } while (true);
        }));
    }


    std::thread garbage_thread = std::thread([&]() {
        do 
        {
            uint64_t value;
            if (adk::ErrorCode::kSuccess == garbage_queue->Pop(value))
            {
                if (ADK_UNLIKELY(adk::ErrorCode::kSuccess != test_pool->Push(value)))
                {
                    std::cout << ">>>> " << __LINE__ << " <<<< BUG ON " << std::endl;
                    abort();
                }
            }
        } while (true);
    });

    uint64_t last_pop_sqn_rec = 0;
    do
    {
        sleep(1);
        const auto last_pop_sqn = garbage_queue->last_pop_sqn();
        std::cout << "diff = " << last_pop_sqn - last_pop_sqn_rec << std::endl;
        last_pop_sqn_rec = last_pop_sqn;
    } while (true);

    return 0;
}