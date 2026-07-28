#include <adk/lock_free_msg_queue.h>
#include <adk/error_code.h>
#include <adk/arch/generic.h>

#include <boost/thread/thread.hpp>

uint64_t g_counter = 0;

using namespace adk;
volatile bool g_is_start = false;
void Producer(MPSCQueue* mq, uint64_t* counter, uint64_t index)
{
    *counter = index << 63;
    while (!g_is_start);

    uint64_t backoff = 0;
    while (1)
    {
        ++(*counter);
        backoff = 1;
        while (mq->Push(*counter) != adk::ErrorCode::kSuccess)
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

    uint64_t counter[2] = { 0, 0 };
    uint64_t recv_counter = 0;
    uint64_t backoff = 0;

    while (1)
    {
        backoff = 256;
        while (mq->Pop(recv_counter) != adk::ErrorCode::kSuccess)
        {
            for (uint32_t i = 0; i < backoff; ++i)
                ADK_PAUSE();
            backoff <<= 1;
        }

        uint64_t& counter_ref = counter[recv_counter >> 63];
        ++counter_ref;

        if (ADK_UNLIKELY(counter_ref != (recv_counter & ((1ul << 63) - 1))))
        {
            std::cout << "BUG ON!" << std::endl;
        }
    }
}

void Obverser(MPSCQueue* mq, uint64_t* counter)
{
    while (!g_is_start);

    uint64_t saved_counter = 0;
    while (1)
    {
        QueueStats stats;
        mq->GetStats(stats);
        uint64_t temp = (counter[0] + counter[16]);
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

    uint64_t* counter_array = new uint64_t[64]();

    boost::thread p_thread1 = boost::thread(boost::bind(Producer, mpsc_q1, &counter_array[0], 0));
    boost::thread p_thread2 = boost::thread(boost::bind(Producer, mpsc_q1, &counter_array[16], 1));
    boost::thread c_thread = boost::thread(boost::bind(Consumer, mpsc_q2));
    boost::thread o_thread = boost::thread(boost::bind(Obverser, mpsc_q1, &counter_array[0]));

    sleep(1);

    g_is_start = true;

    p_thread1.join();
    p_thread2.join();

    return 0;
}
