#ifndef TEST_TEST_THREAD2_H_
#define TEST_TEST_THREAD2_H_

#include <adk_pack/thread.h>

#include <iostream>
#include <iomanip>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

extern boost::mutex g_log_mutex;
#define LOG_MSG(msg) do {    \
    boost::mutex::scoped_lock lock_guard(g_log_mutex);  \
    std::cout << std::left << std::setw(40) << __PRETTY_FUNCTION__ << " "     \
              << boost::this_thread::get_id() << " "    \
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

ADK_THREAD_MESSAGE(Cookie)
{
public:
    int32_t value() { return value_; }
    void set_value(int32_t arg) { value_ = arg; }

private:
    int32_t value_;
};

ADK_OOB_THREAD_MESSAGE(ConnectReady)
{
public:
    int32_t value() { return value_; }
    void set_value(int32_t arg) { value_ = arg; }

private:
    int32_t value_;
};

struct MyThreadSharedData
{
    uint32_t counter = 0;
};

ADK_DEFINE_THREAD(MyThread, "this is my thread")
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
        set_thread_shared(new MyThreadSharedData());
        return adk::ErrorCode::kSuccess;
    }

    ADK_DEFINE_MESSAGE_HANDLER(
        (OnMessage, Task),
        (OnMessage, Cookie),
        (OnMessage, ConnectReady)
    )

    void OnExit()
    {
        LOG_MSG("");
    }

    void OnExitOnce()
    {
        LOG_MSG("");
    }
};

inline void MyThread::OnMessage(Task* task)
{
    ++(get_thread_shared<MyThreadSharedData>()->counter);

    LOG_MSG(task->value() 
            << " shared_counter <" 
            << get_thread_shared<MyThreadSharedData>()->counter
            << ">");
};

inline void MyThread::OnMessage(ConnectReady* ready_event)
{
    LOG_MSG(ready_event->value());
};

ADK_DEFINE_THREAD(TheirThread, "this is their thread")
{
public:
    TheirThread()
        :   round_counter_(0)
    {}

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

    void OnRun()
    {
        while (is_running())
        {
            sleep(1);
            LOG_MSG(round_counter_++);
        }
    }

    void OnExit()
    {
        LOG_MSG("");
    }

    void OnExitOnce()
    {
        LOG_MSG("");
    }

private:
    uint32_t round_counter_;
};

ADK_REGISTER_THREAD_BEGIN()

    (ADK_THREAD_CLASS(MyThread), 
                adk::thread::EventMode = adk::thread::kInterrupt,
                adk::thread::InstanceNumber = 2, 
                adk::thread::BusyPollNano = adk::thread::Microseconds(200))

    (ADK_THREAD_CLASS(TheirThread))
    
ADK_REGISTER_THREAD_END()

#endif // TEST_TEST_THREAD2_H_


