#include <adk_pack/thread.h>
#include <adk/util.h>

#include <iostream>
#include <iomanip>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

boost::mutex g_log_mutex;
#define LOG_MSG(msg) do {    \
    boost::mutex::scoped_lock lock_guard(g_log_mutex);  \
    std::cout << std::left << std::setw(40) << __PRETTY_FUNCTION__ << " "     \
              << boost::this_thread::get_id() << " | "    \
              << msg << std::endl; \
} while (false)


ADK_THREAD_MESSAGE(Task)
{
public:
    int32_t value() { return value_; }
    void set_value(int32_t arg) { value_ = arg; }

private:
    uint64_t value_;
};

ADK_DEFINE_THREAD(TestThread, "TestThread")
{
public:
    int32_t OnInit()
    {
        counter_ = 0;
        LOG_MSG("");
        return adk::ErrorCode::kSuccess;
    }

    int32_t OnInitOnce()
    {
        LOG_MSG("");
        return adk::ErrorCode::kSuccess;
    }

    ADK_DEFINE_MESSAGE_HANDLER(
            (OnMessage, Task)
    )

    uint64_t counter() { return counter_; }

    void inc_counter() { ++counter_; }

private:
    uint64_t counter_;
};

void TestThread::OnMessage(Task* task)
{
    ++counter_;
    return;
};

ADK_DEFINE_THREAD(WatchThread, "TestThread")
{
public:
    void OnRun()
    {
        counter_save_ = 0;
        clock_gettime(CLOCK_REALTIME, &ts_save_);

        TestThread* test_thread = adk::ThreadManager::Instance()->ThreadInstance<TestThread>();
        while (is_running())
        {
            ADK_CALC_RATE(ts_save_, counter_save_, test_thread->counter(), rate_);
            LOG_MSG("msg rate = " << rate_);
            sleep(1);
        }
    }

    struct timespec ts_save_;
    uint64_t counter_save_;
    uint64_t rate_;
};


ADK_REGISTER_THREAD_BEGIN()

    (ADK_THREAD_CLASS(TestThread), 
                adk::thread::EventMode = adk::thread::kInterrupt,
                adk::thread::InstanceNumber = 1, 
                adk::thread::BusyPollNano = adk::thread::Microseconds(200))

    (ADK_THREAD_CLASS(WatchThread), 
                adk::thread::EventMode = adk::thread::kInterrupt,
                adk::thread::InstanceNumber = 1, 
                adk::thread::BusyPollNano = adk::thread::Microseconds(200))
    
ADK_REGISTER_THREAD_END()

int main(int argc, char const *argv[])
{
    const char* test_case = "perf_test";
    if (argc != 1)
    {
        test_case = argv[1];
    }

    auto& thr_mana = *adk::ThreadManager::Instance();

    int32_t nr_instance = 1;
    if (argc != 1)
    {
        nr_instance = atoi(argv[1]);
        thr_mana.ChangeParams<TestThread>(adk::thread::InstanceNumber = nr_instance);
    }
    
    thr_mana.Start();

    adk::GenericGC::ChangeParams(adk::thread::kThreadGCName, 
                                 adk::gc::MiniGCPeriodMilli = 0);

    if (std::string("perf_test") == test_case)
    {
        std::cout << "total perf test start" << std::endl;
        for (uint64_t i = 1; i < 102400000; ++i)
        {
            auto tmsg1 = Task::NewUnsafe();
            tmsg1->set_value(i);
            adk::SendMsgUnsafe<TestThread>(tmsg1);
        }    
    }
    else if (std::string("mp_test") == test_case)
    {
        std::cout << "memory pool start" << std::endl;
        TestThread* test_thread = adk::ThreadManager::Instance()->ThreadInstance<TestThread>();
        for (uint64_t i = 1; i < 1024000000; ++i)
        {
            auto tmsg1 = Task::NewUnsafe();
            test_thread->inc_counter();
        } 
    }
    
    sleep(1000);

    return 0;
}


