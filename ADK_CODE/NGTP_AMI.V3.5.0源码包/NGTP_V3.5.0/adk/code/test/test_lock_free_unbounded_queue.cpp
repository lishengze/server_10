#include <adk/lock_free_unbounded_queue.h>
#include <adk/util.h>

#include <boost/thread/thread.hpp>

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
    SPSCUnboundedQueue<uint64_t>* mq = SPSCUnboundedQueue<uint64_t>::Create("test_unbounded_queue", 4, 8192);
    boost::thread cthread = boost::thread(boost::bind(Consumer, mq));
    boost::thread pthread = boost::thread(boost::bind(Producer, mq));
    boost::thread othread = boost::thread(boost::bind(Observer));

    cthread.join();
    return 0;
}
