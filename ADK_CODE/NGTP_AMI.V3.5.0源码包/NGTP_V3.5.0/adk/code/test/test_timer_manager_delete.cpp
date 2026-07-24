#include <adk/thread.h>

#include <iostream>
#include <iomanip>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

std::string test_case = "delete";

void Greeting(adk::TimerHandler& hdl)
{
    if (test_case == "cancel_failed")
        sleep(1);
    std::cout << "thread " << boost::this_thread::get_id() << ", Success: " << "hello world!" << std::endl;
}

void SimPeriodTimer(adk::TimerHandler& hdl)
{
    std::cout << "thread " << boost::this_thread::get_id() << ", Success: " << __FUNCTION__ << std::endl;   
    adk::TimerManager::ModifyTimer(hdl, adk::thread::Milliseconds(1000), adk::TimerOffset::kLast);
}

ADK_DEFINE_THREAD(MyThread, "this is my thread")
{};

ADK_REGISTER_THREAD_BEGIN()

    (ADK_THREAD_CLASS(MyThread), 
                adk::thread::InstanceNumber = 1)
    
ADK_REGISTER_THREAD_END()

int main(int argc, char const *argv[])
{
    if (argc != 1)
    {
        test_case = argv[1];
    }

    std::cout << "main thread " << boost::this_thread::get_id() << std::endl;

    auto& thr_mana = *adk::ThreadManager::Instance();
    thr_mana.Start();

    auto timer_hdl = adk::TimerManager::CreateTimer<MyThread>(
                                                adk::TimerType::kPeriod, 
                                                boost::bind(Greeting, _1),
                                                adk::thread::Milliseconds(1000));

    auto timer_hdl2 = adk::TimerManager::CreateTimer<MyThread>(
                                                adk::TimerType::kOneShot, 
                                                boost::bind(SimPeriodTimer, _1));
    adk::TimerManager::ModifyTimer(timer_hdl2, adk::thread::Milliseconds(1000));

    uint32_t i = 0u;
    while (++i <= 6)
    {
        sleep(1);
    }

    if (test_case == "delete")
    {
        adk::TimerManager::DeleteTimer(timer_hdl);
        adk::TimerManager::DeleteTimer(timer_hdl2);    
    }
    else if (test_case == "sync_cancel")
    {
        adk::TimerManager::SyncCancelTimer(timer_hdl);
        adk::TimerManager::SyncCancelTimer(timer_hdl2);
    }
    else if (test_case == "cancel" || test_case == "cancel_failed")
    {
        adk::TimerManager::CancelTimer(timer_hdl);
        adk::TimerManager::CancelTimer(timer_hdl2);
    }

    i = 0u;
    while (++i <= 4)
    {
        sleep(1);
        std::cout << "timer was deleted" << std::endl;
    }

    adk::TimerManager::Finish();
    thr_mana.Finish();
    return 0;
}


