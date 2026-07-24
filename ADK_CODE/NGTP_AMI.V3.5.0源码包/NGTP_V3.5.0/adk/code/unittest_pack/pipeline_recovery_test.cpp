#define BOOST_TEST_MODULE pipeline
#include <boost/test/included/unit_test.hpp>

#include <adk_pack/pipeline.h>

#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <iostream>
#include <sstream>

using namespace adk;
using std::endl;
using std::cout;

#define BOOST_TEST_IGNORE_NON_ZERO_CHILD_CODE

class AppStageWorker : public StageWorker<ADK_IO(int64_t, int64_t)>
{
public:
    AppStageWorker(const std::string& name)
        :   StageWorker<ADK_IO(int64_t, int64_t)>(name)
    {}

    ~AppStageWorker()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {    
        ++expect;
        total_order = shm_chk_point_->total_order_sqn;
        if (expect != message || total_order != message)
        {
            BOOST_REQUIRE(false);
        }
        // BOOST_CHECK_EQUAL(message, expect);
        // BOOST_CHECK_EQUAL(total_order, expect);
        if (except_pos != 0 && message == except_pos)
        {
            throw std::runtime_error("mannual exception");
        }
        Forward(message);
    }

    uint64_t total_order = 0;
    int64_t expect = 0;
    int64_t except_pos = 0;
};

class AppStageWorker2 : public StageWorker<ADK_IO(int64_t, int64_t)>
{
public:
    AppStageWorker2(const std::string& name)
        :   StageWorker<ADK_IO(int64_t, int64_t)>(name)
    {}

    ~AppStageWorker2()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {    
        ++expect;
        total_order = shm_chk_point_->total_order_sqn;
        if (expect != message || total_order != message)
        {
            BOOST_REQUIRE(false);
        }
        // 最后一个stage，不再forward消息
    }

    uint64_t total_order = 0;
    int64_t expect = 0;
};

struct TestFixture
{
    TestFixture() 
        :   firt_1("f1"),
            firt_2("f2"),
            firt_3("f3"),
            firt_4("f4"),
            pipeline("ATP", "TE_Cash", 1024)
    {
    }

    AppStageWorker firt_1;
    AppStageWorker firt_2;
    AppStageWorker firt_3;
    AppStageWorker2 firt_4;
    Pipeline pipeline;
    PipelineEntrance<int64_t, 1>* entrance;
};

// pipeline基础测试，验证消息正确性与总消息数量
BOOST_FIXTURE_TEST_CASE(case1, TestFixture)
{
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_1, firt_2);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_2, firt_3);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_3, firt_4);
    entrance = pipeline.CreateEntrance<Pipeline::kMessaging>(firt_1.Prev());
    pipeline.Start();
        
    int64_t counter = 1;
    while (counter != 65536)
    {
        adk::set_pipeline_total_order_seq_num(counter);
        if (entrance->Forward(counter) != kSuccess)
            continue;
        ++counter;
    }

    sleep(1);
    BOOST_REQUIRE(firt_1.expect == 65535);
    BOOST_REQUIRE(firt_2.expect == 65535);
    BOOST_REQUIRE(firt_3.expect == 65535);
    BOOST_REQUIRE(firt_4.expect == 65535);
    pipeline.Stop();
}

// Messaging 与 inplace 递交消息两种方式混用，验证消息正确性与总消息数量
BOOST_FIXTURE_TEST_CASE(case2, TestFixture)
{
    pipeline.Connect<Pipeline::kInplace, int64_t>(firt_1, firt_2);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_2, firt_3);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_3, firt_4);
    auto* entrance = pipeline.CreateEntrance<Pipeline::kInplace>(firt_1.Prev());

    pipeline.Start();
        
    int64_t counter = 1;
    while (counter != 65536)
    {
        adk::set_pipeline_total_order_seq_num(counter);
        if (entrance->Forward(counter) != kSuccess)
            continue;
        ++counter;
    }

    sleep(1);
    BOOST_REQUIRE(firt_1.expect == 65535);
    BOOST_REQUIRE(firt_2.expect == 65535);
    BOOST_REQUIRE(firt_3.expect == 65535);
    BOOST_REQUIRE(firt_4.expect == 65535);
    pipeline.Stop();
}

// 指定位置抛异常后，停止forward消息，验证消息序列号
BOOST_FIXTURE_TEST_CASE(case3, TestFixture)
{
    // 指定在第一个stage发送10000条消息抛异常
    firt_1.except_pos = 10000;
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_1, firt_2);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_2, firt_3);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_3, firt_4);
    auto* entrance = pipeline.CreateEntrance<Pipeline::kMessaging>(firt_1.Prev());

    pipeline.Start();
        
    int64_t counter = 1;
    while (counter != 65536)
    {
        adk::set_pipeline_total_order_seq_num(counter);
        // entrance->SequencialForward(counter);
        try
        {
            if (entrance->Forward(counter) != kSuccess)
                continue;
        }
        catch(const std::runtime_error& e)
        {
            std::cerr << e.what() << '\n';
            break;
        }
        catch(...)
        {
            
        }
        
        ++counter;
    }

    sleep(1);
    // 只有第一个stage收到10000条消息，后面的stage都为9999
    BOOST_REQUIRE(firt_1.expect == 10000);
    BOOST_REQUIRE(firt_1.shm_chk_point_->total_order_sqn == 10000);
    BOOST_REQUIRE(firt_2.expect == 9999);
    BOOST_REQUIRE(firt_2.shm_chk_point_->total_order_sqn == 9999);
    BOOST_REQUIRE(firt_3.expect == 9999);
    BOOST_REQUIRE(firt_3.shm_chk_point_->total_order_sqn == 9999);
    BOOST_REQUIRE(firt_4.expect == 9999);
    BOOST_REQUIRE(firt_4.shm_chk_point_->total_order_sqn == 9999);
    pipeline.Stop();
}


BOOST_FIXTURE_TEST_CASE(case4, TestFixture)
{
    // 指定第3个stage发送10000条消息后抛异常
    firt_3.except_pos = 10000;
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_1, firt_2);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_2, firt_3);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_3, firt_4);
    auto* entrance = pipeline.CreateEntrance<Pipeline::kMessaging>(firt_1.Prev());

    pipeline.Start();
        
    int64_t counter = 1;
    while (counter != 65536)
    {
        adk::set_pipeline_total_order_seq_num(counter);
        // entrance->SequencialForward(counter);
        try
        {
            if (entrance->Forward(counter) != kSuccess)
                continue;
        }
        catch(const std::runtime_error& e)
        {
            std::cerr << e.what() << '\n';
            break;
        }
        catch(...)
        {
            
        }
        
        ++counter;
    }

    sleep(1);
    // Messaging方式传递消息
    // 前两个stage会大于10000条消息
    // 第三个stage处理到10000条
    // 第四个stage只会卡在9999条
    BOOST_REQUIRE(firt_1.expect >= 10000);
    BOOST_REQUIRE(firt_1.shm_chk_point_->total_order_sqn >= 10000);
    BOOST_REQUIRE(firt_2.expect >= 10000);
    BOOST_REQUIRE(firt_2.shm_chk_point_->total_order_sqn >= 10000);
    BOOST_REQUIRE(firt_3.expect == 10000);
    BOOST_REQUIRE(firt_3.shm_chk_point_->total_order_sqn == 10000);
    BOOST_REQUIRE(firt_4.expect == 9999);
    BOOST_REQUIRE(firt_4.shm_chk_point_->total_order_sqn == 9999);
    pipeline.Stop();
}

BOOST_FIXTURE_TEST_CASE(case5, TestFixture)
{
    firt_3.except_pos = 10000;
    pipeline.Connect<Pipeline::kInplace, int64_t>(firt_1, firt_2);
    pipeline.Connect<Pipeline::kInplace, int64_t>(firt_2, firt_3);
    pipeline.Connect<Pipeline::kInplace, int64_t>(firt_3, firt_4);
    auto* entrance = pipeline.CreateEntrance<Pipeline::kInplace>(firt_1.Prev());

    pipeline.Start();
        
    int64_t counter = 1;
    while (counter != 65536)
    {
        adk::set_pipeline_total_order_seq_num(counter);
        // entrance->SequencialForward(counter);
        try
        {
            if (entrance->Forward(counter) != kSuccess)
                continue;
        }
        catch(const std::runtime_error& e)
        {
            std::cerr << e.what() << '\n';
            break;
        }
        catch(...)
        {
            
        }
        
        ++counter;
    }

    sleep(1);
    // Inplace方式传递消息，所有消息递交都在同一个线程
    // 前三个stage会处理到10000条消息，第四个stage处理到9999
    BOOST_REQUIRE(firt_1.expect == 10000);
    BOOST_REQUIRE(firt_1.shm_chk_point_->total_order_sqn == 10000);
    BOOST_REQUIRE(firt_2.expect == 10000);
    BOOST_REQUIRE(firt_2.shm_chk_point_->total_order_sqn == 10000);
    BOOST_REQUIRE(firt_3.expect == 10000);
    BOOST_REQUIRE(firt_3.shm_chk_point_->total_order_sqn == 10000);
    BOOST_REQUIRE(firt_4.expect == 9999);
    BOOST_REQUIRE(firt_4.shm_chk_point_->total_order_sqn == 9999);
    pipeline.Stop();
}