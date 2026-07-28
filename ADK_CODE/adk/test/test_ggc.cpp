#include <adk/generic_gc.h>

class TestClass
{
public:
    TestClass()
    {
        agent = adk::GenericGC::CreateGCAgent("global_object");
    }

    adk::GCAgent* agent;
};

TestClass g_test;

class GCReq1 : public adk::GCRequest
{
public:
    void DoGC()
    {
        std::cout << "counter_ = " << counter_ << std::endl;
        delete ((char*)this);
    }

    uint32_t counter_;
};

class GCReq2 : public adk::GCRequest
{
public:
    void DoGC()
    {
        std::cout << "desc_ = " << desc_ << std::endl;
        delete ((char*)this);
    }

    std::string desc_;
};

class GCReq3 : public adk::GCRequest
{
public:
    void DoGC();

    std::string desc_;
};

class ReqContainer
{
public:
    ReqContainer()
    {
        var_ = 1;
        var2_ = 2;
    }

    void DoGC()
    {
        std::cout << "var_ = " << var_ << ", "
                  << "var2_ = " << var2_  << std::endl;

        delete ((char*)this);
    }
    
    GCReq3   req_;
    uint32_t var_;
    uint32_t var2_;
};

void GCReq3::DoGC()
{
    ReqContainer* c = ADK_CONTAINER_OF(this, ReqContainer, req_);
    c->DoGC();
}

class GCReq4 : public adk::GCRequest
{
public:
    void DoGC()
    {
        ++counter_;
        delete ((char*)this);
    }

    static uint32_t counter_;
};
uint32_t GCReq4::counter_ = 0;

#define TOTAL_TEST_NUMBER 1024000
void TestThreadShare(bool volatile* start)
{
    while (!(*start));

    uint32_t counter = 0;
    while (++counter <= TOTAL_TEST_NUMBER)
    {
        auto* req4 = new GCReq4();
        g_test.agent->PushGCRequest(req4);    
    }
}

int main(int argc, char const *argv[])
{
    adk::GCAgent* agent_local = adk::GenericGC::CreateGCAgent("stack_object");

    auto* req1 = new GCReq1();
    req1->counter_ = 1;
    g_test.agent->PushGCRequest(req1);

    auto* req2 = new GCReq2();
    req2->desc_ = "hello world";
    agent_local->PushGCRequest(req2);

    auto* req3 = new ReqContainer();
    agent_local->PushGCRequest(&(req3->req_));

    sleep(1);
    bool start = false;
    boost::thread t1 = boost::thread(TestThreadShare, &start);
    boost::thread t2 = boost::thread(TestThreadShare, &start);
    sleep(2);
    std::cout << "start parallel test" << std::endl;
    start = 1;
    t1.join();
    t2.join();

    sleep(2);
    std::cout << "GCReq4::counter_ = " << GCReq4::counter_ << std::endl;
    if (GCReq4::counter_ != TOTAL_TEST_NUMBER * 2)
    {
        std::cout << "bug on !" << std::endl;
    }

    sleep(100);

    return 0;
}


