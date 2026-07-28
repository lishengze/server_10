#include <adk_pack/thread.h>

#include <iostream>
#include <iomanip>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

// build:
// g++ --std=c++11 test_thread_optimize.cpp -I ~/work/lib/ami_res/include/ -I ~/work/3rd/boost_1_62_0/ -L ~/work/lib/ami_res/lib64/ -ladk -lpthread -lboost_thread -lboost_system
// 
boost::mutex g_log_mutex;
#define LOG_MSG(msg) do {    \
    boost::mutex::scoped_lock lock_guard(g_log_mutex);  \
    std::cout << std::left << std::setw(40) << __PRETTY_FUNCTION__ << " "     \
              << boost::this_thread::get_id() << " | "    \
              << msg << std::endl; \
} while (false)


ADK_THREAD_MESSAGE(OrderTask)
{
public:
    int32_t value() { return value_; }
    void set_value(int32_t arg) { value_ = arg; }

private:
    int32_t value_;
};

ADK_OOB_THREAD_MESSAGE(SignalNotify)
{
public:
    int32_t value() { return value_; }
    void set_value(int32_t arg) { value_ = arg; }

    bool is_block() { return is_block_; }
    void set_is_block(bool is_block) { is_block_ = is_block; }
private:
    int32_t value_;
    bool is_block_;
};

ADK_THREAD_MESSAGE(IdleMessage)
{
public:
    int16_t thread_num() { return thread_num_; }
    void set_thread_num(int16_t thread_num) { thread_num_ = thread_num; }

private:
    int16_t thread_num_;
};

ADK_DEFINE_THREAD(BizProcessThread, "ors biz process thread")
{
public:
    int32_t OnInit()
    {
        block_point_ = 0;
        idle_intransit_ = false;
        LOG_MSG("");
        return adk::ErrorCode::kSuccess;
    }

    int32_t OnInitOnce()
    {
        LOG_MSG("");
        return adk::ErrorCode::kSuccess;
    }

    ADK_DEFINE_MESSAGE_HANDLER(
        (OnMessage, OrderTask),
        (OnMessage, IdleMessage),
        (OnMessage, SignalNotify)
        )
    
    void OnIdle()
    {
        LOG_MSG("thread on idle");
    }

private:
    void DealAciveQue(int16_t thread_num);

    int32_t block_point_;
    bool idle_intransit_;
};

void BizProcessThread::OnMessage(OrderTask* task)
{
    LOG_MSG(task->value() << ", " << task->message_tag() << ", in message OrderTask");
    
    // process order
    
    // 处理积压委托
    DealAciveQue(1);
};

void BizProcessThread::OnMessage(SignalNotify* notify)
{
    LOG_MSG("receive signal notify, tag: " << notify->value() << ", is_block:" << notify->is_block());
    if (notify->is_block())
    {
        BlockMessageProcess(notify->value());
    }
    else
    {
        ReleaseMessageProcess(notify->value());
    }
};

inline void BizProcessThread::OnMessage(IdleMessage* message)
{
    static uint64_t cnt = 0;
    idle_intransit_ = false;
    usleep(1000);
    DealAciveQue(message->thread_num());
    if ((cnt++ % 5000) == 0)
    {
        LOG_MSG("in message IdleMessage");
    }
}

inline void BizProcessThread::DealAciveQue(int16_t thread_num)
{
    // do biz process
    // 
    if (!idle_intransit_)
    {
        auto tmsg = IdleMessage::New();
        tmsg->set_thread_num(thread_num);
        if (adk::ErrorCode::kSuccess == adk::ThreadManager::Instance()->SendMsg<BizProcessThread, false>(tmsg, 0))
        {
            idle_intransit_ = true;
        }
        static int32_t id = 0;
        if (++id == 10000)
        {
            LOG_MSG("thread info:" << adk::ThreadManager::Instance()->Dump(true));
            id = 0;
        }
    }
}

ADK_REGISTER_THREAD_BEGIN()

    (ADK_THREAD_CLASS(BizProcessThread), 
                adk::thread::EventMode = adk::thread::kInterrupt,
                adk::thread::InstanceNumber = 1, 
                adk::thread::BusyPollNano = adk::thread::Microseconds(200))
    
ADK_REGISTER_THREAD_END()

int main(int argc, char const *argv[])
{
    auto& thr_mana = *adk::ThreadManager::Instance();

    thr_mana.Start();

    // 先发一笔委托，触发分拣消息
    auto tmsg = OrderTask::New();
    tmsg->set_value(1000);
    tmsg->set_message_tag(1);
    adk::ThreadManager::Instance()->SendMsg<BizProcessThread>(tmsg, 0);

    // block OrderTask, tag = 1
    auto tmsg_notify = SignalNotify::New();
    tmsg_notify->set_value(1); // value为要block/release的tag
    tmsg_notify->set_is_block(true);
    adk::ThreadManager::Instance()->SendMsg<BizProcessThread>(tmsg_notify, 0);

    usleep(10000000);
    tmsg_notify->set_value(1); // value为要block/release的tag
    tmsg_notify->set_is_block(false);
    adk::ThreadManager::Instance()->SendMsg<BizProcessThread>(tmsg_notify, 0);

    // 发送委托查看是否能够处理   
    tmsg->set_value(1001);
    tmsg->set_message_tag(1);
    adk::ThreadManager::Instance()->SendMsg<BizProcessThread>(tmsg, 0);
    LOG_MSG("Send order message: " << tmsg->value());
    sleep(3);

    // 再发委托
    tmsg->set_value(1002);
    tmsg->set_message_tag(1);
    adk::ThreadManager::Instance()->SendMsg<BizProcessThread>(tmsg, 0);
    LOG_MSG("Send order message: " << tmsg->value());
    LOG_MSG("thread info:" << adk::ThreadManager::Instance()->Dump(true));
    sleep(1);
    adk::ThreadManager::Instance()->Finish();
    return 0;
}


