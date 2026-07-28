#include <adk/generic_gc.h>

#include <boost/thread/thread.hpp>

class GCReq1 : public adk::GCRequest
{
public:
    GCReq1()
    {
        counter_ = 0;
    }

    void DoGC()
    {
        ++counter_;
    }

    uint64_t counter() { return counter_; }

private:
    uint64_t counter_;
};


void Watcher(GCReq1* gc_req)
{
    uint64_t counter_save = 0;
    struct timespec ts_save;
    clock_gettime(CLOCK_REALTIME, &ts_save);
    uint64_t rate = 0;

    while (1)
    {
        ADK_CALC_RATE(ts_save, counter_save, gc_req->counter(), rate);
        sleep(1);
        std::cout << "rate = " << rate << std::endl;
    }
}

int main(int argc, char const *argv[])
{
    adk::GCAgent* agent_local = adk::GenericGC::CreateGCAgent("stack_object");
    GCReq1* gc_req = new GCReq1();
    boost::thread w1 = boost::thread(Watcher, gc_req);
    for (uint64_t i = 1000*1000*1000*10ul; i != 0; --i)
    {
        agent_local->PushGCRequest(gc_req);
    }

    sleep(1000);

    return 0;
}

