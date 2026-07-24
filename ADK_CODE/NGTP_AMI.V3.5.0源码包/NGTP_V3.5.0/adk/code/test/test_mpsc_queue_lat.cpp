#include <boost/thread/thread.hpp>
#include <adk/lock_free_msg_queue.h>
#include <adk/error_code.h>
#include <assert.h>
#include <stdint.h>
#include <iostream>
#include <sched.h>

using namespace adk;

uint64_t g_counter = 0;
int64_t threshold = 10;
int64_t loops = 80000;
void Consumer(MPSCQueue* mq)
{
    struct sched_param param;
    param.sched_priority = sched_get_priority_min(SCHED_FIFO);
            
    int policy = SCHED_FIFO;
    if (sched_setscheduler(0, policy, &param) == 0)
    {}

    uint64_t sqn = 0, sqn1;
    struct timeval tv;
    while (1)
    {
        int32_t ret = mq->Pop(sqn);
        if (ret == ErrorCode::kSuccess)
        {
            gettimeofday(&tv, nullptr);
            sqn1 = tv.tv_sec * 1000000 + tv.tv_usec;
            if (sqn1 - sqn > threshold)
                std::cout << "diff = " << sqn1 - sqn << " !!!!!!!!!!!!!!!!!!!!!!"<< std::endl;
            ++g_counter;
        }
    }
}

void __attribute__ ((noinline)) Nothing(int argc);

volatile int g_temp = 0;
void Producer(MPSCQueue* mq)
{
    struct sched_param param;
    param.sched_priority = sched_get_priority_min(SCHED_FIFO);
            
    int policy = SCHED_FIFO;
    if (sched_setscheduler(0, policy, &param) == 0)
    {}

    uint64_t sqn = 0;
    struct timeval tv;
    while (1)
    {
        gettimeofday(&tv, nullptr);
        sqn = tv.tv_sec * 1000000 + tv.tv_usec;
        while (mq->Push(sqn) != ErrorCode::kSuccess);

        for (int i = 0; i < loops; ++i)
            Nothing(g_temp);
    }
}

void Nothing(int argc)
{}


int main(int argc, char const *argv[])
{
    if (argc > 1)
        threshold = atol(argv[1]);
    if (argc > 2)
        loops = atol(argv[2]);

    MPSCQueue* mq = MPSCQueue::Create("test", sizeof(uint64_t), 8192);
    boost::thread c_thread = boost::thread(boost::bind(Consumer, mq));
    sleep(1);
    boost::thread p_thread = boost::thread(boost::bind(Producer, mq));

    uint64_t g_counter_saved = 0;
    while (1)
    {
        sleep(1);
        uint64_t temp = ACCESS_ONCE(g_counter);
        std::cout << "rate = " << temp - g_counter_saved << std::endl;
        g_counter_saved = temp;
    }
    c_thread.join();
    return 0;
}
