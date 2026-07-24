#include <adk/thread.h>

void PeriodTimer(adk::TimerHandler& hdl)
{
    std::cout << "Success: " << __FUNCTION__ << std::endl;   
}


int main(int argc, char const *argv[])
{
    auto timer_hdl = adk::TimerManager::CreateTimer(adk::TimerType::kPeriod, 
                                                    boost::bind(PeriodTimer, _1),
                                                    adk::thread::Milliseconds(100));

    sleep(3);
    std::cout << "start to cancel timer" << std::endl;

    int32_t ec = adk::TimerManager::CancelTimer(timer_hdl);
    assert(ec == adk::ErrorCode::kSuccess);

    std::cout << "timer cancelled" << std::endl;
    sleep(3);

    std::cout << "start timer again" << std::endl;
    adk::TimerManager::ModifyTimer(timer_hdl,
                                   adk::thread::Milliseconds(100));

    sleep(3);

    std::cout << "start to cancel timer synchronously" << std::endl;

    adk::TimerManager::SyncCancelTimer(timer_hdl);

    sleep(3);

    std::cout << "start timer again" << std::endl;
    adk::TimerManager::ModifyTimer(timer_hdl,
                                   adk::thread::Milliseconds(100));

    sleep(3);

    adk::TimerManager::Finish();
    return 0;
}
