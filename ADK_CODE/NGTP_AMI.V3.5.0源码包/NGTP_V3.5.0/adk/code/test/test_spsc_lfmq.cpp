#include <adk/lock_free_msg_queue.h>
#include <adk/error_code.h>
#include <adk/arch/generic.h>

#include <boost/thread/thread.hpp>

uint64_t g_counter = 0;

using namespace adk;
volatile bool g_is_start = false;
void Producer(SPSCQueue<uint64_t>* mq)
{
    while (!g_is_start);

    uint64_t backoff = 0;
    while (1)
    {
        ++g_counter;
        backoff = 1;
        while (mq->Push(g_counter) != adk::ErrorCode::kSuccess)
        {
            for (uint32_t i = 0; i < backoff; ++i)
                ADK_PAUSE();
            backoff <<= 1;
        }
    }
}

void Consumer(SPSCQueue<uint64_t>* mq)
{
    while (!g_is_start);

    uint64_t counter = 0;
    uint64_t recv_counter = 0;
    uint64_t backoff = 0;

    while (1)
    {
        ++counter;
        backoff = 128;
        while (mq->Pop(recv_counter) != adk::ErrorCode::kSuccess)
        {
            for (uint32_t i = 0; i < backoff; ++i)
                ADK_PAUSE();
            backoff <<= 1;
        }

        if (ADK_UNLIKELY(counter != recv_counter))
        {
            std::cout << "BUG ON!" << std::endl;
        }
    }
}

void Obverser(SPSCQueue<uint64_t>* mq)
{
    while (!g_is_start);

    uint64_t saved_counter = 0;
    while (1)
    {
        QueueStats stats;
        mq->GetStats(stats);
        uint64_t temp = g_counter;
        std::cout << "counter_diff = " << temp - saved_counter << ", "
                  << "nr_failed = " << stats.nr_forward_fail << ", "
                  << "max_length = " << stats.max_queue_length
                  << std::endl;
        saved_counter = temp;
        sleep(1);
    }
}

int main(int argc, char const *argv[])
{
    SPSCQueue<uint64_t>* mq_p = SPSCQueue<uint64_t>::Create("test_spsc", 8192);
    SPSCQueue<uint64_t>* mq_c = SPSCQueue<uint64_t>::Duplicate(*mq_p);

    boost::thread p_thread = boost::thread(boost::bind(Producer, mq_p));
    boost::thread c_thread = boost::thread(boost::bind(Consumer, mq_c));
    boost::thread o_thread = boost::thread(boost::bind(Obverser, mq_p));

    sleep(1);

    g_is_start = true;

    p_thread.join();

    return 0;
}
