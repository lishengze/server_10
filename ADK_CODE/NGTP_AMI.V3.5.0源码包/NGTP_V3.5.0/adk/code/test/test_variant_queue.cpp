#include <boost/thread/thread.hpp>
#include <adk/lock_free_queue_variant.h>
#include <adk/lock_free_unbounded_queue_variant.h>

#define UNBOUNDED_SPSC
#define VARIANT_TEST 1

#if VARIANT_TEST
using namespace adk::variant;
#else
using namespace adk;
#endif

#ifdef SPSC_TEST
typedef SPSCQueue<uint64_t> Queue;
std::string g_title_queue = "spsc_variant_queue_test";
const uint32_t g_producer_num = 1;
const uint32_t g_consumer_num = 1;
#define CreateQueue() Queue::Create(g_title_queue, 8192) 
#define DuplicateQueue(queue) (queue)->Duplicate()
#endif

#ifdef MPSC_TEST
typedef MPSCQueue<uint64_t> Queue;
std::string g_title_queue = "mpsc_variant_queue_test";
const uint32_t g_producer_num = 4;
const uint32_t g_consumer_num = 1;
#define CreateQueue() Queue::Create(g_title_queue, 8192)
#define DuplicateQueue(queue) (queue)->Duplicate()
#endif

#ifdef SPMC_TEST
typedef SPMCQueue<uint64_t> Queue;
std::string g_title_queue = "spmc_variant_queue_test";
const uint32_t g_producer_num = 1;
const uint32_t g_consumer_num = 4;
#define CreateQueue() Queue::Create(g_title_queue, 8192)
#define DuplicateQueue(queue) (queue)->Duplicate()
#endif

#ifdef MPMC_TEST
typedef MPMCQueue<uint64_t> Queue;
std::string g_title_queue = "mpmc_variant_queue_test";
const uint32_t g_producer_num = 4;
const uint32_t g_consumer_num = 4;
#define CreateQueue() Queue::Create(g_title_queue, 8192)
#define DuplicateQueue(queue) (queue)->Duplicate()
#endif

#ifdef UNBOUNDED_SPSC
typedef SPSCUnboundedQueue<uint64_t> Queue;
std::string g_title_queue = "spsc_variant_unbounded_queue_speed_test";
const uint32_t g_producer_num = 1;
const uint32_t g_consumer_num = 1;
#define CreateQueue() Queue::Create(g_title_queue, 8192)
#define DuplicateQueue(queue) (queue)
#endif

#ifdef UNBOUNDED_MPSC
typedef MPSCUnboundedQueue<uint64_t> Queue;
std::string g_title_queue = "mpsc_variant_unbounded_queue_speed_test";
const uint32_t g_producer_num = 4;
const uint32_t g_consumer_num = 1;
#define CreateQueue() Queue::Create(g_title_queue, 8192)
#define DuplicateQueue(queue) (queue)
#endif

uint64_t g_producer_mask = g_producer_num - 1;
//push fail time counter
uint64_t g_counter1 = 0;
//pop fail time counter
uint64_t g_counter2 = 0;
//pop element counter
uint64_t g_counter3 = 0;
volatile bool g_is_start = false;

uint64_t g_push_counter[g_producer_num];

void Producer(uint64_t thread_index, Queue* queue)
{
    Queue* producer_thread = DuplicateQueue(queue);
    uint64_t counter1 = 0;
    uint64_t& push_context = g_push_counter[thread_index];

    while(!g_is_start);
    while(1)
    {
        DEFINE_BACKOFF_COUNTER();
        while(ADK_UNLIKELY(adk::ErrorCode::kSuccess != producer_thread->Push(push_context)))
        {
            if (ADK_UNLIKELY(++counter1 & 0x800))
            {
                __sync_fetch_and_add(&g_counter1, counter1);
                counter1 = 0;
            }
            else
            {
                PAUSE_BACKOFF();
            }
        }
        push_context += g_producer_num;
    }
}

void Consumer(Queue* queue)
{
    Queue* consumer_queue = DuplicateQueue(queue);
    uint32_t thread_index;
    uint64_t recv_counter;
    uint64_t counter2 = 0;
    uint64_t counter3 = 0;
    uint64_t pop_context;

    uint64_t arr_recv_counter[g_producer_num];
    for (uint32_t index=0; index<g_producer_num; ++index)
    {
        arr_recv_counter[index] = index;
    }

    while(!g_is_start);
    while(1)
    {
        DEFINE_BACKOFF_COUNTER();
        while(ADK_UNLIKELY(adk::ErrorCode::kSuccess != consumer_queue->Pop(pop_context)))
        {
            if (ADK_UNLIKELY(++counter2 & 0x800))
            {
                __sync_fetch_and_add(&g_counter2, counter2);
                counter2 = 0;
            }
            else
            {
                PAUSE_BACKOFF();
            }
        }

        thread_index = pop_context & g_producer_mask;
        recv_counter = arr_recv_counter[thread_index];
        arr_recv_counter[thread_index] += g_producer_num;

        if (ADK_UNLIKELY(recv_counter != pop_context))
        {
            std::cout << "BUG ON!"
                    << ", pop_context = " << pop_context
                    << ", recv_counter[thread_index] = " << recv_counter
                    << std::endl;
        }

        if (ADK_UNLIKELY(++counter3 & 0x8000))
        {
            __sync_fetch_and_add(&g_counter3, counter3);
            counter3 = 0;
        }
    }
}

void Consumers(Queue* queue)
{
    Queue* consumer_queue = DuplicateQueue(queue);
    uint32_t thread_index;
    uint64_t counter2 = 0;
    uint64_t counter3 = 0;
    uint64_t pop_context;

    int64_t recv_counter[g_producer_num];
    for (int32_t index=0; index<(int32_t)g_producer_num; ++index)
    {
        recv_counter[index] = index - 1;
    }

    while(!g_is_start);

    while(1)
    {
        DEFINE_BACKOFF_COUNTER();
        while(ADK_UNLIKELY(adk::ErrorCode::kSuccess != consumer_queue->Pop(pop_context)))
        {
            if (ADK_UNLIKELY(++counter2 & 0x800))
            {
                __sync_fetch_and_add(&g_counter2, counter2);
                counter2 = 0;
            }
            else
            {
                PAUSE_BACKOFF();
            }
        }

        thread_index = pop_context & g_producer_mask;
        if (ADK_UNLIKELY(recv_counter[thread_index] >= (int64_t)pop_context))
        {
            std::cout << "BUG ON!"
                    << " recv_counter[thread_index] = " << recv_counter[thread_index]
                    << ", pop_context = " << pop_context
                    << std::endl;
        }
        recv_counter[thread_index] = pop_context;

        if (ADK_UNLIKELY(++counter3 & 0x8000))
        {
            __sync_fetch_and_add(&g_counter3, counter3);
            counter3 = 0;
        }
    }
}

void Observer()
{
    uint64_t counter1;
    uint64_t counter2;
    uint64_t counter3;
    uint64_t save_counter1 = 0;
    uint64_t save_counter2 = 0;
    uint64_t save_counter3 = 0;

    uint64_t push_counter;
    uint64_t push_diff_pop;

    while(!g_is_start);

    while(1)
    {
        push_counter = 0;
        for (uint32_t index=0; index<g_producer_num; ++index)
        {
            push_counter += (g_push_counter[index] - index)/g_producer_num;
        }

        counter1 = g_counter1;
        counter2 = g_counter2;
        counter3 = g_counter3;
        push_diff_pop = push_counter - counter3;
        std::cout << "pop_diff = " << counter3 - save_counter3
                  << ", push_fail = " << counter1 - save_counter1
                  << ", pop_fail = " << counter2 - save_counter2
                  << ", push-pop = " << push_diff_pop
                  << std::endl;

        if (ADK_UNLIKELY((g_consumer_num > 1)&&(push_diff_pop > 0x8000*g_consumer_num + 8192)))
        {
            std::cout << "queue data loss" << std::endl;
        }

        save_counter1 = counter1;
        save_counter2 = counter2;
        save_counter3 = counter3;
        sleep(1);
    }
}

int main()
{
    std::cout << "---" << g_title_queue << "---"
              << ", producer_count = " << g_producer_num
              << ", consumer_count = " << g_consumer_num << std::endl;

    boost::thread producer_thread[g_producer_num];
    boost::thread consumer_thread[g_consumer_num];

    Queue* queue = CreateQueue();
    for (uint32_t index=0; index<g_producer_num; ++index)
    {
        g_push_counter[index] = index;
        producer_thread[index] = boost::thread(boost::bind(Producer, index, queue));
    }

    if (1 == g_consumer_num)
    {
        consumer_thread[0] = boost::thread(boost::bind(Consumer, queue));
    }
    else
    {
        for (uint32_t index=0; index<g_consumer_num; ++index)
        {
            consumer_thread[index] = boost::thread(boost::bind(Consumers, queue));
        }
    }
 
    boost::thread observer_thread = boost::thread(Observer);

    sleep(1);
    g_is_start = true;

    observer_thread.join();
    return 0;
}