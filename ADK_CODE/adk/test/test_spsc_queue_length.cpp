#include <adk/lock_free_msg_queue.h>
#include <adk/error_code.h>
#include <adk/arch/generic.h>

#include <boost/thread/thread.hpp>

#include <iostream>

using namespace adk;
    
void Producer(SPSCQueue<uint64_t>* mq)
{
    uint64_t counter = 0;
    while (true)
    {
        mq->Push(counter);
        usleep(1);
    }
}

void Consumer(SPSCQueue<uint64_t>* mq)
{
    sleep(1000);
}

int main(int argc, char const *argv[])
{
    SPSCQueue<uint64_t>* mq_p = SPSCQueue<uint64_t>::Create("test_spsc", 1024);
    SPSCQueue<uint64_t>* mq_c = SPSCQueue<uint64_t>::Duplicate(*mq_p);

    boost::thread pthread = boost::thread(boost::bind(Producer, mq_p));

    while (true)
    {
        QueueStats stats;
        mq_c->GetStats(stats);
        std::cout << "fail = " << stats.nr_forward_fail << ", "
                  << "len = " << stats.max_queue_length << std::endl;
        sleep(1);
        uint64_t temp_var;
        mq_c->Pop(temp_var);
    }

    return 0;
}