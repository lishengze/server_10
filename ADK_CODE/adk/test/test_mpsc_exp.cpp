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
constexpr uint64_t begin_shift = 60;

void Observer(uint64_t* counter)
{
    uint64_t counter_saved = 0;
    while (true)
    {
        uint64_t temp = *counter;
        std::cout << "counter_diff = " << temp - counter_saved << std::endl;
        counter_saved = temp;
        sleep(1);
    }
}

void Consumer(MPSCQueue<uint64_t>* mq, uint64_t* counter, adk::SimpleEventManager* ev_man)
{
    uint64_t begin_array[16];
    memset(begin_array, 0x00, sizeof(begin_array));
    constexpr uint64_t begin_mask = (1ul << begin_shift) - 1ul;
    while (true)
    {
        uint64_t begin;
        if (mq->Pop(begin) == adk::ErrorCode::kSuccess)
        {
            ++(*counter);
            uint64_t& next = begin_array[begin >> begin_shift];
            if ((begin & begin_mask) != next)
            {
                std::cout << "Bug on ! orig_begin = " << begin 
                          << " begin = " << (begin & begin_mask) 
                          << " next = " << next 
                          << " counter = " << *counter
                          << std::endl;
                sleep(100000);
            }
            ++next;
            continue;
        }
        else
        {
            ADK_PAUSE();
            // for (uint32_t i = 32; i > 0; --i)
            //         ADK_PAUSE();
        }
    }
}

void Producer(MPSCQueue<uint64_t>* mq, uint64_t begin)
{
    while (true)
    {
        while (mq->Push(begin) != adk::ErrorCode::kSuccess)
        {
            ADK_PAUSE();
            // for (uint32_t i = 64; i > 0; --i)
            //     ADK_PAUSE();
        }
        ++begin;
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
    adk::SimpleEventManager ev_man(100000ul, 3);
    uint64_t counter = 0;
    boost::thread consumer = boost::thread(boost::bind(Consumer, mq_p, &counter, &ev_man));
    boost::thread p1 = boost::thread(boost::bind(Producer, mq_p, 0UL));
    boost::thread p2 = boost::thread(boost::bind(Producer, mq_p, (1UL << 60)));
    boost::thread observer = boost::thread(boost::bind(Observer, &counter));

    consumer.join();
    return 0;
}
