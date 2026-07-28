#include <adk/thread.h>

#include <iostream>
#include <iomanip>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

struct UserData
{};

class ABC
{
public:
    void OneShot1(adk::TimerHandler& hdl, UserData* user)
    {
        std::cout << "thread " << boost::this_thread::get_id() << ", Success: " << "hello world 1" << std::endl;
        adk::TimerManager::ModifyTimer(hdl, adk::thread::Milliseconds(1000));
    }

    void OneShot2(adk::TimerHandler& hdl, UserData* user)
    {
        std::cout << "thread " << boost::this_thread::get_id() << ", Success: " << "hello world 2" << std::endl;
        adk::TimerManager::ModifyTimer(hdl, adk::thread::Milliseconds(2000));
    }

    void OneShot4(adk::TimerHandler& hdl, UserData* user)
    {
        std::cout << "thread " << boost::this_thread::get_id() << ", Success: " << "hello world 4" << std::endl;
        adk::TimerManager::ModifyTimer(hdl, adk::thread::Milliseconds(4000));
    }

    void OneShot8(adk::TimerHandler& hdl, UserData* user)
    {
        std::cout << "thread " << boost::this_thread::get_id() << ", Success: " << "hello world 8" << std::endl;
        adk::TimerManager::ModifyTimer(hdl, adk::thread::Milliseconds(8000));
    }

    bool Init()
    {
        auto hdl1 = adk::TimerManager::CreateTimer(adk::TimerType::kOneShot,
                                                   boost::bind(&ABC::OneShot1, this, _1, nullptr));

        auto hdl2 = adk::TimerManager::CreateTimer(adk::TimerType::kOneShot,
                                                   boost::bind(&ABC::OneShot2, this, _1, nullptr));

        auto hdl3 = adk::TimerManager::CreateTimer(adk::TimerType::kOneShot,
                                                   boost::bind(&ABC::OneShot4, this, _1, nullptr));

        auto hdl4 = adk::TimerManager::CreateTimer(adk::TimerType::kOneShot,
                                                   boost::bind(&ABC::OneShot8, this, _1, nullptr));

        adk::TimerManager::ModifyTimer(hdl1, adk::thread::Milliseconds(1000));
        adk::TimerManager::ModifyTimer(hdl2, adk::thread::Milliseconds(2000));
        adk::TimerManager::ModifyTimer(hdl3, adk::thread::Milliseconds(4000));
        adk::TimerManager::ModifyTimer(hdl4, adk::thread::Milliseconds(8000));
        return true;
    }
};

int main(int argc, char const *argv[])
{
    ABC abc;
    abc.Init();
    sleep(100);
    adk::TimerManager::Finish();
    return 0;
}


