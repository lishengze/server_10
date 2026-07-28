#define adk adk_impl
#include <adk/thread.h>

#include <iostream>
#include <iomanip>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <random>
#include <csignal>

boost::mutex g_log_mutex;
#define LOG_MSG(msg) do {    \
    boost::mutex::scoped_lock lock_guard(g_log_mutex);  \
    std::cout << std::left << std::setw(40) << __PRETTY_FUNCTION__ << " "     \
              << boost::this_thread::get_id() << " | "    \
              << msg << std::endl; std::cout.flush();\
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


ADK_PURE_OOB_THREAD_MESSAGE(BlockRequest);

ADK_THREAD_MESSAGE(Bar)
{};

constexpr uint32_t TotalTags = 64;

ADK_DEFINE_THREAD(TestThread, "TestThread")
{
public:
    bool is_locked_[TotalTags] = {false};

    int32_t OnInit()
    {
        block_point_ = 0;
        for (int i = 0; i < TotalTags; ++i)
        {
            is_locked_[i] = false;
        }
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
            (OnMessage, ReleaseRequest),
            (OnMessage, BlockRequest)
    )

private:
    int32_t block_point_;
};

void TestThread::OnMessage(Task* task)
{
    //LOG_MSG(task->value() << ", " << task->message_tag());
    if(is_locked_[task->message_tag()] == true)
    {
        raise(SIGBUS);
    }
    if ((task->value() & (65535)) == 0)
        LOG_MSG(task->value() << ", " << task->message_tag());
    if (block_point_ != task->value()
        && (task->value() % 10) == 0)
    {
        //block_point_ = task->value();
        //BlockMessageProcess(task->message_tag());
    }
};

void TestThread::OnMessage(ReleaseRequest* req)
{
    //LOG_MSG("receive release request");
    is_locked_[req->message_tag()] = false;
    ReleaseMessageProcess(req->message_tag());
};

void TestThread::OnMessage(BlockRequest* req)
{
    //LOG_MSG("receive release request");
    is_locked_[req->message_tag()] = true;
    BlockMessageProcess(req->message_tag());
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

    thr_mana.Start();
    std::random_device rd;
    int seed = rd();
    ::printf("Use seed: %d, \n", seed);
    std::mt19937 gen(seed);
    std::uniform_int_distribution<int> distribution(0, 127);

    for (int32_t i = 1; i < 1024000000; ++i)
    {
        auto curr = distribution(gen) & 255;
        if ((curr & 64) == 0)
        {
            auto tmsg1 = Task::New();
            tmsg1->set_value(i);
            tmsg1->set_message_tag(curr & 63);
            adk::SendMsg<TestThread>(tmsg1);
        }
        else
        {
            if ((curr & 128) == 0)
            {
                // Block
                auto tmsg2 = BlockRequest::New();
                tmsg2->set_message_tag(curr & 63);
                adk::SendMsg<TestThread>(tmsg2);
            }
            else
            {
                auto tmsg2 = ReleaseRequest::New();
                tmsg2->set_message_tag(curr & 63);
                adk::SendMsg<TestThread>(tmsg2);
            }
        }

        sleep(0);

    }

    for (int i = 0; i < TotalTags; ++i)
    {
        auto tmsg2 = ReleaseRequest::New();
        tmsg2->set_message_tag(i & 63);
        adk::SendMsg<TestThread>(tmsg2);
    }
    ::sleep(10);
    return 0;
}


