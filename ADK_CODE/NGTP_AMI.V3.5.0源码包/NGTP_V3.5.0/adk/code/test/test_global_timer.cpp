#include <adk/thread.h>

#include <iostream>
#include <iomanip>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

void Greeting(adk::TimerHandler& hdl)
{
    std::cout << "Success: " << "hello world!" << std::endl;
}

void SimPeriodTimer(adk::TimerHandler& hdl, adk::ThreadTimerManager* ttm)
{
    std::cout << "Success: " << __FUNCTION__ << std::endl;   
    ttm->ModifyTimer(hdl, adk::thread::Milliseconds(1000), adk::TimerOffset::kLast);
}


int main(int argc, char const *argv[])
{
    adk::ThreadTimerManager ttm;
    ttm.Start();

    auto timer_hdl = ttm.CreateTimer(adk::TimerType::kPeriod, 
                                     boost::bind(Greeting, _1),
                                     adk::thread::Milliseconds(1000));

    auto timer_hdl2 = ttm.CreateTimer(adk::TimerType::kOneShot, 
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
    return 0;
}


