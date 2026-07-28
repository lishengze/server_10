#include <boost/thread.hpp>
#include <adk/lock_free_queue_variant.h>

using namespace adk::variant;

#define MAX_THREAD_NUM 16

void Thread(SPSCQueue<uint64_t>* queue, uint32_t thread_index)
{
    SPSCQueue<uint64_t>* local_queue = queue->Duplicate();
    
    sleep(thread_index);

    SPSCQueue<uint64_t>::Delete(local_queue);
    
    sleep(1);
}

int main()
{
    SPSCQueue<uint64_t>* queue = SPSCQueue<uint64_t>::Create("delete function test", 1024);

    boost::thread thread_handle[MAX_THREAD_NUM];

    for (uint32_t index=0; index<MAX_THREAD_NUM; ++index)
    {
        thread_handle[index] = boost::thread(boost::bind(Thread, queue, index));
    }

    thread_handle[MAX_THREAD_NUM - 1].join();

    sleep(1);

    SPSCQueue<uint64_t>::Delete(queue);

    sleep(1);
    return 0;
}