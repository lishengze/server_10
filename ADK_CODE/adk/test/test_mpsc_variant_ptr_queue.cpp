#include <boost/thread/thread.hpp>
#include <adk/lock_free_queue_variant.h>

using namespace adk::variant;

typedef MPSCPtrQueue Queue;

volatile bool g_is_start = false;

const uint32_t test_thread_num = 2;
const uint32_t test_thread_mask = test_thread_num - 1;

uint64_t g_counter1 = 0;
uint64_t g_counter2 = 0;
uint64_t g_array_counter[test_thread_num] = {0};

void Producer(uint32_t thread_index, Queue* queue)
{
    Queue* producer_queue = (Queue*)queue->Duplicate();
    uint32_t size_limit = producer_queue->GetEntrySizeLimit();
    uint32_t push_num;
    uint64_t* push_buffer[size_limit];

    uint64_t local_counter = thread_index;

    std::cout << "thread_index =  " << thread_index 
              << ", entry_limit = " << size_limit << std::endl;

    while (!g_is_start);

    while (1)
    {
        for (uint32_t index=0; index<size_limit; ++index)
        {
            push_buffer[index] = reinterpret_cast<uint64_t*>(local_counter);
            local_counter += test_thread_num;
        }

        /*
        DEFINE_BACKOFF_COUNTER();
        while (producer_queue->TryPush(push_buffer, size_limit, push_num) != adk::ErrorCode::kSuccess)
        {
            ++g_array_counter[thread_index];
            PAUSE_BACKOFF();
        }*/

        if (ADK_UNLIKELY(producer_queue->Push(push_buffer, size_limit, push_num)))
        {
            std::cout << "BUG ON!, Push failed! " << std::endl;
        }

        if (ADK_UNLIKELY(push_num != size_limit))
        {
            std::cout << "BUG ON!, push_num = " << push_num << std::endl;
        }
    }
}

void Consumer(Queue* queue)
{
    Queue* consumer_queue = (Queue*)queue->Duplicate();

    uint32_t size_limit = consumer_queue->GetEntrySizeLimit();
    uint32_t pop_num;
    uint64_t* pop_buffer[size_limit];

    uint64_t recv_counter[test_thread_num];
    for (uint32_t index=0; index<test_thread_num; ++index)
    {
        recv_counter[index] = index;
    }

    uint32_t thread_index;

    while (!g_is_start);

    while(1)
    {
        DEFINE_BACKOFF_COUNTER();
        while (consumer_queue->Pop(pop_buffer, pop_num) != adk::ErrorCode::kSuccess)
        {
            ++g_counter2;
            PAUSE_BACKOFF();
        }  

        if (ADK_UNLIKELY(size_limit != pop_num))
        {
            std::cout << "Consumer BUG ON!" << std::endl;
        }

        g_counter1 += pop_num;

        thread_index = reinterpret_cast<uint64_t>(pop_buffer[0]) & test_thread_mask;

        for (uint32_t index=0; index<size_limit; ++index)
        {
            if (ADK_UNLIKELY(reinterpret_cast<uint64_t>(pop_buffer[index]) != recv_counter[thread_index]))
            {
                std::cout << "BUG ON!" 
                          << ", pop_data = " << pop_buffer[index]
                          << ", recv_data = " << recv_counter[thread_index] << std::endl;
            }

            recv_counter[thread_index] += test_thread_num;
        }
    }
}

void Obverser()
{
    uint64_t saved_counter = 0;
    uint64_t push_fail = 0;

    while (!g_is_start);

    while (1)
    {
        for (uint32_t index=0; index<test_thread_num; ++index)
        {
            push_fail += g_array_counter[index];
            g_array_counter[index] = 0;
        }

        uint64_t temp = g_counter1;
        std::cout << "counter_diff = " << temp - saved_counter 
                  << ", push_fail_time = " << push_fail
                  << ", pop_fail_time = " << g_counter2 << std::endl;
        saved_counter = temp;
        g_counter2 = 0;
        push_fail = 0;
        sleep(1);
    }
}

int main()
{
    Queue* queue = Queue::Create("test_mpsc", 8192);

    boost::thread p_thread[test_thread_num];
    for (uint32_t thread_index=0; thread_index<test_thread_num; ++thread_index)
    {
        p_thread[thread_index] = boost::thread(boost::bind(Producer, thread_index, queue));
    }

    boost::thread c_thread = boost::thread(boost::bind(Consumer, queue));
    boost::thread o_thread = boost::thread(Obverser);
    
    sleep(1);
    g_is_start = true;
    o_thread.join();
    return 0;
}