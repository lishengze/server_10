#include <time.h>
#include <stdio.h>
#include <iostream>

#include <adk/lock_free_queue_variant.h>
#include <adk/error_code.h>
#include <adk/arch/generic.h>
#include <adk/event.h>

#include <boost/thread/thread.hpp>

using namespace adk::variant;
// using namespace adk;

int CompareLatency(const void *a, const void *b)
{
    const uint64_t a_v = *reinterpret_cast<const uint64_t*>(a);
    const uint64_t b_v = *reinterpret_cast<const uint64_t*>(b);
    return a_v - b_v;
}

void ConsumerThreadMain(SPSCQueue<uint64_t>* mq, adk::SimpleEventManager* ev_man)
{
    uint64_t begin, end;
    uint64_t save_data[10000];
    int32_t counter = 0;
    while (true)
    {
        if (mq->Pop(begin) == adk::ErrorCode::kSuccess)
        {
            ADK_MB();
            end = adk::GetTSC();
            // std::cout << "wakeup time used = " << end - begin << std::endl;
            save_data[counter] = end - begin;
            ++counter;
        }

        // else
        // {
        //     for (uint32_t i = 16; i != 0; --i)
        //         ADK_PAUSE();
        //     continue;
        // }

        if (counter == 7000)
        {
            qsort(save_data, counter, sizeof(uint64_t), CompareLatency);
            uint64_t sum = 0;
            while (--counter >= 0)
            {
                sum += save_data[counter];
            }

            std::cout << "min = " << save_data[0] << ", max = " << save_data[6999]
                      << ", avg = " << sum / 7000 
                      << ", 50 = " << save_data [7000 /2] 
                      << ", 90 = " << save_data [7000 * 90 / 100]
                      << ", 95 = " << save_data [7000 * 95 / 100]
                      << std::endl;

            counter = 0;
        }

        // for (int32_t i = 0; i < 16; ++i)
        //     ADK_PAUSE();
    }
}

int main(int argc, char const *argv[])
{
    uint64_t rdtsc_begin = adk::GetTSC(), rdtsc_end;
    for (int32_t i = 0; i < 1000000; ++i)
    {
        ADK_MB();
        adk::GetTSC();
    }

    rdtsc_end = adk::GetTSC();
    std::cout << "adk::GetTSC() time used = " << (rdtsc_end - rdtsc_begin) / 1000000 << std::endl;
    sleep(1);

    SPSCQueue<uint64_t>* mq_p = SPSCQueue<uint64_t>::Create("test_spsc", 8192);
    SPSCQueue<uint64_t>* mq_c = SPSCQueue<uint64_t>::Duplicate(*mq_p);

    adk::SimpleEventManager ev_man(100000ul, 3);
    boost::thread consumer = boost::thread(boost::bind(ConsumerThreadMain, mq_c, &ev_man));

    uint64_t begin;
    while (true)
    {
        int32_t counter = 0;
        while (++counter <= 7000)
        {
            begin = adk::GetTSC();
            ADK_MB();
            mq_p->Push(begin);
        }

        sleep(1);
    }

    consumer.join();
    return 0;
}
