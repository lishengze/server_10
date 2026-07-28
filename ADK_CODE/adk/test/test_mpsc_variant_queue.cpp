#include <boost/thread/thread.hpp>
#include <adk/lock_free_queue_variant.h>
#include <sched.h>

volatile bool g_is_start = false;

#define TEST_THREAD_NUM  2
#define TEST_THREAD_MASK TEST_THREAD_NUM - 1

#define VARIANT_TEST 1

#if VARIANT_TEST
using namespace adk::variant;
typedef MPSCQueue<uint64_t> Queue;
#define Queue_Create Create
#else
using namespace adk;
typedef MPSCQueue Queue;
#define Queue_Create Create<uint64_t>
#endif

void Producer(uint32_t queue_index, Queue* queue)
{
    uint64_t push_context = queue_index;
    //Queue* queue_producer = queue->Duplicate(*queue);
    Queue* queue_producer = queue;
    std::cout << "Thread_" << queue_index << " prepared" << std::endl;
    while (!g_is_start);

    while (1)
    {   
        DEFINE_BACKOFF_COUNTER();

        /*
        while (ADK_UNLIKELY(adk::ErrorCode::kSuccess != queue_producer->TryPush(push_context)))
        {
            PAUSE_BACKOFF();
        }*/
        
        while (ADK_UNLIKELY(adk::ErrorCode::kSuccess != queue_producer->Push(push_context)))
        {
            #if VARIANT_TEST
            std::cout << "push fail" << std::endl;
            #endif
            PAUSE_BACKOFF();
        }
        push_context += TEST_THREAD_NUM;
    }
}

void Consumer(Queue* queue, uint64_t* counter1, uint64_t* counter2)
{
    uint64_t recv_counter[TEST_THREAD_NUM];
    for (uint64_t index=0; index<TEST_THREAD_NUM; ++index)
    {
        recv_counter[index] = index;
    }

    uint64_t recv_index;
    uint64_t consumer_context;
    Queue* queue_consumer = queue->Duplicate(*queue);

    while (!g_is_start);

    while(1)
    {
        DEFINE_BACKOFF_COUNTER();
        while (ADK_UNLIKELY(adk::ErrorCode::kSuccess != queue_consumer->Pop(consumer_context)))
        {
            ++(*counter2);
            PAUSE_BACKOFF();
        }

        ++(*counter1);

        //判断获取到的数据是由 哪个pro线程生成的
        recv_index = ((consumer_context) & (TEST_THREAD_MASK));
        if (ADK_UNLIKELY(consumer_context != recv_counter[recv_index]))
        {
            std::cout << "BUG ON! " << consumer_context << " != " << recv_counter[recv_index] << std::endl;
        }
        recv_counter[recv_index] += TEST_THREAD_NUM;
    }
}

void Obverser(uint64_t* counter1, uint64_t* counter2)
{
    uint64_t saved_counter = 0;

    while (!g_is_start);

    while (1)
    {
        uint64_t temp = *counter1;
        std::cout << "counter_diff = " << temp - saved_counter 
                  << ", pop_fail = " << *counter2 << std::endl;
        saved_counter = temp;
        *counter2 = 0;
        sleep(1);
    }
}

int main()
{
    Queue* queue = Queue::Queue_Create("test_mpsc", 8192);

    boost::thread pro_thread[TEST_THREAD_NUM];
    for (uint32_t thread_index=0; thread_index<TEST_THREAD_NUM; ++thread_index)
    {
        pro_thread[thread_index] = boost::thread(boost::bind(Producer, thread_index, queue));
    }

    uint64_t counter1 __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint64_t counter2 __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    counter1 = 0, counter2 = 0;
    boost::thread consumer_thread = boost::thread(boost::bind(Consumer, queue, &counter1, &counter2));
    boost::thread ob_thread = boost::thread(boost::bind(Obverser, &counter1, &counter2));

    sleep(1);
    g_is_start = true;

    ob_thread.join();

    return 0;
}