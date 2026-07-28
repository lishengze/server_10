#include <adk/thread_pool.h>
#include <iostream>

using namespace adk;

class TestJob : public adk::IJob
{
public:
    TestJob() {}
    virtual ~TestJob() {}
    
    virtual IJob::JobRetType Run()
    {
        std::cout << "TestJob is running, "
                  << "thread id " << boost::this_thread::get_id() << std::endl;

        sleep(1);

        return IJob::JobRetType::kResched;
    }
};


int main(int argc, char const *argv[])
{
    ThreadPool thr_pool;
    thr_pool.set_nr_init_thr(1);
    thr_pool.set_nr_max_thr(ADK_THREAD_POOL_UNLIMITED);
    thr_pool.set_nr_max_cc_jobs(4096);
    auto ec = thr_pool.Init();
    assert(ec == ErrorCode::kSuccess);

    thr_pool.Start();

    ec = thr_pool.TrySchedule(new TestJob());
    assert(ec == ErrorCode::kSuccess);

    sleep(3);

    ec = thr_pool.TrySchedule(new TestJob());
    assert(ec == ErrorCode::kSuccess);

    ec = thr_pool.TrySchedule(new TestJob());
    assert(ec == ErrorCode::kSuccess);

    ec = thr_pool.TrySchedule(new TestJob());
    assert(ec == ErrorCode::kSuccess);

    ec = thr_pool.TrySchedule(new TestJob());
    assert(ec == ErrorCode::kSuccess);

    ec = thr_pool.Schedule(new TestJob());
    assert(ec == ErrorCode::kSuccess);

    sleep(6);

    thr_pool.Stop();
    thr_pool.Join();

    std::cout << "finished" << std::endl;
    sleep(2);

    return 0;
}
