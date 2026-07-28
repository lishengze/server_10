#define BOOST_TEST_MODULE thread_v4
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


ADK_THREAD_MESSAGE(Task2)
{
public:
    int32_t value() { return value_; }
    void set_value(int32_t arg) { value_ = arg; }

private:
    int32_t value_;
};

ADK_DEFINE_THREAD(TestThread, "TestThread")
{
public:
    int32_t OnInit()
    {
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
        (OnMessage, Task2)
    )

    int32_t count_sqn_[ADK_THREAD_MAX_MESSAGE_TAG];
};



void TestThread::OnMessage(Task2* task)
{
    // value 的值不连续 结束
    if (count_sqn_[task->message_tag()] + 1 != task->value())
    {
        // 抛异常退出
        LOG_MSG("message sqn is not continue");
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


const uint64_t kTestMsgCount = 1000000;  // 100w

void GenTask2(uint64_t class_id)
{
    for (uint64_t i = 1; i <= kTestMsgCount; ++i)
    {
        auto tmsg1 = Task2::New();
        tmsg1->set_value(i);
        tmsg1->set_message_tag(class_id);
        adk::SendMsg<TestThread>(tmsg1);

        if (i % 10 == 0)
        {
            usleep(100);
        }
    }
}

BOOST_AUTO_TEST_SUITE(ThreadTag)
BOOST_AUTO_TEST_CASE(TagCheck)
{

    std::bitset<ADK_THREAD_MAX_MESSAGE_TAG> tag;

    for (int i = 0; i < ADK_THREAD_MAX_MESSAGE_TAG; ++i)
    {
        // auto mask = (1ul << i);
        tag[i] = 1; 

        BOOST_REQUIRE(tag[i] != 0);
    }

    BOOST_REQUIRE(tag.count() == ADK_THREAD_MAX_MESSAGE_TAG);

}

// BOOST_AUTO_TEST_CASE(ReStart)
// {
//     {
//         adk::ThreadManager* thr_mana = adk::ThreadManager::Instance();
//         thr_mana->Start();

//         adk::ThreadBase* thread = thr_mana->ThreadInstance<TestThread>();

//         thread->BlockMessageProcess(1);
//         std::thread thread1 = std::thread(GenTask2, 1);

//         thread->ReleaseMessageProcess(1);
//         sleep(1);
//         BOOST_REQUIRE(0 != ((TestThread*)thread)->count_sqn_[1]);
//         thread1.join();

//         sleep(1);
//         LOG_MSG("tag <1> counter = " << ((TestThread*)thread)->count_sqn_[1]);

//         BOOST_REQUIRE(kTestMsgCount == ((TestThread*)thread)->count_sqn_[1]);
//         BOOST_REQUIRE(0 == ((TestThread*)thread)->count_sqn_[10]);

//         // block after test success
//         thread->BlockMessageProcess(1);
//         thread->Stop();
//         thread->Finish();
//         thr_mana->Finish();
//     }

//     LOG_MSG("ReStart ThreadManager ");
//     {
//         // restart 
//         adk::ThreadManager* thr_mana = adk::ThreadManager::Instance();
//         thr_mana->Start();

//         adk::ThreadBase* thread = thr_mana->ThreadInstance<TestThread>();
//         std::thread thread1 = std::thread(GenTask2, 1);

//         //assert thread is not blocked
//         sleep(2);
//         BOOST_REQUIRE_MESSAGE(0 != ((TestThread*)thread)->count_sqn_[1],
//                               "tag <1> counter = " + std::to_string(((TestThread*)thread)->count_sqn_[1]));
//         thread1.join();

//         sleep(1);
//         LOG_MSG("tag <1> counter = " << ((TestThread*)thread)->count_sqn_[1]);

//         BOOST_REQUIRE(kTestMsgCount == ((TestThread*)thread)->count_sqn_[1]);
//         BOOST_REQUIRE(0 == ((TestThread*)thread)->count_sqn_[10]);
//         thread->Stop();
//         thr_mana->Finish();
//     }

// }

BOOST_AUTO_TEST_CASE(TagLimit)
{
    adk::ThreadManager* thr_mana = adk::ThreadManager::Instance();
    thr_mana->Start();

    adk::ThreadBase* thread = thr_mana->ThreadInstance<TestThread>();

    thread->BlockMessageProcess(1);
    thread->BlockMessageProcess(10);
    thread->BlockMessageProcess(65);
    thread->BlockMessageProcess(1000);

    // 100w / 10 * 100us = 10 s   
    std::thread thread1 = std::thread(GenTask2, 1);
    std::thread thread2 = std::thread(GenTask2, 10);
    std::thread thread3 = std::thread(GenTask2, 65);   //message tag = 1+64
    std::thread thread4 = std::thread(GenTask2, 1000);  //message tag = 10+64

    sleep(1);
    BOOST_REQUIRE(0 == ((TestThread*)thread)->count_sqn_[1]);
    BOOST_REQUIRE(0 == ((TestThread*)thread)->count_sqn_[10]);
    BOOST_REQUIRE(0 == ((TestThread*)thread)->count_sqn_[65]);
    BOOST_REQUIRE(0 == ((TestThread*)thread)->count_sqn_[1000]);

    thread->ReleaseMessageProcess(1);
    thread->ReleaseMessageProcess(10);

    sleep(1);
    BOOST_REQUIRE(0 != ((TestThread*)thread)->count_sqn_[1]);
    BOOST_REQUIRE(0 != ((TestThread*)thread)->count_sqn_[10]);
    BOOST_REQUIRE(0 == ((TestThread*)thread)->count_sqn_[65]);
    BOOST_REQUIRE(0 == ((TestThread*)thread)->count_sqn_[1000]);

    thread->ReleaseMessageProcess(65);
    thread->ReleaseMessageProcess(1000);

    thread1.join();
    thread2.join();
    thread3.join();
    thread4.join();

    sleep(1);
    LOG_MSG("tag <1> counter = " << ((TestThread*)thread)->count_sqn_[1]);
    LOG_MSG("tag <10> counter = " << ((TestThread*)thread)->count_sqn_[10]);
    LOG_MSG("tag <65> counter = " << ((TestThread*)thread)->count_sqn_[65]);
    LOG_MSG("tag <1000> counter = " << ((TestThread*)thread)->count_sqn_[1000]);

    BOOST_REQUIRE(kTestMsgCount == ((TestThread*)thread)->count_sqn_[1]);
    BOOST_REQUIRE(kTestMsgCount == ((TestThread*)thread)->count_sqn_[10]);
    BOOST_REQUIRE(kTestMsgCount == ((TestThread*)thread)->count_sqn_[65]);
    BOOST_REQUIRE(kTestMsgCount == ((TestThread*)thread)->count_sqn_[1000]);

    thr_mana->Finish();
    thread->Stop();
}

BOOST_AUTO_TEST_SUITE_END();