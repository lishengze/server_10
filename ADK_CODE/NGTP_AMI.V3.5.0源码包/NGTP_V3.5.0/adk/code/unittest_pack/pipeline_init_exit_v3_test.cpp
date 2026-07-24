#define BOOST_TEST_MODULE pipeline
#include <boost/test/included/unit_test.hpp>
#include <turtle/mock.hpp>

#include <adk_pack/pipeline.h>

#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <iostream>
#include <sstream>

using namespace adk;
using std::endl;
using std::cout;

class IOSW : public StageWorker<ADK_IO(int64_t, int64_t)>
{
public:
    IOSW(const std::string& name)
        :   StageWorker<ADK_IO(int64_t, int64_t)>(name)
    {}

    ~IOSW()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {
        Forward(message);
    }
};

class ISW : public StageWorker<ADK_INPUT(int64_t)>
{
public:
    ISW(const std::string& name)
        :   StageWorker<ADK_INPUT(int64_t)>(name)
    {}

    ~ISW()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {
        cout << "thread id = " << syscall(SYS_gettid) << ", message = " << message << ", dim = " << dim << ", idx = " << idx << endl;
    }
};

MOCK_BASE_CLASS(MockIOSW, IOSW)
{
    MockIOSW(const std::string& name)
        :   IOSW(name)
    {}

    MOCK_METHOD(OnInit, 0, bool())
    MOCK_METHOD(OnExit, 0, void())
    MOCK_METHOD(OnMessage, 3, void (int64_t&, short, short))
};

MOCK_BASE_CLASS(MockISW, ISW)
{
    MockISW(const std::string& name)
        :   ISW(name)
    {}

    MOCK_METHOD(OnInit, 0, bool())
    MOCK_METHOD(OnExit, 0, void())
    MOCK_METHOD(OnMessage, 3, void (int64_t&, short, short))
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


    MockIOSW firt_1;
    MockIOSW firt_2;
    MockIOSW firt_3;
    MockISW  firt_4;
    Pipeline pipeline;
    PipelineEntrance<int64_t, 1>* entrance;
};

BOOST_FIXTURE_TEST_CASE(test_pipeline_running, TestFixture)
{
    MOCK_EXPECT(firt_1.OnInit).once().returns(true);
    MOCK_EXPECT(firt_2.OnInit).once().returns(true);
    MOCK_EXPECT(firt_3.OnInit).once().returns(true);
    MOCK_EXPECT(firt_4.OnInit).once().returns(true);

    

    MOCK_EXPECT(firt_1.OnExit).once();
    MOCK_EXPECT(firt_2.OnExit).once();
    MOCK_EXPECT(firt_3.OnExit).once();
    MOCK_EXPECT(firt_4.OnExit).once();

    MOCK_EXPECT(firt_4.OnMessage).never();
    MOCK_EXPECT(firt_1.OnMessage).exactly(65535);

    pipeline.Start();
        
    int64_t counter = 0;
    while (counter != 65535)
    {
        ++counter;
        entrance->SequencialForward(counter);
    }

    sleep(1);
    pipeline.Stop();
}

