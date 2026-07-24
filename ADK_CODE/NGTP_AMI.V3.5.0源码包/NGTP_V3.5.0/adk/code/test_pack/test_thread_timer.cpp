#include <adk_pack/thread.h>

#include <iostream>
#include <iomanip>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

void Greeting(adk::TimerHandler& hdl)
{
    std::cout << "thread " << boost::this_thread::get_id() << ", Success: " << "hello world!" << std::endl;
}

void SimPeriodTimer(adk::TimerHandler& hdl, adk::ThreadTimerManager* ttm)
{
    std::cout << "thread " << boost::this_thread::get_id() << ", Success: " << __FUNCTION__ << std::endl;   
    ttm->ModifyTimer(hdl, adk::thread::Milliseconds(1000), adk::TimerOffset::kLast);
}

ADK_DEFINE_THREAD(MyThread, "this is my thread")
{};

ADK_REGISTER_THREAD_BEGIN()

    (ADK_THREAD_CLASS(MyThread), 
                adk::thread::InstanceNumber = 1)
    
ADK_REGISTER_THREAD_END()

int main(int argc, char const *argv[])
{
    std::cout << "main thread " << boost::this_thread::get_id() << std::endl;

    auto& thr_mana = *adk::ThreadManager::Instance();
    thr_mana.Start();

    adk::ThreadTimerManager ttm;
    ttm.Start();

    // 
    auto timer_hdl = ttm.CreateTimer<MyThread>(adk::TimerType::kPeriod, 
                                               boost::bind(Greeting, _1),
                                               adk::thread::Milliseconds(1000));

    auto timer_hdl2 = ttm.CreateTimer<MyThread>(adk::TimerType::kOneShot, 
                                                boost::bind(SimPeriodTimer, _1, &ttm));
    ttm.ModifyTimer(timer_hdl2, adk::thread::Milliseconds(1000));

    uint32_t i = 0u;
    while (++i <= 10)
    {
        usleep(100000);
        ttm.ModifyTimer(timer_hdl2, adk::thread::Milliseconds(100), adk::TimerOffset::kLast);
    }

    sleep(10);

    ttm.Finish();
    thr_mana.Finish();
    return 0;
}


