#define BOOST_TEST_MODULE pipeline
#include <boost/test/included/unit_test.hpp>

#include <adk_pack/pipeline.h>

#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <iostream>

using namespace adk;
using std::endl;
using std::cout;

#define BOOST_TEST_IGNORE_NON_ZERO_CHILD_CODE

class AppStageWorker : public StageWorker<ADK_IO(int64_t, int64_t)>
{
public:
    AppStageWorker(const std::string& name)
        :   StageWorker<ADK_IO(int64_t, int64_t)>(name)
    {
    }

    ~AppStageWorker()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {
        char filename[64] = {0};
        sprintf(filename, "/proc/self/task/%d/comm", tid_);
        FILE* fp = fopen(filename, "r");
        if (fp)
        {
            char buf[128] = {0};
            fgets(buf, sizeof(buf), fp);
            fclose(fp);
            buf[strlen(buf) - 1] = '\0';
            thread_name_.assign(buf);
        }
        Forward(message);
    }

    std::string thread_name_;
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
        char filename[64] = {0};
        sprintf(filename, "/proc/self/task/%d/comm", tid_);
        FILE* fp = fopen(filename, "r");
        if (fp)
        {
            char buf[128] = {0};
            fgets(buf, sizeof(buf), fp);
            fclose(fp);
            buf[strlen(buf) - 1] = '\0';
            thread_name_.assign(buf);
        }
    }

    std::string thread_name_;
};

struct TestFixture
{
    TestFixture() 
        :   firt_1("f1"),
            firt_2("f2"),
            firt_3("f3"),
            firt_4("f4")
    {
        pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_1, firt_2);
        pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_2, firt_3);
        pipeline.Connect<Pipeline::kMessaging, int64_t>(firt_3, firt_4);

        entrance = pipeline.CreateEntrance<Pipeline::kMessaging>(firt_1.Prev());
    }


    AppStageWorker firt_1;
    AppStageWorker firt_2;
    AppStageWorker firt_3;
    AppStageWorker2  firt_4;
    Pipeline pipeline;
    PipelineEntrance<int64_t, 1>* entrance;
};

BOOST_FIXTURE_TEST_CASE(case1, TestFixture)
{
    pipeline.Start();
    int64_t msg = 1;
    entrance->Forward(msg);
    sleep(10);

    BOOST_REQUIRE(firt_1.thread_name_ == "adk-pipeline");
    BOOST_REQUIRE(firt_2.thread_name_ == "adk-pipeline");
    BOOST_REQUIRE(firt_3.thread_name_ == "adk-pipeline");
    BOOST_REQUIRE(firt_4.thread_name_ == "adk-pipeline");

    pipeline.Stop();
}

// 测试修改pipieline stage的线程名
BOOST_FIXTURE_TEST_CASE(case2, TestFixture)
{
    firt_1.set_thread_name("thread_1");
    firt_2.set_thread_name("thread_2");
    firt_3.set_thread_name("thread_3");

    pipeline.Start();

    int64_t msg = 1;
    entrance->Forward(msg);
    sleep(10);
    // 判断修改后的线程名
    BOOST_REQUIRE(firt_1.thread_name_ == "thread_1");
    BOOST_REQUIRE(firt_2.thread_name_ == "thread_2");
    BOOST_REQUIRE(firt_3.thread_name_ == "thread_3");
    BOOST_REQUIRE(firt_4.thread_name_ == "adk-pipeline");
    pipeline.Stop();
}