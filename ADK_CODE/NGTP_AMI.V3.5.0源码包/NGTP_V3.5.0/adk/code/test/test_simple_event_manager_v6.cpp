
#include <iostream>

#include <adk/lock_free_msg_queue.h>
#include <adk/error_code.h>
#include <adk/arch/generic.h>
#include <adk/event.h>

#include <boost/thread/thread.hpp>

using namespace adk;

void ProducerThreadMain(SPSCQueue<uint64_t>* mq, SimpleEventManager* ev_man)
{
    uint64_t local_counter = 0;
    while (ev_man->TryNotify([mq, local_counter](){
                return mq->Push(local_counter);
            }) == adk::ErrorCode::kSuccess);

    std::cout << "TryNotify failed" << std::endl;
    assert(ev_man->Notify([mq, local_counter](){
                return mq->Push(local_counter);
            }) == adk::ErrorCode::kWouldblock);
}

int main(int argc, char const *argv[])
{

    SPSCQueue<uint64_t>* mq_p = SPSCQueue<uint64_t>::Create("test_spsc", 8192);
    SPSCQueue<uint64_t>* mq_c = SPSCQueue<uint64_t>::Duplicate(*mq_p);

    SimpleEventManager ev_man;
    boost::thread consumer = boost::thread(boost::bind(ProducerThreadMain, mq_c, &ev_man));

    sleep(2);
    
    ev_man.ReleaseWaitThread();
    consumer.join();
    return 0;
}
