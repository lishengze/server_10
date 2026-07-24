#include <time.h>
#include <iostream>

#include <adk/lock_free_msg_queue.h>
#include <adk/error_code.h>
#include <adk/arch/generic.h>
#include <adk/event.h>

#include <boost/thread/thread.hpp>

using namespace adk;

void ConsumerThreadMain(SPSCQueue<struct timespec>* mq, SimpleEventManager* ev_man)
{
    struct timespec begin, end;
    while (true)
    {
        if (mq->Pop(begin) == adk::ErrorCode::kSuccess)
        {
            clock_gettime(CLOCK_MONOTONIC, &end);
            std::cout << "wakeup time used = " << (end.tv_sec - begin.tv_sec) * 1000000000ul + end.tv_nsec - begin.tv_nsec << std::endl;
        }

        for (int32_t i = 0; i < 16; ++i)
            ADK_PAUSE();
    }
}

int main(int argc, char const *argv[])
{

    SPSCQueue<struct timespec>* mq_p = SPSCQueue<struct timespec>::Create("test_spsc", 8192);
    SPSCQueue<struct timespec>* mq_c = SPSCQueue<struct timespec>::Duplicate(*mq_p);

    SimpleEventManager ev_man(100000ul, 3);
    boost::thread consumer = boost::thread(boost::bind(ConsumerThreadMain, mq_c, &ev_man));

    struct timespec begin;
    while (true)
    {
        clock_gettime(CLOCK_MONOTONIC, &begin);
        mq_p->Push(begin);
        sleep(1);
    }

    consumer.join();
    return 0;
}
