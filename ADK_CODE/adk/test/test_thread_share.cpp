#include <adk/thread.h>

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

    int32_t block_step() { return block_step_; }
    void set_block_step(int32_t arg) { block_step_ = arg; }

private:
    int32_t value_;
    int32_t block_step_;
};

// ADK_OOB_THREAD_MESSAGE(ReleaseRequest)
// {};

ADK_DEFINE_THREAD(TestThread, "TestThread")
{
public:
    int32_t OnInit()
    {
        memset(block_point_, 0x00, sizeof(block_point_));
        memset(count_sqn_, 0x00, sizeof(count_sqn_));

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
            // ,
            // (OnMessage, ReleaseRequest)
    )

private:
    int32_t block_point_[ADK_THREAD_MAX_MESSAGE_TAG];
    int32_t count_sqn_[ADK_THREAD_MAX_MESSAGE_TAG];
};

void TestThread::OnMessage(Task* task)
{
    LOG_MSG(task->value() << ", " << task->message_tag());

    if (block_point_[task->message_tag()] != task->value() 
        && (task->value() % task->block_step()) == 0)
    {
        block_point_[task->message_tag()] = task->value();
        BlockMessageProcess(task->message_tag());
        return;
    }

    if (count_sqn_[task->message_tag()] + 1 != task->value())
    {
        abort();
    }
    ++count_sqn_[task->message_tag()];
};

// void TestThread::OnMessage(ReleaseRequest* req)
// {
//     LOG_MSG("receive release request");
//     ReleaseMessageProcess(req->message_tag());
// };

ADK_REGISTER_THREAD_BEGIN()

    (ADK_THREAD_CLASS(TestThread), 
                adk::thread::EventMode = adk::thread::kInterrupt,
                adk::thread::InstanceNumber = 1, 
                adk::thread::BusyPollNano = adk::thread::Microseconds(200))
    
ADK_REGISTER_THREAD_END()



// ============================================================================
void GenTask(uint64_t class_id, int32_t block_step)
{
    for (int32_t i = 1; i < 1024; ++i)
    {
        auto tmsg1 = Task::New();
        tmsg1->set_value(i);
        tmsg1->set_message_tag(class_id);
        tmsg1->set_block_step(block_step);
        adk::SendMsg<TestThread>(tmsg1);

        usleep(500000);

        if (i % ((block_step + 7) << 1) == 0)
        {
            adk::ReleaseMessageProcess<TestThread>(class_id);
            // auto tmsg2 = ReleaseRequest::New();
            // tmsg2->set_message_tag(class_id);
            // adk::SendMsg<TestThread>(tmsg2);            
        }
    }
}

int main(int argc, char const *argv[])
{
    auto& thr_mana = *adk::ThreadManager::Instance();

    thr_mana.Start();

    boost::thread thread1 = boost::thread(GenTask, 1 , 5);
    boost::thread thread2 = boost::thread(GenTask, 10, 12);
    boost::thread thread3 = boost::thread(GenTask, 1023, 12);
    
    thread1.join();
    thread2.join();
    thread3.join();

    thr_mana.Finish();
    return 0;
}


