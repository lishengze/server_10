#include <adk/token_buckets.h>

#include <iostream>
#include <boost/format.hpp>

using namespace adk;

constexpr uint32_t kTestThreadNum = 3;

void Stream(uint64_t* byte_counter, TokenBucket* token_bucket)
{
    while (true)
    {
        if (kSuccess == token_bucket->TryAcquire(1))
        {
            (*byte_counter) += 1;
        }
        else
        {
            std::this_thread::yield();
        }
    }
}

int main()
{
    char* test_buffer = (char*)aligned_malloc(ADK_PAGE_SIZE, kTestThreadNum * ADK_CACHE_LINE_SIZE);
    assert(test_buffer);

    TokenBucket* const tb_temp = RateControl::GetInstance<rate_unit::Microsecond>(1);
    tb_temp->SetCapacity(1024 * 4);

    uint64_t* byte_counter[kTestThreadNum];
    std::thread thread_hdl[kTestThreadNum];
    for (uint32_t index=0; index<kTestThreadNum; ++index)
    {
        uint64_t* counter = (uint64_t*)(test_buffer + index * ADK_CACHE_LINE_SIZE);
        *counter = 0;
        byte_counter[index] = counter;
        thread_hdl[index] = std::thread(Stream, counter, tb_temp);
    };

    boost::format fmt;
    uint64_t record_counter[kTestThreadNum] = { 0 };
    while (true)
    {
        sleep(1);

        uint64_t total = 0;
        for (uint32_t index=0; index<kTestThreadNum; ++index)
        {
            const uint64_t counter = *(byte_counter[index]);
            total += (counter - record_counter[index]);
            record_counter[index] = counter;
        }

        fmt = boost::format("Thread token diff = %1%") % total;
        std::cout << fmt.str() << std::endl;
    }

    for (uint32_t thread_index=0; thread_index<kTestThreadNum; ++thread_index)
    {
        thread_hdl[thread_index].join();
    }

    return 0;
}