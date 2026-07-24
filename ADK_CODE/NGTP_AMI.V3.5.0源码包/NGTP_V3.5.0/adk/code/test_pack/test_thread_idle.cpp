#include <iostream>
#include <iomanip>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

#include <adk_pack/error_code.h>
#include <adk_pack/thread.h>

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

ADK_PURE_OOB_THREAD_MESSAGE(ReleaseRequest);

ADK_DEFINE_THREAD(TestThread, "TestThread")
{
public:
    int32_t OnInit()
    {
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

    void OnIdle()
    {
        // ++counter;
        // if (counter % 500 == 0)
            LOG_MSG("");
    }
    uint32_t counter = 0;
};

void TestThread::OnMessage(Task* task)
{
    LOG_MSG(task->value() << ", " << task->message_tag());
};

void TestThread::OnMessage(ReleaseRequest* req)
{
    LOG_MSG("");
};

ADK_REGISTER_THREAD_BEGIN()

    (ADK_THREAD_CLASS(TestThread), 
                adk::thread::EventMode = adk::thread::kInterrupt,
                // adk::thread::EventMode = adk::thread::kPolling,
                adk::thread::InstanceNumber = 2, 
                // adk::thread::InstanceNumber = 1, 
                adk::thread::BusyPollNano = adk::thread::Microseconds(1000))
                // adk::thread::BusyPollNano = adk::thread::Microseconds(1000000)
    
ADK_REGISTER_THREAD_END()

int main(int argc, char const *argv[])
{
    auto& thr_mana = *adk::ThreadManager::Instance();

    thr_mana.Start();

    int32_t end = 1024;
    if (argc >= 2)
        end = atoi(argv[1]);

    for (int32_t i = 1; i < end; ++i)
    {
        auto tmsg1 = Task::New();
        tmsg1->set_value(i);
        adk::SendMsg<TestThread>(tmsg1, i % 2);

        sleep(1);

        if (i % 4 == 0)
        {
            auto tmsg2 = ReleaseRequest::New();
            adk::SendMsg<TestThread>(tmsg2, i % 2);            
        }
    }

    sleep(6);

    thr_mana.Finish();
    return 0;
}
