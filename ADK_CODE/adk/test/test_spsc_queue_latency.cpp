#include <adk/lock_free_msg_queue.h>
#include <adk/error_code.h>
#include <adk/arch/generic.h>
#include <adk/high_performance_clock.h>

#include <boost/thread/thread.hpp>

/*
 *  config: 
 *          sysctl -w kernel.sched_rt_runtime_us=-1
 *
*  test mpsc
 *  command: 
 *          chrt -f 20  taskset -c 5-10 bin/gcc-4.8.5/release/debug-symbols-on/threading-multi/test_spsc_queue_latency 1
 *  data:
 *          counter_diff = 681713, nr_failed = 0, max_length = 1, avg_latency = 134, min_latency = 37, max_latency = 641, errors = 0, tick_per_us = 2593916987
            counter_diff = 681812, nr_failed = 0, max_length = 1, avg_latency = 134, min_latency = 37, max_latency = 456, errors = 0, tick_per_us = 2593914923
            counter_diff = 681526, nr_failed = 0, max_length = 1, avg_latency = 134, min_latency = 37, max_latency = 2110, errors = 0, tick_per_us = 2593913478
            counter_diff = 681975, nr_failed = 0, max_length = 1, avg_latency = 134, min_latency = 37, max_latency = 382, errors = 0, tick_per_us = 2593912491
            counter_diff = 681959, nr_failed = 0, max_length = 1, avg_latency = 134, min_latency = 49, max_latency = 431, errors = 0, tick_per_us = 2593911776
            counter_diff = 681835, nr_failed = 0, max_length = 2, avg_latency = 134, min_latency = 37, max_latency = 2826, errors = 0, tick_per_us = 2593911271
            counter_diff = 682134, nr_failed = 0, max_length = 1, avg_latency = 134, min_latency = 49, max_latency = 333, errors = 0, tick_per_us = 2593910925
            counter_diff = 681814, nr_failed = 0, max_length = 1, avg_latency = 134, min_latency = 37, max_latency = 481, errors = 0, tick_per_us = 2593910644
            counter_diff = 681744, nr_failed = 0, max_length = 2, avg_latency = 134, min_latency = 37, max_latency = 2863, errors = 0, tick_per_us = 2593910510
            counter_diff = 681745, nr_failed = 0, max_length = 1, avg_latency = 134, min_latency = 37, max_latency = 1073, errors = 0, tick_per_us = 2593910437
            counter_diff = 681994, nr_failed = 0, max_length = 1, avg_latency = 134, min_latency = 49, max_latency = 431, errors = 0, tick_per_us = 2593910261
            counter_diff = 681820, nr_failed = 0, max_length = 1, avg_latency = 134, min_latency = 49, max_latency = 876, errors = 0, tick_per_us = 2593910238
            counter_diff = 681839, nr_failed = 0, max_length = 1, avg_latency = 134, min_latency = 37, max_latency = 468, errors = 0, tick_per_us = 2593910197
            counter_diff = 681932, nr_failed = 0, max_length = 1, avg_latency = 134, min_latency = 37, max_latency = 481, errors = 0, tick_per_us = 2593910170
            counter_diff = 681757, nr_failed = 0, max_length = 4, avg_latency = 134, min_latency = 37, max_latency = 4319, errors = 0, tick_per_us = 2593910186
            counter_diff = 681964, nr_failed = 0, max_length = 1, avg_latency = 134, min_latency = 37, max_latency = 185, errors = 0, tick_per_us = 2593910119

 */

using namespace adk;
volatile bool g_is_start = false;

uint64_t g_counter = 0;

template<typename T>
void Producer(T* mq)
{
    while (!g_is_start);

    uint64_t backoff = 0;

    while (1)
    {
        ++g_counter;
        backoff = 32;
        while (mq->PushTsc(g_counter) != adk::ErrorCode::kSuccess)
        {
            
        }

        for (uint32_t i = 0; i < backoff; ++i)
                ADK_PAUSE();
            // backoff <<= 1;
    }
}

void Consumer(MPSCQueue* mq)
{
    while (!g_is_start);

    uint64_t counter = 0;
    uint64_t recv_counter = 0;

    while (1)
    {
        Entry* entry;
        int32_t ret = mq->WaitEntryTsc<true>(&entry);
        if (ret != kSuccess) 
        {
            continue;
        }

        mq->SaveLatency(entry);

        ++counter;
        char* buf = entry->buffer;
        recv_counter = *(uint64_t*)(buf);
        mq->FreeEntryTsc(entry);
        if (ADK_UNLIKELY(counter != recv_counter))
        {
            std::cout << "BUG ON!" << std::endl;
        }
    }
}

void* g_mq;

template<typename T>
void Obverser(T* mq)
{
    tick::TickClock tc;
    g_mq = mq;
    while (!g_is_start);

    uint64_t saved_counter = 0;
    while (1)
    {
        tc.Adjust();
        QueueStats stats;
        QueueLatStats lat_stats;
        mq->GetStats(stats);
        mq->CalcLatency(lat_stats);

        uint64_t temp = g_counter;
        std::cout << "counter_diff = " << temp - saved_counter << ", "
                  << "nr_failed = " << stats.nr_forward_fail << ", "
                  << "max_length = " << stats.max_queue_length << ", "
                  << "avg_latency = " << tc.ToNanoseconds(lat_stats.avg) << ", "
                  << "min_latency = " << tc.ToNanoseconds(lat_stats.min) << ", "
                  << "max_latency = " << tc.ToNanoseconds(lat_stats.max) << ", "
                  << "errors = " << lat_stats.errors << ", "
                  << "tick_per_us = " << tc.tick_per_sec()
                  << std::endl;
        saved_counter = temp;
        sleep(1);
    }
}

void impl_0()
{
    MPSCQueue* mq_p = MPSCQueue::Create<uint64_t>("test_spsc", 8192, ADK_LFMQ_FLAG_CREATE_AVG_LAT_BUF);
    MPSCQueue* mq_c = MPSCQueue::Duplicate(*mq_p);

    boost::thread p_thread = boost::thread(boost::bind(Producer<MPSCQueue>, mq_p));
    boost::thread c_thread = boost::thread(boost::bind(Consumer, mq_c));
    boost::thread o_thread = boost::thread(boost::bind(Obverser<MPSCQueue>, mq_c));

    sleep(1);

    g_is_start = true;

    p_thread.join();
}

int main(int argc, char const *argv[])
{
    typedef void (*FuncType)();
    FuncType f_array[1024] = {impl_0};
    // int select = 0;
    // if (argc != 1)
    // {
    //     select = atoi(argv[1]);
    // }

    f_array[0]();
    return 0;
}
