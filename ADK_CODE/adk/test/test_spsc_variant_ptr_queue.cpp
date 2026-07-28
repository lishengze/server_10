#include <boost/thread/thread.hpp>
#include <adk/lock_free_queue_variant.h>

using namespace adk::variant;

volatile bool g_is_start = false;

uint64_t g_counter1 = 0;
uint64_t g_counter2 = 0;
uint64_t g_counter3 = 0;

#define TEST_PTRQUEUE_ARRAY 1

typedef SPSCPtrQueue Queue;
//typedef MPSCPtrQueue Queue;
//typedef SPMCPtrQueue Queue;

void Producer(Queue* queue)
{
    Queue* producer_queue = (Queue*)queue->Duplicate();

    while (!g_is_start);

    while (1)
    {
        DEFINE_BACKOFF_COUNTER();
        while (producer_queue->Push(reinterpret_cast<uint64_t*>(g_counter1)) != adk::ErrorCode::kSuccess)
        {
            ++g_counter2;
            PAUSE_BACKOFF();
        }

        ++g_counter1;
    }
}

void ProducerArray(Queue* queue)
{
    uint32_t size_limit = queue->GetEntrySizeLimit();
    uint32_t push_num;
    uint64_t* push_buffer[size_limit];

    Queue* producer_queue = (Queue*)queue->Duplicate();

    while (!g_is_start);

    while (1)
    {
        for (uint32_t index=0; index<size_limit; ++index)
        {
            push_buffer[index] = reinterpret_cast<uint64_t*>(g_counter1++);
        }

        DEFINE_BACKOFF_COUNTER();
        while (producer_queue->Push(push_buffer, size_limit, push_num) != adk::ErrorCode::kSuccess)
        {
            ++g_counter2;
            PAUSE_BACKOFF();
        }
    }
}

void Consumer(Queue* queue)
{
    Queue* consumer_queue = (Queue*)queue->Duplicate();

    uint64_t recv_counter = 0;
    uint64_t* recv_ptr;

    while (!g_is_start);

    while (1)
    {
        DEFINE_BACKOFF_COUNTER();
        while (consumer_queue->Pop(&recv_ptr) != adk::ErrorCode::kSuccess)
        {
            ++g_counter3;
            PAUSE_BACKOFF();
        }

        if (ADK_UNLIKELY(reinterpret_cast<uint64_t>(recv_ptr) != recv_counter++))
        {
            std::cout << "BUG ON!" << std::endl;
        }
    }
}

void ConsumerArray(Queue* queue)
{
    uint32_t size_limit = queue->GetEntrySizeLimit();
    uint32_t pop_num;
    uint64_t* pop_buffer[size_limit];

    Queue* consumer_queue = (Queue*)queue->Duplicate();

    while (!g_is_start);

    uint64_t recv_counter = 0;

    while (1)
    {
        DEFINE_BACKOFF_COUNTER();
        while (consumer_queue->Pop(pop_buffer, pop_num) != adk::ErrorCode::kSuccess)
        {
            ++g_counter3;
            PAUSE_BACKOFF();
        }

        if (ADK_UNLIKELY(size_limit != pop_num))
        {
            std::cout << "Consumer BUG ON!" << std::endl;
        }

        for (uint32_t index=0; index<size_limit; ++index)
        {
            if (ADK_UNLIKELY(reinterpret_cast<uint64_t>(pop_buffer[index]) != recv_counter++))
            {
                std::cout << "BUG ON!" << std::endl;
            }
        }
    }
}

void Obverser()
{
    uint64_t saved_counter = 0;
    while (!g_is_start);
    while (1)
    {
        uint64_t temp = g_counter1;
        std::cout << "counter_diff = " << temp - saved_counter 
                  << ", push_fail_time = " << g_counter2
                  << ", pop_fail_time = " << g_counter3 << std::endl;
        saved_counter = temp;
        g_counter2 = 0;
        g_counter3 = 0;
        sleep(1);
    }
}

int main(int argc, char const *argv[])
{
	Queue* queue = Queue::Create("test_ptr_spsc", 8192);

    #if TEST_PTRQUEUE_ARRAY
    boost::thread p_thread = boost::thread(boost::bind(ProducerArray, queue));
    boost::thread c_thread = boost::thread(boost::bind(ConsumerArray, queue));
    #else
    boost::thread p_thread = boost::thread(boost::bind(Producer, queue));
    boost::thread c_thread = boost::thread(boost::bind(Consumer, queue));
    #endif

    boost::thread o_thread = boost::thread(Obverser);
    
    sleep(1);
    g_is_start = true;
    p_thread.join();
    return 0;
}