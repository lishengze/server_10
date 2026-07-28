#include <adk_pack/lock_free_unbounded_queue.h>

#include <boost/thread/thread.hpp>

#include "test_lock_free_unbounded_queue.h"

using namespace adk;

void Producer(SPSCUnboundedQueue<uint64_t>* mq)
{
    uint64_t counter = 0;
    while (true)
    {
        ++counter;
        mq->Push(counter);
        // for (uint32_t i = 0; i < 2; ++i)
        //     ADK_PAUSE();
    }
}

uint64_t g_counter = 0;
void Consumer(SPSCUnboundedQueue<uint64_t>* mq)
{
    uint64_t recv_counter;
    while (true)
    {
        ++g_counter;
        while (mq->Pop(recv_counter) != ErrorCode::kSuccess)
        {
            for (uint32_t i = 0; i < 64; ++i)
                ADK_PAUSE();
        }
        
        if (recv_counter != g_counter)
        {
            std::cout << "Bug On!" << std::endl;
        }
    }
}

void Observer()
{
    uint64_t saved_counter = 0;
    struct timespec last_calc;
    int64_t rate = 0;

    clock_gettime(CLOCK_REALTIME, &last_calc);
    
    while(true)
    {
        ADK_CALC_RATE(last_calc, saved_counter, g_counter, rate);
        std::cout << "rate = " << rate << std::endl;
        sleep(1);
    }
}

int main(int argc, char const *argv[])
{
    SPSCUnboundedQueue<uint64_t>* mq = SPSCUnboundedQueue<uint64_t>::Create("test_unbounded_queue", 8, 64);
    assert(mq->Head() == NULL);
    assert(mq->ElementAt(0) == NULL);
    assert(mq->ElementAt(1) == NULL);

    uint64_t counter = 0;
    while (++counter <= 260)
    {
        mq->Push(counter);
    }

    assert(mq->ElementAt(0) == NULL);
    assert(mq->ElementAt(261) == NULL);
    assert(*(mq->ElementAt(1)) == 1);
    assert(*(mq->ElementAt(260)) == 260);

    assert(*(mq->Head()) == 1);

    mq->Pop();

    assert(*(mq->Head()) == 2);
    assert(*(mq->Head()) == 2);

    uint64_t counter2 = 2;
    while (true)
    {
        ++counter2;
        mq->Pop(); 
        if (counter2 == counter)
            break;
        assert(*(mq->Head()) == counter2);
    }

    assert(mq->Head() == NULL);

    while (counter <= 512)
    {
        mq->Push(counter);
        ++counter;
    }
    assert(*(mq->Head()) == 261);

    assert(mq->ElementAt(260) == NULL);
    assert(mq->ElementAt(513) == NULL);
    assert(*(mq->ElementAt(261)) == 261);
    assert(*(mq->ElementAt(512)) == 512);
    return 0;
}
