#include <adk/thread.h>

#include <iostream>
#include <iomanip>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

void SimPeriodTimer(adk::TimerHandler& hdl)
{
    std::cout << "Success: " << __FUNCTION__ << std::endl;   
    adk::TimerManager::ModifyTimer(hdl, 
                                   adk::thread::Milliseconds(1000),
                                   adk::TimerOffset::kLast);
}

int main(int argc, char const *argv[])
{
    auto timer_hdl = adk::TimerManager::CreateTimer(adk::TimerType::kPeriod, 
                                                    [](adk::TimerHandler& hdl){
                                                         std::cout << "Success: " << "hello world!" << std::endl;
                                                    },
                                                    adk::thread::Milliseconds(1000));

    auto timer_hdl2 = adk::TimerManager::CreateTimer(adk::TimerType::kOneShot, 
                                                     boost::bind(SimPeriodTimer, _1));
    adk::TimerManager::ModifyTimer(timer_hdl2, adk::thread::Milliseconds(1000));

    uint32_t i = 0u;
    while (++i <= 10)
    {
        usleep(100000);
        adk::TimerManager::ModifyTimer(timer_hdl2, 
                                       adk::thread::Milliseconds(100),
                                       adk::TimerOffset::kLast);
    }

    sleep(10);

    adk::TimerManager::Finish();
    return 0;
}


