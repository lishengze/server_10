#include <boost/thread/thread.hpp>
#include <adk/lock_free_queue_variant.h>

#define VARIANT_TEST 1

#if VARIANT_TEST
using namespace adk::variant;
typedef MPMCQueue<uint64_t> Queue;
#else
using namespace adk;
typedef SPMCQueue Queue;
#endif

const uint32_t test_thread_num = 1;
const uint32_t test_thread_mask = test_thread_num - 1;

const uint32_t queue_size = 8192;

volatile bool g_is_start = false;
uint64_t g_push_counter = 0;
uint64_t pop_array_counter[test_thread_num];
uint64_t g_counter1 = 0;
uint64_t g_counter2 = 0;

void Producer(Queue* queue)
{
    Queue* producer_queue = queue->Duplicate(*queue);
    while(!g_is_start);
    while(1)
    {
        DEFINE_BACKOFF_COUNTER();
        while(ADK_UNLIKELY(adk::ErrorCode::kSuccess != producer_queue->Push(g_push_counter)))
        {
            ++g_counter1;
            PAUSE_BACKOFF();
        }
        ++g_push_counter;
    }
}

void Consumer(uint32_t thread_index , Queue* queue)
{
    uint64_t pop_context;
    uint64_t local_counter = 0;
    Queue* consumer_queue = queue->Duplicate(*queue);
    pop_array_counter[thread_index] = 0;

    while(!g_is_start);

    while(1)
    {
        DEFINE_BACKOFF_COUNTER();
        while(ADK_UNLIKELY(adk::ErrorCode::kSuccess != consumer_queue->Pop(pop_context)))
        {
            #if VARIANT_TEST
            std::cout << "BUG ON! Pop failed~" << std::endl;
            #else
            ++g_counter2;
            PAUSE_BACKOFF();
            #endif
        }

        /*
        while(ADK_UNLIKELY(adk::ErrorCode::kSuccess != consumer_queue->TryPop(pop_context)))
        {
            ++g_counter2;
            PAUSE_BACKOFF();
        }*/

        if (ADK_UNLIKELY(pop_context < local_counter))
        {
            std::cout << "BUG ON!"
                      << ", pop_context = " << pop_context
                      << ", local_counter = " << local_counter << std::endl;
        }
        local_counter = pop_context;
        ++pop_array_counter[thread_index];
    }
}

void Obverser()
{
    uint64_t push_counter = 0;
    uint64_t pop_counter = 0;

    uint64_t save_counter = 0;

    while(!g_is_start);

    while(1)
    {
        push_counter = g_push_counter;

        ADK_BARRIER();
        pop_counter = 0;
        for (uint32_t index=0; index<test_thread_num; ++index)
        {
            pop_counter += pop_array_counter[index];
        }

        if (ADK_UNLIKELY(pop_counter + queue_size < push_counter))
        {
            std::cout << pop_counter + queue_size << " < " << push_counter << std::endl;
            std::cout << "BUG ON!" << std::endl;
        }

        std::cout << "push_diff = " << push_counter - save_counter
                  << ", push_fail = " << g_counter1
                  #if! VARIANT_TEST
                  << ", pop_fail = " << g_counter2
                  #endif
                  << std::endl;

        g_counter1 = 0;
        g_counter2 = 0;
        save_counter = push_counter;
        sleep(1);
    }
}

int main()
{
    #if VARIANT_TEST
    Queue* queue = Queue::Create("test_spmc", queue_size);
    #else
    Queue* queue = Queue::Create("test_spmc", sizeof(uint64_t), queue_size);
    #endif

    boost::thread producer_thread = boost::thread(boost::bind(Producer, queue));

    boost::thread consumer_thread[test_thread_num];
    for (uint32_t index=0; index<test_thread_num; ++index)
    {
        consumer_thread[index] = boost::thread(boost::bind(Consumer, index, queue));
    }

    boost::thread obverser_thread = boost::thread(boost::bind(Obverser));

    sleep(1);
    g_is_start = true;

    obverser_thread.join();

    return 0;
}