#include <adk_pack/token_buckets.h>
#include <adk_pack/error_code.h>

#include <malloc.h>

#include <thread>
#include <iostream>

#include <boost/format.hpp>

using namespace adk;

constexpr uint32_t kTestThreadNum = 3;

void Stream(uint64_t* byte_counter, TokenBucket* token_bucket)
{
    while (true)
    {
        if (adk::ErrorCode::kSuccess == token_bucket->TryAcquire(1))
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
    char* test_buffer = (char*)memalign(4096, kTestThreadNum * 64);
    assert(test_buffer);

    uint64_t* byte_counter[kTestThreadNum];
    std::thread thread_hdl[kTestThreadNum];
    for (uint32_t index=0; index<kTestThreadNum; ++index)
    {
        uint64_t* counter = (uint64_t*)(test_buffer + index * 64);
        *counter = 0;

        TokenBucket* const tb_temp = RateControl::GetInstance<rate_unit::Microsecond>(index + 1);
        byte_counter[index] = counter;

        tb_temp->SetCapacity(1024 * 2 * (index + 1));
        thread_hdl[index] = std::thread(Stream, counter, tb_temp);
    };

    boost::format fmt;
    uint64_t record_counter[kTestThreadNum] = { 0 };
    while (true)
    {
        sleep(1);

        for (uint32_t index=0; index<kTestThreadNum; ++index)
        {
            const uint64_t counter = *(byte_counter[index]);
            fmt = boost::format("Thread %1% token diff = %2%") % index % (counter - record_counter[index]);
            record_counter[index] = counter;    
            std::cout << fmt.str() << std::endl;
        }
    }

    for (uint32_t thread_index=0; thread_index<kTestThreadNum; ++thread_index)
    {
        thread_hdl[thread_index].join();
    }

    return 0;
}