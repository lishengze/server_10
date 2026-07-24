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

uint32_t FilterCount(uint64_t count, std::vector<int> filter)
{
    uint32_t result = 0;
    for (uint64_t i = 1; i < count; ++i)
    {
        bool exsists = true;
        for (auto f : filter)
        {
            if (i % f != 0)
            {
                exsists = false;
            }
        }

        if (exsists)
            ++result;
    }
    return result;
}

class AppStageWorker : public StageWorker<ADK_IO(int64_t, int64_t)>
{
public:
    AppStageWorker(const std::string& name, int64_t filter)
        :   StageWorker<ADK_IO(int64_t, int64_t)>(name),
            filter_(filter)
    {}

    ~AppStageWorker()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {
        ++cnt_;
        uint64_t total_order = shm_chk_point_->total_order_sqn;
        // BOOST_CHECK_EQUAL(total_order, message);
        if (total_order != message)
        {
            BOOST_REQUIRE(false);
        }
        if (except_pos != 0 && message == except_pos)
        {
            throw std::runtime_error("mannual exception");
        }
        if (message % filter_ == 0)
        {
            Forward(message);
        } 
    }

    int64_t filter_ = 1;
    uint64_t cnt_ = 0;
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
        ++cnt_;
        uint64_t total_order = shm_chk_point_->total_order_sqn;
        // BOOST_CHECK_EQUAL(total_order, message);
        if (total_order != message)
        {
            BOOST_REQUIRE(false);
        }
        // 最后一个stage，不再forward消息
    }

    uint64_t cnt_ = 0;
};

struct TestFixture
{
    TestFixture() 
        :   firt_1("f1", 2),
            firt_2("f2", 3),
            firt_3("f3", 5),
            firt_4("f4"),
            pipeline("ATP", "TE_Cash", 1024)
    {}

    AppStageWorker firt_1;
    AppStageWorker firt_2;
    AppStageWorker firt_3;
    AppStageWorker2 firt_4;
    Pipeline pipeline;
    PipelineEntrance<int64_t, 1>* entrance;
};

// 测试stage过滤掉部分消息的行为是否正确
// firt_1 接收 entrance传递的所有消息，但只向后forward 能被2整除的数据
// firt_2 只向后 forward 能被3整除的消息
// firt_3 只向后 forward 能被5整除的消息
// firt_4 是最后一个stage
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
        // entrance->SequencialForward(counter);
        if (entrance->Forward(counter) != kSuccess)
            continue;
        ++counter;
        usleep(1);
    }
    sleep(1);
    
    // firt_1会收到所有的65536条消息
    // firt_2收到能被2整除的数据，32768条
    // firt_3收到能被2和3整除的数据
    // firt_4收到能被2/3/5整除的数据
    BOOST_REQUIRE(firt_1.cnt_ == 65535);
    BOOST_REQUIRE(firt_2.cnt_ == 32767);
    BOOST_CHECK_EQUAL(firt_3.cnt_, FilterCount(65536, std::vector<int>{2, 3}));
    BOOST_CHECK_EQUAL(firt_4.cnt_, FilterCount(65536, std::vector<int>{2, 3, 5}));

    pipeline.Stop();
}

BOOST_FIXTURE_TEST_CASE(case2, TestFixture)
{
    // 第一个stage收到30000条消息后抛异常，后面停止forward消息
    firt_1.except_pos = 30000;
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_1, firt_2);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_2, firt_3);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_3, firt_4);
    entrance = pipeline.CreateEntrance<Pipeline::kMessaging>(firt_1.Prev());
    pipeline.Start();
        
    int64_t counter = 1;
    while (counter != 65536)
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
        usleep(1);
    }
    sleep(1);
    
    // total_order实际是与forward的message始终保持一致的
    // firt_1的total_order会是30000
    // firt_2为30000之前的最后一个能被2整除的数
    // firt_3为30000之前的最后一个能被2/3整除的数
    // firt_4为30000之前的最后一个能被2/3/5整除的数
    BOOST_REQUIRE(firt_1.shm_chk_point_->total_order_sqn == 30000);
    BOOST_REQUIRE(firt_2.shm_chk_point_->total_order_sqn == 29998);
    BOOST_REQUIRE(firt_3.shm_chk_point_->total_order_sqn == 29994);
    BOOST_REQUIRE(firt_4.shm_chk_point_->total_order_sqn == 29970);

    // 消息数量需要考虑被过滤的消息
    BOOST_REQUIRE(firt_1.cnt_ == 30000);
    BOOST_CHECK_EQUAL(firt_2.cnt_, FilterCount(30000, std::vector<int>{2}));
    BOOST_CHECK_EQUAL(firt_3.cnt_, FilterCount(30000, std::vector<int>{2, 3}));
    BOOST_CHECK_EQUAL(firt_4.cnt_, FilterCount(30000, std::vector<int>{2, 3, 5}));

    pipeline.Stop();
}


BOOST_FIXTURE_TEST_CASE(case3, TestFixture)
{
    // 第三个stage收到message为30000（可以被2/3整除）后抛异常，不再forward消息
    firt_3.except_pos = 30000;
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_1, firt_2);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_2, firt_3);
    pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_3, firt_4);
    entrance = pipeline.CreateEntrance<Pipeline::kMessaging>(firt_1.Prev());
    pipeline.Start();
        
    int64_t counter = 1;
    while (counter != 65536)
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
        usleep(1);
    }
    sleep(1);

    // 前两个stage会收到30000以上的消息
    // 第三个stage会收到30000这个消息
    // 第四个stage只能收到30000之前的能被2/3/5整除的消息
    BOOST_REQUIRE(firt_1.shm_chk_point_->total_order_sqn >= 30000);
    BOOST_REQUIRE(firt_2.shm_chk_point_->total_order_sqn >= 30000);
    BOOST_REQUIRE(firt_3.shm_chk_point_->total_order_sqn == 30000);
    BOOST_REQUIRE(firt_4.shm_chk_point_->total_order_sqn == 29970);

    BOOST_REQUIRE(firt_1.cnt_ >= 30000);
    BOOST_CHECK_GT(firt_2.cnt_, FilterCount(30000, std::vector<int>{2}));
    BOOST_CHECK_EQUAL(firt_3.cnt_, FilterCount(30000, std::vector<int>{2, 3}) + 1);
    BOOST_CHECK_EQUAL(firt_4.cnt_, FilterCount(30000, std::vector<int>{2, 3, 5}));

    pipeline.Stop();
}


BOOST_FIXTURE_TEST_CASE(case4, TestFixture)
{
    // 指定第三个stage在30000这个消息时抛异常，Inplace方式传递消息
    firt_3.except_pos = 30000;
    pipeline.Connect<Pipeline::kInplace, int64_t>(firt_1, firt_2);
    pipeline.Connect<Pipeline::kInplace, int64_t>(firt_2, firt_3);
    pipeline.Connect<Pipeline::kInplace, int64_t>(firt_3, firt_4);
    entrance = pipeline.CreateEntrance<Pipeline::kInplace>(firt_1.Prev());
    pipeline.Start();
        
    int64_t counter = 1;
    while (counter != 65536)
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
        usleep(1);
    }
    sleep(1);
    

    BOOST_REQUIRE(firt_1.shm_chk_point_->total_order_sqn == 30000);
    BOOST_REQUIRE(firt_2.shm_chk_point_->total_order_sqn == 30000);
    BOOST_REQUIRE(firt_3.shm_chk_point_->total_order_sqn == 30000);
    BOOST_REQUIRE(firt_4.shm_chk_point_->total_order_sqn == 29970);

    BOOST_REQUIRE(firt_1.cnt_ == 30000);
    BOOST_CHECK_EQUAL(firt_2.cnt_, FilterCount(30000, std::vector<int>{2}) + 1);
    BOOST_CHECK_EQUAL(firt_3.cnt_, FilterCount(30000, std::vector<int>{2, 3}) + 1);
    BOOST_CHECK_EQUAL(firt_4.cnt_, FilterCount(30000, std::vector<int>{2, 3, 5}));

    pipeline.Stop();
}
