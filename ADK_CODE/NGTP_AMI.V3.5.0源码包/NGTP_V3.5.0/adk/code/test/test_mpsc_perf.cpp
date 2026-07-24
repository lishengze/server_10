#include <adk/lock_free_msg_queue.h>
#include <adk/error_code.h>
#include <adk/arch/generic.h>

#include <boost/thread/thread.hpp>

uint64_t g_counter = 0;

using namespace adk;
volatile bool g_is_start = false;
void Producer(MPSCQueue* mq)
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

void Consumer(MPSCQueue* mq)
{
    while (!g_is_start);

    uint64_t counter = 0;
    uint64_t recv_counter = 0;
    uint64_t backoff = 0;

    while (1)
    {
        ++counter;
        backoff = 256;
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

void Obverser(MPSCQueue* mq)
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
    MPSCQueue* mpsc_q1 = MPSCQueue::Create("test_mpsc", sizeof(uint64_t), 8192);
    MPSCQueue* mpsc_q2 = MPSCQueue::Duplicate(*mpsc_q1);

    boost::thread p_thread = boost::thread(boost::bind(Producer, mpsc_q1));
    boost::thread c_thread = boost::thread(boost::bind(Consumer, mpsc_q2));
    boost::thread o_thread = boost::thread(boost::bind(Obverser, mpsc_q1));

    sleep(1);

    g_is_start = true;

    p_thread.join();

    return 0;
}
