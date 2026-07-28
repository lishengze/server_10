#include "test_case.h"

#include <iostream>

#include <adk/shm_cont_channel.h>

constexpr uint32_t kTestBufferSize = 128;

int main(int argc, char** argv)
{
    const std::string sccl_name = "shm_test_v1";
    auto* const agent = adk::sccl::Agent::Create(sccl_name);
    if (nullptr == agent)
    {
        std::cout << "Create Agent " << sccl_name << " failed" << std::endl;
        return -1;
    }

    auto* const proxy = adk::sccl::Proxy::Create(sccl_name);
    if (nullptr == proxy)
    {
        std::cout << "connect to Agent " << sccl_name << " failed" << std::endl;
        return -1;
    }

    volatile uint64_t* volatile consumer_nr = nullptr;
    volatile uint64_t* volatile consumer_failed_nr = nullptr;
    std::thread consumer = std::thread([&]() {
        char payload[kTestBufferSize];
        consumer_nr = reinterpret_cast<uint64_t*>(&payload[0]);

        uint64_t counter = 0;
        consumer_failed_nr = &counter;

        uint64_t expected = 0;
        do 
        {
            auto* const entry = agent->TryWaitEntry();
            if (nullptr == entry)
            {
                ++counter;
                usleep(0);
                continue;
            }

            if (kTestBufferSize != entry->BufferSize())
            {
                std::cout << "### BUG ###" << __LINE__ << std::endl;
            }

            shm_memory(payload, entry->Buffer(), kTestBufferSize);
            agent->FreeEntry(entry);

            if (expected != *consumer_nr)
            {
                std::cout << "### BUG ### value = " << *consumer_nr 
                          << ", expected value = " << expected << std::endl;
            }

            expected = *consumer_nr + 1;
        } while (true);
    });

    volatile uint64_t* volatile producer_nr = nullptr;
    volatile uint64_t* volatile producer_failed_nr = nullptr;
    std::thread producer = std::thread([&]() {
        char payload[kTestBufferSize];
        producer_nr = reinterpret_cast<uint64_t*>(&payload[0]);

        uint64_t counter = 0;
        producer_failed_nr = &counter;

        do 
        {
            auto* const buffer = proxy->AllocBuffer(kTestBufferSize);
            if (nullptr != buffer)
            {
                shm_memory(buffer, payload, kTestBufferSize);
                proxy->PostBuffer(buffer, kTestBufferSize);
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
    return 0;
}