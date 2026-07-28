#include <boost/thread/thread.hpp>
#include <adk/lock_free_queue_variant.h>

using namespace adk::variant;

volatile bool g_is_start = false;

uint64_t g_counter = 0;

typedef SPSCQueue<uint64_t> Queue;
//typedef MPSCQueue<uint64_t> Queue;
//typedef SPMCQueue<uint64_t> Queue;
//typedef MPMCQueue<uint64_t> Queue;

void Producer(Queue* queue)
{
    uint64_t& producer_counter = queue->GetCustomCounter(0);

    while (!g_is_start);

    while (1)
    {
        uint32_t backoff_counter = 16;
        while (queue->Push(g_counter) != adk::ErrorCode::kSuccess)
        {
            PAUSE_BACKOFF();
            producer_counter++;
        }
        ++g_counter;
    }
}

void Consumer(Queue* queue)
{
    uint64_t counter = 0;
    uint64_t recv_counter = 0;

    uint64_t& consumer_counter = queue->GetCustomCounter(0);

    while (!g_is_start);

    while (1)
    {
        uint32_t backoff_counter = 128;
        while (queue->Pop(recv_counter) != adk::ErrorCode::kSuccess)
        {
            PAUSE_BACKOFF();
            consumer_counter++;
        }

        if (ADK_UNLIKELY(counter != recv_counter))
        {
            std::cout << "BUG ON!" << std::endl;
        }

        ++counter;
    }
}

void Obverser(Queue* producer_queue, Queue* cosumer_queue)
{
    uint64_t& producer_counter = producer_queue->GetCustomCounter(0);
    uint64_t& consumer_counter = cosumer_queue->GetCustomCounter(0);

    producer_counter = 0;
    consumer_counter = 0;
    while (!g_is_start);

    uint64_t saved_counter = 0;
    while (1)
    {
        uint64_t temp = g_counter;
        std::cout << "counter_diff = " << temp - saved_counter 
                  << ", push_fail = " << producer_counter 
                  << ", pop_fail = " << consumer_counter
                  << std::endl;
        saved_counter = temp;
        producer_counter = 0;
        consumer_counter = 0;
        sleep(1);
    }
}

int main(int argc, char const *argv[])
{
	Queue* queue_producer = Queue::Create("test_spsc", 8192);
    Queue* queue_consumer = queue_producer->Duplicate();

    boost::thread p_thread = boost::thread(boost::bind(Producer, queue_producer));
    boost::thread c_thread = boost::thread(boost::bind(Consumer, queue_consumer));
    boost::thread o_thread = boost::thread(boost::bind(Obverser, queue_producer, queue_consumer));
    
    sleep(1);
    g_is_start = true;
    o_thread.join();
    return 0;
}