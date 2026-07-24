#define BOOST_TEST_MODULE thread
#include <boost/test/included/unit_test.hpp>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>

#include <adk_pack/error_code.h>
#include <adk_pack/thread.h>
#include <adk_pack/macros.h>
#include <adk_pack/property.h>
#include "thread_define_test.h"

std::mutex g_log_mutex;

ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest1);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest2);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest3);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest4);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest5);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest6);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest7);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest8);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest9);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest10);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest11);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest12);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest13);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest14);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest15);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest16);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest17);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest18);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest19);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest20);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest21);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest22);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest23);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest24);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest25);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest26);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest27);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest28);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest29);
ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest30);

// 定义公共的线程类
ADK_DEFINE_THREAD(TestThread, "TestThread")
{
public:
    int32_t OnInit()
    {
        counter_ = 0;
        return adk::ErrorCode::kSuccess;
    }

    int32_t OnInitOnce()
    {
        return adk::ErrorCode::kSuccess;
    }

    ADK_DEFINE_MESSAGE_HANDLER(
        (OnMessage, Task),
        (OnMessage, BlockMsg),
        (OnMessage, ReleaseRequest),
        (OnMessage, ReleaseRequest1),
        (OnMessage, ReleaseRequest2),
        (OnMessage, ReleaseRequest3),
        (OnMessage, ReleaseRequest6),
        (OnMessage, ReleaseRequest4),
        (OnMessage, ReleaseRequest5),
        (OnMessage, ReleaseRequest7),
        (OnMessage, ReleaseRequest8),
        (OnMessage, ReleaseRequest9),
        (OnMessage, ReleaseRequest10),
        (OnMessage, ReleaseRequest11),
        (OnMessage, ReleaseRequest12),
        (OnMessage, ReleaseRequest13),
        (OnMessage, ReleaseRequest14),
        (OnMessage, ReleaseRequest15),
        (OnMessage, ReleaseRequest16),
        (OnMessage, ReleaseRequest17),
        (OnMessage, ReleaseRequest18),
        (OnMessage, ReleaseRequest19),
        (OnMessage, ReleaseRequest20),
        (OnMessage, ReleaseRequest21),
        (OnMessage, ReleaseRequest22),
        (OnMessage, ReleaseRequest23),
        (OnMessage, ReleaseRequest24),
        (OnMessage, ReleaseRequest25),
        (OnMessage, ReleaseRequest26),
        (OnMessage, ReleaseRequest27),
        (OnMessage, ReleaseRequest28),
        (OnMessage, ReleaseRequest29),
        (OnMessage, ReleaseRequest30)
    );

    void OnIdle()
    {

    }

    uint32_t counter() { return counter_; }

    uint32_t counter_ = 0;
};

void TestThread::OnMessage(Task* task)
{
    //LOG_MSG("");
    ++counter_;
};

void TestThread::OnMessage(ReleaseRequest* req)
{
    //LOG_MSG("");
    ++counter_;
};

void TestThread::OnMessage(ReleaseRequest1* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest2* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest3* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest4* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest5* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest6* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest7* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest8* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest9* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest10* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest11* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest12* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest13* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest14* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest15* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest16* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest17* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest18* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest19* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest20* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest21* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest22* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest23* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest24* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest25* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest26* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest27* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest28* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest29* req)
{
    ++counter_;
};
void TestThread::OnMessage(ReleaseRequest30* req)
{
    ++counter_;
};


// 收到消息 value = 8 的消息时， 会阻塞1秒
void TestThread::OnMessage(BlockMsg* msg)
{
    constexpr int32_t block_point = 8;
    if (block_point == msg->value())
    {
        sleep(1);
        LOG_MSG("blocking");
    }
};

// 实现和声明分离
void MyThread::OnMessage(Cookie* cookie)
{
    LOG_MSG("");
    LOG_MSG(cookie->value());
};


/**************************************************************/
BOOST_AUTO_TEST_SUITE(SingleThread)
BOOST_AUTO_TEST_CASE(GetTypeInfo)
{

    // 检查信息获取接口
    auto obj = MyThreadMessage::New();
    BOOST_REQUIRE(MyThreadMessage::TypeId() == 1);
    BOOST_REQUIRE(MyThreadMessage::TypeName() == std::string("MyThreadMessage"));
    BOOST_REQUIRE(MyThreadMessage::TypeId() == obj->message_type());
    BOOST_REQUIRE(MyThreadMessage::TypeName() == obj->message_type_name());


    auto obj1 = MyThreadMessage::New();
    BOOST_REQUIRE(MyThreadMessage::TypeId() == obj1->message_type());
    BOOST_REQUIRE(MyThreadMessage::TypeName() == obj1->message_type_name());

    auto obj2 = MyThreadMessage2::New();
    BOOST_REQUIRE(MyThreadMessage2::TypeId() == 2);
    BOOST_REQUIRE(MyThreadMessage2::TypeName() == std::string("MyThreadMessage2"));
    BOOST_REQUIRE(MyThreadMessage2::TypeId() == obj2->message_type());
    BOOST_REQUIRE(MyThreadMessage2::TypeName() == obj2->message_type_name());


    // 检查Unsafe接口
    auto obj3 = MyThreadMessage2::NewUnsafe();
    BOOST_REQUIRE(MyThreadMessage2::TypeId() == obj3->message_type());
    BOOST_REQUIRE(MyThreadMessage2::TypeName() == obj3->message_type_name());

    auto obj4 = MyOOB::New();
    BOOST_REQUIRE(MyOOB::TypeId() == -3);
    BOOST_REQUIRE(MyOOB::TypeName() == std::string("MyOOB"));
    BOOST_REQUIRE(MyOOB::TypeId() == obj4->message_type());
    BOOST_REQUIRE(MyOOB::TypeName() == obj4->message_type_name());

}

BOOST_AUTO_TEST_CASE(Idle)
{
    ADK_REGISTER_THREAD_BEGIN()

        (ADK_THREAD_CLASS(TestThread),
            adk::thread::EventMode = adk::thread::kInterrupt,
            // adk::thread::EventMode = adk::thread::kPolling,
            adk::thread::InstanceNumber = 2,
            // adk::thread::InstanceNumber = 1, 
            adk::thread::BusyPollNano = adk::thread::Microseconds(1000));
        // adk::thread::BusyPollNano = adk::thread::Microseconds(1000000)

    ADK_REGISTER_THREAD_END();
    adk::ThreadManager* thr_mana = adk::ThreadManager::Instance();

    thr_mana->Start();

    int32_t end = 128;

    for (int32_t i = 0; i < end; ++i)
    {
        auto tmsg1 = Task::New();
        tmsg1->set_value(i);
        // 轮流分派给两个实例
        // 对应 "nr_normal_msg"
        adk::SendMsg<TestThread>(tmsg1, i % 2);

        usleep(100);

        if (i % 4 == 0)
        {
            // 对应 "nr_oob_msg"
            auto tmsg2 = ReleaseRequest::New();
            adk::SendMsg<TestThread>(tmsg2, i / 4 % 2);
        }
    }
    sleep(1);
    adk::Property prop(thr_mana->Dump(false));
    LOG_MSG(prop.Dump(true));
    // 获取统计指标，比对数量
    // .. 表示数组中第一个元素
    std::vector<adk::Property> inst_prop = prop.GetPropertyVectorValue("Threads..Instance");
    BOOST_REQUIRE(inst_prop.size() == 2);
    BOOST_REQUIRE(inst_prop[0].GetIntValue("nr_normal_msg") == end / 2);
    BOOST_REQUIRE(inst_prop[1].GetIntValue("nr_normal_msg") == end / 2);
    BOOST_REQUIRE(inst_prop[0].GetIntValue("nr_oob_msg") == end / 8);
    BOOST_REQUIRE(inst_prop[1].GetIntValue("nr_oob_msg") == end / 8);
    
    thr_mana->Finish();
}

BOOST_AUTO_TEST_CASE(MemoryLeak)
{
    // 注册线程，设置参数
    ADK_REGISTER_THREAD_BEGIN()

        (ADK_THREAD_CLASS(TestThread),
            adk::thread::EventMode = adk::thread::kInterrupt,
            adk::thread::InstanceNumber = 1,
            adk::thread::BusyPollNano = adk::thread::Microseconds(200))

    ADK_REGISTER_THREAD_END();

    adk::ThreadManager* thr_mana = adk::ThreadManager::Instance();

    int32_t nr_instance = 1;

    thr_mana->Start();

    uint32_t counter = 0;
    // 发送消息， 统计发送的个数
    for (int32_t i = 1; i < 1024000; ++i)
    {
        auto tmsg1 = Task::New();
        tmsg1->set_value(i);
        for (int32_t j = 0; j < nr_instance; ++j)
        {
            adk::SendMsg<TestThread>(tmsg1, j);
            ++counter;
        }

        // 发送两种类型消息
        if (i % 20 == 0)
        {
            auto tmsg2 = ReleaseRequest::New();
            for (int32_t j = 0; j < nr_instance; ++j)
            {
                adk::SendMsg<TestThread>(tmsg2);
                ++counter;
            }
        }
    }

    sleep(2);
    thr_mana->Finish();

    uint32_t thread_counter_sum = 0;
    // 获取线程收到消息数 
    for (int32_t j = 0; j < nr_instance; ++j)
    {
        auto* my_thread = (TestThread*)(thr_mana->ThreadInstance<TestThread>(j));
        thread_counter_sum += my_thread->counter();
    }

    LOG_MSG("counter = " << counter << ", thread_counter_sum = " << thread_counter_sum);
    // 比对消息数量是否一致
    BOOST_REQUIRE(counter == thread_counter_sum);
    LOG_MSG(adk::GenericGC::Dump(adk::thread::kThreadGCName));

};

BOOST_AUTO_TEST_CASE(NonBlock)
{
    ADK_REGISTER_THREAD_BEGIN()

        (ADK_THREAD_CLASS(TestThread),
            adk::thread::EventMode = adk::thread::kInterrupt,
            adk::thread::InstanceNumber = 1,
            adk::thread::BusyPollNano = adk::thread::Microseconds(200))

    ADK_REGISTER_THREAD_END();

    adk::ThreadManager* thr_mana = adk::ThreadManager::Instance();

    thr_mana->Start();

    int32_t end = 8192 + 128;
    int32_t i = 0, ec;
    for (; i < end; ++i)
    {
        auto tmsg1 = BlockMsg::New();
        tmsg1->set_value(i);
        // 非阻塞发送消息
        ec = adk::SendMsg<TestThread, false>(tmsg1);
        if (ec == adk::ErrorCode::kQueueFull)
        {
            LOG_MSG("QueueFull, no block method returns kQueueFull, i= " << i);
            break;
        }
    }
    // 检查是否发生阻塞， 阻塞的位置
    BOOST_REQUIRE(ec == adk::ErrorCode::kQueueFull);
    BOOST_REQUIRE(i == 8192 + 8 + 1);
};

void Greeting(adk::TimerHandler& hdl)
{
    LOG_MSG("Success: hello world!");
}
uint32_t g_timer_counter_ = 0;
void SimPeriodTimer(adk::TimerHandler& hdl, adk::ThreadTimerManager* ttm)
{
    LOG_MSG("Success:" << __FUNCTION__);
    ++g_timer_counter_;
    // 推迟时间后重新调度
    ttm->ModifyTimer(hdl, adk::thread::Milliseconds(1000), adk::TimerOffset::kLast);
}

BOOST_AUTO_TEST_CASE(Timer)
{

    ADK_REGISTER_THREAD_BEGIN()

        (ADK_THREAD_CLASS(MyThread),
            adk::thread::InstanceNumber = 1)

    ADK_REGISTER_THREAD_END();

    adk::ThreadManager* thr_mana = adk::ThreadManager::Instance();

    thr_mana->Start();
    adk::ThreadTimerManager ttm;
    ttm.Start();

    // 周期定时器
    auto timer_hdl = ttm.CreateTimer<MyThread>(adk::TimerType::kPeriod,
        boost::bind(Greeting, _1),
        adk::thread::Milliseconds(1000));

    // 单次定时器
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
    thr_mana->Finish();
    //验证不是LOG MSG 相关锁的问题
    LOG_MSG("Timer Finished.");
    // Check Equal会打印出counter 当前的值，出错的情况预期是0？
    BOOST_CHECK_EQUAL(g_timer_counter_, 10);

};

void MultiTimerHandler(adk::TimerHandler& hdl)
{

}

BOOST_AUTO_TEST_CASE(multi_Timer)
{
    ADK_REGISTER_THREAD_BEGIN()

        (ADK_THREAD_CLASS(MyThread),
            adk::thread::InstanceNumber = 1)

    ADK_REGISTER_THREAD_END();

    adk::ThreadManager* thr_mana = adk::ThreadManager::Instance();

    thr_mana->Start();
    adk::ThreadTimerManager ttm;
    ttm.Start();

    uint32_t nr_i = 0;
    for (; nr_i < 256 * 4; ++nr_i)
    {
        auto timer_hdl = ttm.CreateTimer<MyThread>(adk::TimerType::kPeriod,
                                                   boost::bind(MultiTimerHandler, _1),
                                                   adk::thread::Milliseconds(1000)
                                                    );
        if (timer_hdl.timer_id == adk::kInvalidTimerHandler.timer_id
            && timer_hdl.timer_version == adk::kInvalidTimerHandler.timer_version)
        {
            LOG_MSG("Failed: failed to create timer, idx: " + std::to_string(nr_i));
            break;
        }
    }

    sleep(20);
    ttm.Finish();
    thr_mana->Finish();
    BOOST_REQUIRE(nr_i == (256 * 4));
}

BOOST_AUTO_TEST_SUITE_END();
