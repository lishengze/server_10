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
    AppStageWorker(const std::string& name, int64_t expect_ini, uint32_t forward_times, uint32_t msg_step)
        :   StageWorker<ADK_IO(int64_t, int64_t)>(name),
            expect(expect_ini),
            times(forward_times),
            step(msg_step)
    {}

    ~AppStageWorker()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {    
        // std::cout << "AppStageWorker message: " << message << std::endl;     
        if ((expect / step) != message 
            || shm_chk_point_->total_order_sqn != message)
        {
            BOOST_REQUIRE(false);
        }
        ++expect;
        // BOOST_CHECK_EQUAL(message, expect);
        // BOOST_CHECK_EQUAL(total_order, expect);
        if (except_pos != 0 && message == except_pos)
        {
            throw std::runtime_error("mannual exception");
        }

        for (uint32_t i = 0; i < times; ++i)
        {
            Forward(message);
        }
    }

    int64_t expect = 0;
    uint32_t times = 1;
    uint32_t step = 1;
    int64_t except_pos = 0;
};

class AppStageWorker2 : public StageWorker<ADK_IO(int64_t, int64_t)>
{
public:
    AppStageWorker2(const std::string& name, int64_t expect_ini, uint32_t msg_step)
        :   StageWorker<ADK_IO(int64_t, int64_t)>(name),
            expect(expect_ini),
            step(msg_step)
    {}

    ~AppStageWorker2()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {   
        if ((expect / step) != message 
            || shm_chk_point_->total_order_sqn != message)
        {
            BOOST_REQUIRE(false);
        }
        ++expect;
        // 最后一个stage，不再forward消息
    }

    int64_t expect = 0;
    uint32_t step = 1;
};

struct TestFixture
{
    TestFixture() 
        :   firt_1("f1", 1, 2, 1),
            firt_2("f2", 2, 2, 2),
            firt_3("f3", 4, 2, 4),
            firt_4("f4", 8, 8),
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

// 测试stage向前forward多次的情况
// firt_1 接收entrance forward的所有消息，并每收到一个消息，向firt_2 forward两次
// firt_2 向前 forward 两次
// firt_3 向前 forward 两次
BOOST_FIXTURE_TEST_CASE(case1, TestFixture)
{
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_1, firt_2);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_2, firt_3);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_3, firt_4);
    entrance = pipeline.CreateEntrance<Pipeline::kMessaging>(firt_1.Prev());
    pipeline.Start();
        
    int64_t counter = 1;
    while (counter != 10000)
    {
        adk::set_pipeline_total_order_seq_num(counter);
        if (entrance->Forward(counter) != kSuccess)
            continue;
        ++counter;
    }

    sleep(1);
    std::cout << "expect: " << firt_1.expect 
            << "," << firt_2.expect
            << "," << firt_3.expect
            << "," << firt_4.expect
            << std::endl;

    std::cout << "total_order: " << firt_1.shm_chk_point_->total_order_sqn
            << "," << firt_2.shm_chk_point_->total_order_sqn
            << "," << firt_3.shm_chk_point_->total_order_sqn
            << "," << firt_4.shm_chk_point_->total_order_sqn
            << std::endl;
 
    // 各个stage收到的消息指数增长
    BOOST_REQUIRE(firt_1.expect == 10000);
    BOOST_REQUIRE(firt_2.expect == 20000);
    BOOST_REQUIRE(firt_3.expect == 40000);
    BOOST_REQUIRE(firt_4.expect == 80000);

    // total_order和message数值保持一致
    BOOST_REQUIRE(firt_1.shm_chk_point_->total_order_sqn == 9999);
    BOOST_REQUIRE(firt_2.shm_chk_point_->total_order_sqn == 9999);
    BOOST_REQUIRE(firt_3.shm_chk_point_->total_order_sqn == 9999);
    BOOST_REQUIRE(firt_4.shm_chk_point_->total_order_sqn == 9999);
    pipeline.Stop();
}


BOOST_FIXTURE_TEST_CASE(case2, TestFixture)
{
    // firt_1 在接收到5000条消息后抛异常
    firt_1.except_pos = 5000;
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_1, firt_2);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_2, firt_3);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_3, firt_4);
    entrance = pipeline.CreateEntrance<Pipeline::kMessaging>(firt_1.Prev());
    pipeline.Start();
        
    int64_t counter = 1;
    while (counter != 10000)
    {
        adk::set_pipeline_total_order_seq_num(counter);
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
    std::cout << "expect: " << firt_1.expect 
            << "," << firt_2.expect
            << "," << firt_3.expect
            << "," << firt_4.expect
            << std::endl;

    std::cout << "total_order: " << firt_1.shm_chk_point_->total_order_sqn
            << "," << firt_2.shm_chk_point_->total_order_sqn
            << "," << firt_3.shm_chk_point_->total_order_sqn
            << "," << firt_4.shm_chk_point_->total_order_sqn
            << std::endl;
 
    BOOST_REQUIRE(firt_1.expect == 5001);
    BOOST_REQUIRE(firt_2.expect == 10000);
    BOOST_REQUIRE(firt_3.expect == 20000);
    BOOST_REQUIRE(firt_4.expect == 40000);

    BOOST_REQUIRE(firt_1.shm_chk_point_->total_order_sqn == 5000);
    BOOST_REQUIRE(firt_2.shm_chk_point_->total_order_sqn == 4999);
    BOOST_REQUIRE(firt_3.shm_chk_point_->total_order_sqn == 4999);
    BOOST_REQUIRE(firt_4.shm_chk_point_->total_order_sqn == 4999);
    pipeline.Stop();
}


BOOST_FIXTURE_TEST_CASE(case3, TestFixture)
{
    firt_3.except_pos = 5000;
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_1, firt_2);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_2, firt_3);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_3, firt_4);
    entrance = pipeline.CreateEntrance<Pipeline::kMessaging>(firt_1.Prev());
    pipeline.Start();
        
    int64_t counter = 1;
    while (counter != 10000)
    {
        adk::set_pipeline_total_order_seq_num(counter);
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
    std::cout << "expect: " << firt_1.expect 
            << "," << firt_2.expect
            << "," << firt_3.expect
            << "," << firt_4.expect
            << std::endl;

    std::cout << "total_order: " << firt_1.shm_chk_point_->total_order_sqn
            << "," << firt_2.shm_chk_point_->total_order_sqn
            << "," << firt_3.shm_chk_point_->total_order_sqn
            << "," << firt_4.shm_chk_point_->total_order_sqn
            << std::endl;
 
    BOOST_REQUIRE(firt_1.expect >= 5001);
    BOOST_REQUIRE(firt_2.expect >= 10000);
    BOOST_REQUIRE(firt_3.expect == 20001);
    BOOST_REQUIRE(firt_4.expect == 40000);

    BOOST_REQUIRE(firt_1.shm_chk_point_->total_order_sqn >= 5000);
    BOOST_REQUIRE(firt_2.shm_chk_point_->total_order_sqn >= 5000);
    BOOST_REQUIRE(firt_3.shm_chk_point_->total_order_sqn == 5000);
    BOOST_REQUIRE(firt_4.shm_chk_point_->total_order_sqn == 4999);
    pipeline.Stop();
}

BOOST_FIXTURE_TEST_CASE(case4, TestFixture)
{
    firt_3.except_pos = 5000;
    pipeline.Connect<Pipeline::kInplace, int64_t>(firt_1, firt_2);
    pipeline.Connect<Pipeline::kInplace, int64_t>(firt_2, firt_3);
    pipeline.Connect<Pipeline::kInplace, int64_t>(firt_3, firt_4);
    entrance = pipeline.CreateEntrance<Pipeline::kInplace>(firt_1.Prev());
    pipeline.Start();
        
    int64_t counter = 1;
    while (counter != 10000)
    {
        adk::set_pipeline_total_order_seq_num(counter);
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
    std::cout << "expect: " << firt_1.expect 
            << "," << firt_2.expect
            << "," << firt_3.expect
            << "," << firt_4.expect
            << std::endl;

    std::cout << "total_order: " << firt_1.shm_chk_point_->total_order_sqn
            << "," << firt_2.shm_chk_point_->total_order_sqn
            << "," << firt_3.shm_chk_point_->total_order_sqn
            << "," << firt_4.shm_chk_point_->total_order_sqn
            << std::endl;
 
    BOOST_REQUIRE(firt_1.expect == 5001);
    BOOST_REQUIRE(firt_2.expect == 10001);
    BOOST_REQUIRE(firt_3.expect == 20001);
    BOOST_REQUIRE(firt_4.expect == 40000);

    BOOST_REQUIRE(firt_1.shm_chk_point_->total_order_sqn == 5000);
    BOOST_REQUIRE(firt_2.shm_chk_point_->total_order_sqn == 5000);
    BOOST_REQUIRE(firt_3.shm_chk_point_->total_order_sqn == 5000);
    BOOST_REQUIRE(firt_4.shm_chk_point_->total_order_sqn == 4999);
    pipeline.Stop();
}