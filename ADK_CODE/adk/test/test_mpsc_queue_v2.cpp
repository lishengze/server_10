#include <boost/thread/thread.hpp>
#include <adk/lock_free_msg_queue.h>
#include <adk/error_code.h>
#include <assert.h>
#include <stdint.h>
#include <iostream>

using namespace adk;

void Consumer(adk::MPSCQueue* mq)
{
    uint64_t counter = 0;
    adk::Entry* entries[2048];
    uint64_t sqn = 0;
    while (1)
    {
        sleep(1);
        counter = 0;
        adk::Entry* entry;
        while (mq->WaitEntry(&entry) == ErrorCode::kSuccess)
        {
            ++sqn;
            uint64_t recv_sqn = *(uint64_t*)(entry->buffer);
            if (recv_sqn != sqn)
            {
                std::cout << "bug on!" << std::endl;
                sleep(1000);
            }

            entries[counter] = entry;
            ++counter;
        }

        std::cout << "sqn = " << sqn << std::endl;

        for (uint32_t i = 0; i < counter; ++i)
        {
            int32_t ec = mq->FreeEntry(entries[i]);
            assert(ec == ErrorCode::kSuccess);
        }
    }
}

void Producer(adk::MPSCQueue* mq)
{
    uint64_t sqn = 0;
    while (1)
    {
        ++sqn;
        while (mq->Push(sqn) != ErrorCode::kSuccess);
    }
}


int main(int argc, char const *argv[])
{
    adk::MPSCQueue* mq = adk::MPSCQueue::Create("test", sizeof(uint64_t), 1024);
    boost::thread p_thread = boost::thread(boost::bind(Producer, mq));
    boost::thread c_thread = boost::thread(boost::bind(Consumer, mq));

    c_thread.join();
    return 0;
}