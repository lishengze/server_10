#include <adk_pack/thread.h>
#include <adk_pack/generic_gc.h>

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
    int32_t value_;
};

ADK_OOB_THREAD_MESSAGE(ReleaseRequest)
{};

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
            (OnMessage, Task),
            (OnMessage, ReleaseRequest)
    )

    uint32_t counter() { return counter_; }

private:
    uint32_t counter_;
};

void TestThread::OnMessage(Task* task)
{
    ++counter_;
    return;
};

void TestThread::OnMessage(ReleaseRequest* req)
{
    ++counter_;
    return;
};

ADK_REGISTER_THREAD_BEGIN()

    (ADK_THREAD_CLASS(TestThread), 
                adk::thread::EventMode = adk::thread::kInterrupt,
                adk::thread::InstanceNumber = 1, 
                adk::thread::BusyPollNano = adk::thread::Microseconds(200))
    
ADK_REGISTER_THREAD_END()

int main(int argc, char const *argv[])
{
    auto& thr_mana = *adk::ThreadManager::Instance();

    int32_t nr_instance = 1;
    if (argc != 1)
    {
        nr_instance = atoi(argv[1]);
        thr_mana.ChangeParams<TestThread>(adk::thread::InstanceNumber = nr_instance);
    }
    
    thr_mana.Start();

    uint32_t counter = 0;
    for (int32_t i = 1; i < 1024000; ++i)
    {
        auto tmsg1 = Task::New();
        tmsg1->set_value(i);
        for (int32_t j = 0; j < nr_instance; ++j)
        {
            adk::SendMsg<TestThread>(tmsg1, j);
            ++counter;
        }

        if (i % 20 == 0)
        {
            auto tmsg2 = ReleaseRequest::New();
            for (int32_t j = 0; j < nr_instance; ++j)
            {
                adk::SendMsg<TestThread>(tmsg2);            
                ++counter;
            }
        }
    }

    sleep(2);

    uint32_t thread_counter_sum = 0;
    for (int32_t j = 0; j < nr_instance; ++j)
    {
        auto* my_thread = (TestThread*)(thr_mana.ThreadInstance<TestThread>(j));
        thread_counter_sum += my_thread->counter();
    }

    LOG_MSG("counter = " << counter << ", thread_counter_sum = " << thread_counter_sum);
    LOG_MSG(adk::GenericGC::Dump(adk::thread::kThreadGCName));
    // define=__ADK_GGC_TEST__
    // LOG_MSG("adk::GCAgent::nr_push_requests() = " << adk::GCAgent::nr_push_requests());
    if (counter != thread_counter_sum)
    {
        LOG_MSG("bug on !");
    }

    return 0;
}


