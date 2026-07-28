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

uint64_t burst = 7000;

void ConsumerThreadMain(MPSCQueue<uint64_t>* mq, MPSCQueue<uint64_t>* mq1, adk::SimpleEventManager* ev_man)
{
    int32_t counter = 0;
    while (true)
    {
        uint64_t begin;
        if (mq->Pop(begin) == adk::ErrorCode::kSuccess)
        {
            mq1->Push(begin);
        }
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

    if (argc > 1)
        burst = atoi(argv[1]);

    MPSCQueue<uint64_t>* mq_p = MPSCQueue<uint64_t>::Create("test_spsc", 8192);
    MPSCQueue<uint64_t>* mq_p1 = MPSCQueue<uint64_t>::Create("test_spsc", 8192);
    adk::SimpleEventManager ev_man(100000ul, 3);
    boost::thread consumer = boost::thread(boost::bind(ConsumerThreadMain, mq_p, mq_p1, &ev_man));

    uint64_t begin, end;
    uint64_t save_data[10000];
    while (true)
    {
        int32_t counter = -1;
        while (++counter < burst)
        {
            begin = adk::GetTSC();
            ADK_MB();
            mq_p->Push(begin);

            do {
                if (mq_p1->Pop(begin) == adk::ErrorCode::kSuccess)
                {
                    ADK_MB();
                    end = adk::GetTSC();
                    save_data[counter] = end - begin;
                    break;
                }
            } while (true);
        }

        qsort(save_data, counter, sizeof(uint64_t), CompareLatency);
        uint64_t sum = 0;
        while (--counter >= 0)
        {
            sum += save_data[counter];
        }

        std::cout << "min = " << save_data[0] << ", max = " << save_data[burst - 1]
                  << ", avg = " << sum / burst 
                  << ", 50 = " << save_data [burst /2] 
                  << ", 90 = " << save_data [burst * 90 / 100]
                  << ", 95 = " << save_data [burst * 95 / 100]
                  << std::endl;
        sleep(1);
    }

    consumer.join();
    return 0;
}
