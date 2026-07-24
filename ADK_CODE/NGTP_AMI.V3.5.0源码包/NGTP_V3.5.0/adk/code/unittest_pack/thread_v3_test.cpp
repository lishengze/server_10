#define BOOST_TEST_MODULE thread_v3
#include <boost/test/included/unit_test.hpp>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <thread>
#include <bitset>

#include <adk_pack/error_code.h>
#include <adk_pack/thread.h>
#include <adk_pack/property.h>

std::mutex g_log_mutex;
#define LOG_MSG(msg) do {    \
    std::lock_guard<std::mutex> lock_guard(g_log_mutex);  \
    std::cout << std::left << std::setw(40) << __PRETTY_FUNCTION__ << " "     \
              << std::this_thread::get_id() << " | "    \
              << msg << std::endl; \
} while (false)

ADK_THREAD_MESSAGE(Task)
{
public:
    int32_t value() { return value_; }
    void set_value(int32_t arg) { value_ = arg; }

    int32_t block_step() { return block_step_; }
    void set_block_step(int32_t arg) { block_step_ = arg; }

private:
    int32_t value_;
    // block_step_ 表示消息阻塞的步长  每 block_step_ 个消息阻塞
    int32_t block_step_;
};


ADK_DEFINE_THREAD(TestThread, "TestThread")
{
public:
    int32_t OnInit()
    {
        memset(block_point_, 0x00, sizeof(block_point_));
        memset(count_sqn_, 0x00, sizeof(count_sqn_));

        LOG_MSG("");
        return adk::ErrorCode::kSuccess;
    }

    int32_t OnInitOnce()
    {
        LOG_MSG("");
        return adk::ErrorCode::kSuccess;
    }

    ADK_DEFINE_MESSAGE_HANDLER(
        (OnMessage, Task)
    )

    int32_t block_point_[ADK_THREAD_MAX_MESSAGE_TAG];
    int32_t count_sqn_[ADK_THREAD_MAX_MESSAGE_TAG];
};

void TestThread::OnMessage(Task* task)
{
    //LOG_MSG(task->value() << ", " << task->message_tag());

    // 第一次处理该消息 && 这个消息满足阻塞条件
    if (block_point_[task->message_tag()] != task->value()
        && (task->value() % task->block_step()) == 0)
    {
        block_point_[task->message_tag()] = task->value();
        BlockMessageProcess(task->message_tag());
        return;
    }

    // value 的值不连续 结束
    if (count_sqn_[task->message_tag()] + 1 != task->value())
    {
        // 抛异常退出
        abort();
    }
    ++count_sqn_[task->message_tag()];
};


ADK_REGISTER_THREAD_BEGIN()

(ADK_THREAD_CLASS(TestThread),
    adk::thread::EventMode = adk::thread::kInterrupt,
    adk::thread::InstanceNumber = 1,
    adk::thread::BusyPollNano = adk::thread::Microseconds(200))

ADK_REGISTER_THREAD_END();
/**************************************************************/

void GenTask(uint64_t class_id, int32_t block_step)
{
    for (int32_t i = 1; i < 128; ++i)
    {
        auto tmsg1 = Task::New();
        tmsg1->set_value(i);
        tmsg1->set_message_tag(class_id);
        tmsg1->set_block_step(block_step);
        adk::SendMsg<TestThread>(tmsg1);

        usleep(50000);

        if (i % ((block_step + 7) << 1) == 0)
        {
            //LOG_MSG("Release, ClassId = " << class_id << " i = " << i);
            adk::ReleaseMessageProcess<TestThread>(class_id);         
        }
    }
}

BOOST_AUTO_TEST_SUITE(ShareThread)
BOOST_AUTO_TEST_CASE(Share)
{

    adk::ThreadManager* thr_mana = adk::ThreadManager::Instance();

    thr_mana->Start();
    // 128 / ((block_step + 7) << 1)  + 1 = 6  => 30
    std::thread thread1 = std::thread(GenTask, 1, 5);
    // 128 / ((block_step + 7) << 1)  + 1 = 4  => 48
    std::thread thread2 = std::thread(GenTask, 10, 12);

    // 128 / ((block_step + 7) << 1)  + 1 = 4  => 48
    std::thread thread3 = std::thread(GenTask, 65, 5);     //message tag = 1+64
    
    std::thread thread4 = std::thread(GenTask, 1000, 12);     //message tag = 10+64

    thread1.join();
    thread2.join();
    thread3.join();
    thread4.join();
    thr_mana->Finish();
    adk::ThreadBase* thread = thr_mana->ThreadInstance<TestThread>();
    LOG_MSG("tag <1> counter = " << ((TestThread*)thread)->count_sqn_[1]);
    LOG_MSG("tag <10> counter = " << ((TestThread*)thread)->count_sqn_[10]);
    LOG_MSG("tag <65> counter = " << ((TestThread*)thread)->count_sqn_[65]);
    LOG_MSG("tag <1000> counter = " << ((TestThread*)thread)->count_sqn_[1000]);
    // 30 - 1
    BOOST_REQUIRE(29 == ((TestThread*)thread)->count_sqn_[1]);
    // 48 - 1
    BOOST_REQUIRE(47 == ((TestThread*)thread)->count_sqn_[10]);
    // 30 - 1
    BOOST_REQUIRE(29 == ((TestThread*)thread)->count_sqn_[65]);
     // 48 - 1
    BOOST_REQUIRE(47 == ((TestThread*)thread)->count_sqn_[1000]);

    thread->Stop();
}

BOOST_AUTO_TEST_SUITE_END();
