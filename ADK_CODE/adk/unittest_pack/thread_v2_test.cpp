#define BOOST_TEST_MODULE thread_v2
#include <boost/test/included/unit_test.hpp>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>

#include <adk_pack/error_code.h>
#include <adk_pack/thread.h>
#include <adk_pack/property.h>
#include "thread_define_test.h"

std::mutex g_log_mutex;

// 实现和声明分离
void MyThread::OnMessage(Cookie* cookie)
{
    LOG_MSG("");
    LOG_MSG(cookie->value());
};


/**************************************************************/
BOOST_AUTO_TEST_SUITE(TestThread2)

ADK_REGISTER_THREAD_BEGIN()

(ADK_THREAD_CLASS(MyThread),
    adk::thread::EventMode = adk::thread::kInterrupt,
    adk::thread::InstanceNumber = 2,
    adk::thread::BusyPollNano = adk::thread::Microseconds(200))

    (ADK_THREAD_CLASS(TheirThread))

ADK_REGISTER_THREAD_END();

BOOST_AUTO_TEST_CASE(Reference)
{
    adk::ThreadManager* thr_mana = adk::ThreadManager::Instance();
    //  重新修改参数
    int32_t ec = 0;
    ec = thr_mana->ChangeParams<MyThread>(adk::thread::ParallelInit = true);
    BOOST_REQUIRE(ec == adk::ErrorCode::kSuccess);
    ec = thr_mana->ChangeParams<MyThread>(adk::thread::InstanceNumber = 3);
    BOOST_REQUIRE(ec == adk::ErrorCode::kSuccess);
    ec = thr_mana->ChangeParams<MyThread>(adk::thread::ThreadAffinity = std::string("1-3"));
    BOOST_REQUIRE(ec == adk::ErrorCode::kSuccess);

    thr_mana->Start();

    adk::ThreadBase* my_thread = thr_mana->ThreadInstance<MyThread>();

    for (int32_t i = 0; i < 3; ++i)
    {
        auto tmsg1 = Task::New();
        tmsg1->set_value(10);
        thr_mana->SendMsg<MyThread>(tmsg1, (i % 2));
        sleep(1);

        auto tmsg2 = Cookie::New();
        tmsg2->set_value(20);
        adk::SendMsg<MyThread>(tmsg2);
        sleep(1);


        if ((i % 2) == 0)
        {
            auto event = ConnectReady::NewUnsafe();
            event->set_value(30);
            //adk::SendMsgUnsafe<MyThread>(event);
            my_thread->SendMsg(event);
        }
    }

    std::cout << thr_mana->Dump(true) << std::endl;

    std::cout << thr_mana->GetParms<MyThread>() << std::endl;
    thr_mana->Finish();
    
};


BOOST_AUTO_TEST_SUITE_END()