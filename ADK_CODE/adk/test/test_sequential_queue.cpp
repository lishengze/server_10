#include <adk/lock_free_msg_queue.h>
#include <adk/error_code.h>
#include <adk/arch/generic.h>

#include <boost/thread/thread.hpp>

uint64_t g_counter = 0;

using namespace adk;
volatile bool g_is_start = false;
#define INC_STEP    2
void Producer(SCSequentialQueue* mq, uint64_t idx)
{
    while (!g_is_start);

    uint64_t backoff = 0;

    uint64_t counter = idx;
    while (1)
    {
        backoff = 128;
        while (mq->Push(counter, counter) != adk::ErrorCode::kSuccess)
        {
            for (uint32_t i = 0; i < backoff; ++i)
                ADK_PAUSE();
            // backoff <<= 1;
        }

        counter += INC_STEP;
    }
}

void Consumer(SCSequentialQueue* mq)
{
    while (!g_is_start);

    uint64_t recv_counter = 0;
    uint64_t backoff = 0;

    while (1)
    {
        ++g_counter;
        // backoff = 8;
        while (mq->Pop(recv_counter) != adk::ErrorCode::kSuccess)
        {
            // for (uint32_t i = 0; i < backoff; ++i)
                ADK_PAUSE();
            // backoff <<= 1;
        }

        if (ADK_UNLIKELY(g_counter != recv_counter))
        {
            std::cout << "BUG ON! "
            << "g_counter = " << g_counter << " "
            << "recv_counter = " << recv_counter << std::endl;
        }
    }
}

int main(int argc, char const *argv[])
{
    SCSequentialQueue* mq_p1 = SCSequentialQueue::Create("test_spsc", 8, 8192);
    SCSequentialQueue* mq_p2 = SCSequentialQueue::Duplicate(*mq_p1);
    SCSequentialQueue* mq_c = SCSequentialQueue::Duplicate(*mq_p1);

    boost::thread c_thread = boost::thread(boost::bind(Consumer, mq_c));
    boost::thread p_thread1 = boost::thread(boost::bind(Producer, mq_p1, 1));
    boost::thread p_thread2 = boost::thread(boost::bind(Producer, mq_p2, 2));

    sleep(1);

    g_is_start = true;

    uint64_t local_counter = 0;
    while (1)
    {
        sleep(1);
        uint64_t saved = g_counter;
        std::cout << "speed : " << saved - local_counter << std::endl;
        local_counter = saved;
    }

    c_thread.join();

    return 0;
}
