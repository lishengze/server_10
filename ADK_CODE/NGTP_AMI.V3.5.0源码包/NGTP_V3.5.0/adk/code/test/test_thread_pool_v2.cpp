#include <adk/thread_pool.h>
#include <iostream>

#include <boost/date_time/posix_time/posix_time.hpp>

using namespace adk;

class TestJob : public adk::IJob
{
public:
    TestJob(uint32_t counter, bool is_init_fail = false) 
    {
        static uint32_t job_id = 0;
        counter_ = counter;
        is_init_fail_ = is_init_fail;
        job_id_ = ++job_id;
    }

    virtual ~TestJob() {}

    virtual IJob::JobRetType Init()
    {
        boost::mutex::scoped_lock lock_guard(log_lock_);
        if (is_init_fail_)
        {
            std::cout << "TestJob init failed, id " << job_id_ << std::endl;
            return IJob::JobRetType::kFailure;
        }

        std::cout << "TestJob init succeed, id " << job_id_ << std::endl;
        return IJob::JobRetType::kSuccess;
    }
    
    virtual IJob::JobRetType Run()
    {
        boost::mutex::scoped_lock lock_guard(log_lock_);
        std::cout << "TestJob is running, "
                  << "thread id " << boost::this_thread::get_id() << ", " 
                  << boost::posix_time::ptime(boost::posix_time::microsec_clock::local_time()) << ", "
                  << "id " << job_id_
                  << std::endl;

        --counter_;

        if (counter_ > 0)
        {
            JOB_RESCHED_DELAY_SEC(1);
        }

        return IJob::JobRetType::kDone;
    }

    virtual void Exit()
    {
        boost::mutex::scoped_lock lock_guard(log_lock_);
        std::cout << "TestJob is exit, id " << job_id_ << std::endl;
        delete this;
    }

    uint32_t counter_;
    bool is_init_fail_;
    uint32_t job_id_;
    static boost::mutex log_lock_; 
};

boost::mutex TestJob::log_lock_;

int main(int argc, char const *argv[])
{
    ThreadPool thr_pool;
    thr_pool.set_nr_init_thr(1);
    thr_pool.set_nr_max_thr(ADK_THREAD_POOL_UNLIMITED);
    thr_pool.set_nr_max_cc_jobs(4096);
    thr_pool.set_worker_reap_nrounds(100);
    thr_pool.set_idle_delay_us(20000);
    auto ec = thr_pool.Init();
    assert(ec == ErrorCode::kSuccess);

    thr_pool.Start();

    ec = thr_pool.TrySchedule(new TestJob(4));
    assert(ec == ErrorCode::kSuccess);

    sleep(3);

    ec = thr_pool.TrySchedule(new TestJob(20));
    assert(ec == ErrorCode::kSuccess);

    sleep(1);

    ec = thr_pool.TrySchedule(new TestJob(20, true));
    assert(ec == ErrorCode::kSuccess);

    sleep(1);

    ec = thr_pool.TrySchedule(new TestJob(3));
    assert(ec == ErrorCode::kSuccess);

    sleep(5);

    thr_pool.Stop();
    thr_pool.Join();

    std::cout << "finished" << std::endl;
    sleep(2);

    return 0;
}


