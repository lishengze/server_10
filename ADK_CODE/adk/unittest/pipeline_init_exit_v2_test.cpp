#define BOOST_TEST_MODULE pipeline
#include <boost/test/included/unit_test.hpp>
#include <turtle/mock.hpp>

#include <adk/pipeline.h>

#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <iostream>
#include <sstream>

using namespace adk;
using std::endl;
using std::cout;

class SWFirst : public StageWorker<ADK_IO(int64_t, int64_t)>
{
public:
    SWFirst(const std::string& name)
        :   StageWorker<ADK_IO(int64_t, int64_t)>(name)
    {
        init_is_called = false;
        exit_is_called = false;
    }

    ~SWFirst()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {
        cout << "message = " << message << endl;
        message += offset_;
        Forward(message);
    }

    virtual bool OnInit()
    {
        std::cout << "SWFirst " << name() << " >>> " << __FUNCTION__ << std::endl;
        init_is_called = true;
        return true;
    }

    virtual void OnExit()
    {
        std::cout << "SWFirst " << name() << " >>> " << __FUNCTION__ << std::endl;
        exit_is_called = true;
    }

    int64_t offset_;
    bool init_is_called;
    bool exit_is_called;
};

class SWSecond : public StageWorker<ADK_INPUT(2, int64_t, ADK_PL_MODE_SEQUENCIAL)>
{
public:
    SWSecond(const std::string& name)
        :   StageWorker<ADK_INPUT(2, int64_t, ADK_PL_MODE_SEQUENCIAL)>(name)
    {}

    ~SWSecond()
    {}  

    virtual void OnMessage(int64_t& message, short dim, short idx)
    {
        cout << "thread id = " << syscall(SYS_gettid) << ", message = " << message << ", dim = " << dim << ", idx = " << idx << endl;
    }

    virtual bool OnInit()
    {
        std::cout << "SWSecond " << name() << " >>> " << __FUNCTION__ << std::endl;
        init_is_called = true;
        return true;
    }

    virtual void OnExit()
    {
        std::cout << "SWSecond " << name() << " >>> " << __FUNCTION__ << std::endl;
        exit_is_called = true;
    }

    bool init_is_called;
    bool exit_is_called;
};

MOCK_BASE_CLASS(MockSWFirst, SWFirst)
{
    MockSWFirst(const std::string& name)
        :   SWFirst(name)
    {}

    MOCK_METHOD(OnInit, 0, bool())
    MOCK_METHOD(OnExit, 0, void())
    // MOCK_METHOD(OnMessage, 3, void (int64_t&, short, short))
};

MOCK_BASE_CLASS(MockSWSecond, SWSecond)
{
    MockSWSecond(const std::string& name)
        :   SWSecond(name)
    {}

    MOCK_METHOD(OnInit, 0, bool())
    MOCK_METHOD(OnExit, 0, void())
    MOCK_METHOD(OnMessage, 3, void (int64_t&, short, short))
};

// class AA
// {
// public:
//     AA(int a)
//     {}

//     virtual int Func() { return 0; };
// };

// MOCK_BASE_CLASS(MockSWFirst, AA)
// {
//     MockSWFirst(int a)
//         :   AA(a)
//     {}

//     MOCK_METHOD(Func, 0, int())
// };

struct TestFixture
{
    TestFixture() 
        :   firt_1("f1"),
            firt_2("f2"),
            second_1("s1")
    {
        pipeline.ConnectManyToOne(boost::assign::list_of(firt_1.Next<int64_t>())
                                                        (firt_2.Next<int64_t>()),
                                  second_1.Prev());

        entrance = pipeline.CreateEntrance<Pipeline::kMessaging, 2>(boost::assign::list_of(firt_1.Prev())
                                                                    (firt_2.Prev()));
    }   

    // SWFirst firt_1;
    // SWFirst firt_2;
    MockSWFirst firt_1;
    MockSWFirst firt_2;
    // SWSecond second_1;
    MockSWSecond second_1;
    Pipeline pipeline;
    PipelineEntrance<int64_t, 2>* entrance;
};


BOOST_FIXTURE_TEST_CASE(test_pipeline_start_stop_succ, TestFixture)
{
    MOCK_EXPECT(firt_1.OnInit).once().returns(true);
    MOCK_EXPECT(firt_2.OnInit).once().returns(true);
    MOCK_EXPECT(second_1.OnInit).once().returns(true);

    

    MOCK_EXPECT(firt_1.OnExit).once();
    MOCK_EXPECT(firt_2.OnExit).once();
    MOCK_EXPECT(second_1.OnExit).once();
    
    
    pipeline.Start();
    sleep(1);
    pipeline.Stop();

    mock::verify();

    // BOOST_CHECK_EQUAL(firt_1.init_is_called, true);
    // BOOST_CHECK_EQUAL(firt_2.init_is_called, true);
    // BOOST_CHECK_EQUAL(second_1.init_is_called, true);

    // BOOST_CHECK_EQUAL(firt_1.exit_is_called, true);
    // BOOST_CHECK_EQUAL(firt_2.exit_is_called, true);
    // BOOST_CHECK_EQUAL(second_1.exit_is_called, true);
}

BOOST_FIXTURE_TEST_CASE(test_pipeline_start_fail, TestFixture)
{
    MOCK_EXPECT(firt_1.OnInit).once().returns(true);
    MOCK_EXPECT(firt_2.OnInit).once().returns(true);
    MOCK_EXPECT(second_1.OnInit).once().returns(false);

    

    MOCK_EXPECT(firt_1.OnExit).once();
    MOCK_EXPECT(firt_2.OnExit).once();
    MOCK_EXPECT(second_1.OnExit).never();

    pipeline.Start();
    sleep(1);
    pipeline.Stop();
}

BOOST_FIXTURE_TEST_CASE(test_pipeline_start_fail_v2, TestFixture)
{
    MOCK_EXPECT(firt_1.OnInit).once().returns(true);
    MOCK_EXPECT(firt_2.OnInit).once().returns(false);
    MOCK_EXPECT(second_1.OnInit).never();

    MOCK_EXPECT(firt_1.OnExit).once();
    MOCK_EXPECT(firt_2.OnExit).never();
    MOCK_EXPECT(second_1.OnExit).never();

    pipeline.Start();
    sleep(1);
    pipeline.Stop();
}

BOOST_FIXTURE_TEST_CASE(test_pipeline_start_fail_v3, TestFixture)
{
    MOCK_EXPECT(firt_1.OnInit).once().returns(false);
    MOCK_EXPECT(firt_2.OnInit).never();
    MOCK_EXPECT(second_1.OnInit).never();

    MOCK_EXPECT(firt_1.OnExit).never();
    MOCK_EXPECT(firt_2.OnExit).never();
    MOCK_EXPECT(second_1.OnExit).never();

    pipeline.Start();
    sleep(1);
    pipeline.Stop();
}

BOOST_FIXTURE_TEST_CASE(test_pipeline_running, TestFixture)
{
    MOCK_EXPECT(firt_1.OnInit).once().returns(true);
    MOCK_EXPECT(firt_2.OnInit).once().returns(true);
    MOCK_EXPECT(second_1.OnInit).once().returns(true);

    MOCK_EXPECT(firt_1.OnExit).once();
    MOCK_EXPECT(firt_2.OnExit).once();
    MOCK_EXPECT(second_1.OnExit).once();

    MOCK_EXPECT(second_1.OnMessage).exactly(4);

    pipeline.Start();
        
    int64_t counter = 0;
    while (counter != 4)
    {
        ++counter;
        entrance->SequencialForward(counter);
    }

    sleep(1);
    pipeline.Stop();
}

