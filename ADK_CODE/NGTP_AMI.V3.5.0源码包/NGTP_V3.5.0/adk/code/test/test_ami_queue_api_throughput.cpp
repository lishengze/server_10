#include <boost/thread/thread.hpp>

#include "adk.h"

#ifdef SPSC_TEST
typedef SPSCQueue<uint64_t> Queue;
std::string g_title_queue = "spsc_variant_queue_speed_test";
uint32_t g_producer_num = 1;
uint32_t g_consumer_num = 1;
#define CreateQueue() Queue::Create(g_title_queue, 8192) 
#define DuplicateQueue(queue) (queue)->Duplicate()
#define DeleteQueue(queue) Queue::Delete(queue)
#endif

#ifdef MPSC_TEST
typedef MPSCQueue<uint64_t> Queue;
std::string g_title_queue = "mpsc_variant_queue_speed_test";
uint32_t g_producer_num = 2;
uint32_t g_consumer_num = 1;
#define CreateQueue() Queue::Create(g_title_queue, 8192)
#define DuplicateQueue(queue) (queue)->Duplicate()
#define DeleteQueue(queue) Queue::Delete(queue)
#endif

#ifdef SPMC_TEST
typedef SPMCQueue<uint64_t> Queue;
std::string g_title_queue = "spmc_variant_queue_speed_test";
uint32_t g_producer_num = 1;
uint32_t g_consumer_num = 4;
#define CreateQueue() Queue::Create(g_title_queue, 8192)
#define DuplicateQueue(queue) (queue)->Duplicate()
#define DeleteQueue(queue) Queue::Delete(queue)
#endif

#ifdef MPMC_TEST
typedef MPMCQueue<uint64_t> Queue;
std::string g_title_queue = "mpmc_variant_queue_speed_test";
uint32_t g_producer_num = 4;
uint32_t g_consumer_num = 4;
#define CreateQueue() Queue::Create(g_title_queue, 8192)
#define DuplicateQueue(queue) (queue)->Duplicate()
#define DeleteQueue(queue) Queue::Delete(queue)
#endif

#ifdef UNBOUNDED_SPSC
typedef SPSCUnboundedQueue<uint64_t> Queue;
std::string g_title_queue = "spsc_variant_unbounded_queue_speed_test";
uint32_t g_producer_num = 1;
uint32_t g_consumer_num = 1;
#define CreateQueue() Queue::Create(g_title_queue, 8192)
#define DuplicateQueue(queue) (queue)
#define DeleteQueue(queue) Queue::Delete(queue)
#endif

#ifdef UNBOUNDED_MPSC
typedef MPSCUnboundedQueue<uint64_t> Queue;
std::string g_title_queue = "mpsc_variant_unbounded_queue_speed_test";
uint32_t g_producer_num = 2;
uint32_t g_consumer_num = 1;
#define CreateQueue() Queue::Create(g_title_queue, 8192)
#define DuplicateQueue(queue) (queue)
#define DeleteQueue(queue) Queue::Delete(queue)
#endif

volatile bool g_is_start = false;
volatile bool g_is_running = true;

void Producer(Queue* queue, uint64_t** counter1_ptr)
{
    uint64_t value = 0;
    uint64_t counter1 = 0;
    *counter1_ptr = &counter1;

    Queue* producer_thread = DuplicateQueue(queue);

    while(!g_is_start);

    uint32_t backoff_counter = 32;
    while(g_is_running)
    {
        ++value;
        while(ADK_UNLIKELY(adk::ErrorCode::kSuccess != producer_thread->Push(value)))
        {
            ++counter1;
            PAUSE_BACKOFF();
        }
    }
}

void Consumer(Queue* queue, uint64_t** counter2_ptr, uint64_t** counter3_ptr)
{
    uint64_t value = 0;
    uint64_t counter2 = 0;
    uint64_t counter3 = 0;
    *counter2_ptr = &counter2;
    *counter3_ptr = &counter3;

    Queue* consumer_queue = DuplicateQueue(queue);

    while(!g_is_start);

    uint32_t backoff_counter = 389;
    while(g_is_running)
    {
        ++counter3;
        while(ADK_UNLIKELY(adk::ErrorCode::kSuccess != consumer_queue->Pop(value)))
        {
            ++counter2;
            PAUSE_BACKOFF();
        }
    }
}

int main()
{
    std::cout << "---" << g_title_queue << "---"
              << ", producer_count = " << g_producer_num
              << ", consumer_count = " << g_consumer_num << std::endl;

    boost::thread producer_thread[g_producer_num];
    boost::thread consumer_thread[g_consumer_num];
    uint64_t* counter1[g_producer_num];
    uint64_t* counter2[g_consumer_num];
    uint64_t* counter3[g_consumer_num];

    Queue* queue = CreateQueue();
    for (uint32_t index=0; index<g_producer_num; ++index)
    {
        counter1[index] = nullptr;
        producer_thread[index] = boost::thread(boost::bind(Producer, queue, &counter1[index]));

        while (nullptr == *(volatile uint64_t**)(&counter1[index]))
        {
            ADK_PAUSE();
        }
    }

    for (uint32_t index=0; index<g_consumer_num; ++index)
    {
        counter2[index] = nullptr;
        counter3[index] = nullptr;
        consumer_thread[index] = boost::thread(boost::bind(Consumer, queue, &counter2[index], &counter3[index]));
        while (nullptr == *(volatile uint64_t**)(&counter2[index]))
        {
            ADK_PAUSE();
        }

        while (nullptr == *(volatile uint64_t**)(&counter3[index]))
        {
            ADK_PAUSE();
        }
    }

    g_is_start = true;

    uint64_t save_counter1 = 0;
    uint64_t save_counter2 = 0;
    uint64_t save_counter3 = 0;
    for (uint32_t index = 0; index < 30; ++index)
    {
        sleep(1);

        uint64_t temp_counter1 = 0;
        for (uint32_t index = 0; index<g_producer_num; ++index)
        {
            temp_counter1 += *counter1[index];
        }

        uint64_t temp_counter2 = 0;
        uint64_t temp_counter3 = 0;
        for (uint32_t index = 0; index < g_consumer_num; ++index)
        {
            temp_counter2 += *counter2[index];
            temp_counter3 += *counter3[index];
        }

        std::cout << "pop_diff = " << temp_counter3 - save_counter3
            << ", push_fail = " << temp_counter1 - save_counter1
            << ", pop_fail = " << temp_counter2 - save_counter2
            << std::endl;

        save_counter1 = temp_counter1;
        save_counter2 = temp_counter2;
        save_counter3 = temp_counter3;
    }

    g_is_running = false;
    for (uint32_t index=0; index<g_producer_num; ++index)
    {
        producer_thread[index].join();
    }

    for (uint32_t index=0; index<g_consumer_num; ++index)
    {
        consumer_thread[index].join();
    }

    DeleteQueue(queue);
    return 0;
}