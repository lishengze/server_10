
#include <iostream>

#include <adk/lock_free_msg_queue.h>
#include <adk/error_code.h>
#include <adk/arch/generic.h>
#include <adk/event.h>

#include <boost/thread/thread.hpp>

using namespace adk;

void ConsumerThreadMain(SPSCQueue<struct timeval>* mq, SimpleEventManager* ev_man)
{
    struct timeval begin, end;
    if (ev_man->Wait([mq, &begin](){
                    // int32_t ec = mq->Pop(begin);
                    // if (ec == adk::ErrorCode::kSuccess)
                    //     return adk::ErrorCode::kSuccess;
                    // return static_cast<adk::ErrorCode>(ec);
                    return mq->Pop(begin);
                }) == adk::ErrorCode::kSuccess)
    {
        gettimeofday(&end, NULL);
        std::cout << "wakeup time used = " << (end.tv_sec - begin.tv_sec) * 1000000 + end.tv_usec - begin.tv_usec << std::endl;
    }
}

int main(int argc, char const *argv[])
{

    SPSCQueue<struct timeval>* mq_p = SPSCQueue<struct timeval>::Create("test_spsc", 8192);
    SPSCQueue<struct timeval>* mq_c = SPSCQueue<struct timeval>::Duplicate(*mq_p);

    SimpleEventManager ev_man(100000ul, 3);
    boost::thread consumer = boost::thread(boost::bind(ConsumerThreadMain, mq_c, &ev_man));

    sleep(2);
    
    ev_man.ReleaseWaitThread();
    consumer.join();
    return 0;
}
