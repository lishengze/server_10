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
        static int64_t counter = 0;
        ++counter;
        std::cout << "thread " << boost::this_thread::get_id() << ", Success: " << class_id  <<
                     ", counter = " << counter << std::endl;
        // 发起连接
        // 连接失败
        is_finished_ = true;
    }

    bool Init(int32_t id)
    {
        hdl1_ = adk::TimerManager::CreateTimer(adk::TimerType::kOneShot,
                                                   boost::bind(&ABC::OneShot1, this, _1, nullptr));        
        class_id = id;
        return true;
    }

    void ResetTimer()
    {
        int32_t ret = adk::TimerManager::ModifyTimer(hdl1_, adk::thread::Milliseconds(10));
        assert(ret == adk::ErrorCode::kSuccess);
        is_finished_ = false;
    }

    adk::TimerHandler hdl1_;
    int32_t class_id = 0;
    volatile bool is_finished_ = true;
};

int main(int argc, char const *argv[])
{
    ABC abc1, abc2;
    abc1.Init(1);
    abc2.Init(2);

    while (1)
    {
        if (abc1.is_finished_)
            abc1.ResetTimer();
        if (abc2.is_finished_)
            abc2.ResetTimer();
        usleep(5000);
    }

    adk::TimerManager::Finish();
    return 0;
}


