#define BOOST_TEST_MODULE thread_cache
#include <boost/test/included/unit_test.hpp>
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
        idle_intransit_ = false;
        is_block_thread_ = false;
        block_point_ = 0;
        nr_on_idle_call_ = 0;
        nr_order_msg_ = 0;
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
        nr_on_idle_call_++;
    }

    bool is_block_thread_;
    bool idle_intransit_;
    int32_t block_point_;
    uint32_t nr_on_idle_call_;
    uint32_t nr_order_msg_;

private:
    void DealAciveQue(int16_t thread_num);
};

void BizProcessThread::OnMessage(OrderTask* task)
{
    nr_order_msg_++;
    
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
        is_block_thread_ = true;
    }
    else
    {
        ReleaseMessageProcess(notify->value());
        is_block_thread_ = false;
    }
};

inline void BizProcessThread::OnMessage(IdleMessage* message)
{
    static uint64_t cnt = 0;
    idle_intransit_ = false;
    usleep(1000);
    DealAciveQue(message->thread_num());
}

inline void BizProcessThread::DealAciveQue(int16_t thread_num)
{
    // do biz process
    if (!idle_intransit_)
    {
        auto tmsg = IdleMessage::New();
        tmsg->set_thread_num(thread_num);
        tmsg->set_message_tag(0);
        if (adk::ErrorCode::kSuccess == adk::ThreadManager::Instance()->SendMsg<BizProcessThread, false>(tmsg, 0))
        {
            idle_intransit_ = true;
        }

    }
}

ADK_REGISTER_THREAD_BEGIN()

    (ADK_THREAD_CLASS(BizProcessThread), 
                adk::thread::EventMode = adk::thread::kInterrupt,
                adk::thread::InstanceNumber = 1, 
                adk::thread::BusyPollNano = adk::thread::Microseconds(200),
                adk::thread::WaitTimeoutNano = adk::thread::Milliseconds(100))
    
ADK_REGISTER_THREAD_END()

BOOST_AUTO_TEST_CASE(pipeline_variant_entrance_sample)
{
    auto& thr_mana = *adk::ThreadManager::Instance();

    thr_mana.Start();

    BizProcessThread* biz_thread = thr_mana.ThreadInstance<BizProcessThread>();

    BOOST_REQUIRE(biz_thread != nullptr);
    BOOST_REQUIRE(biz_thread->is_block_thread_ == false);
    BOOST_REQUIRE(biz_thread->nr_order_msg_ == 0);

    // 先发一笔委托，触发分拣消息
    auto tmsg = OrderTask::New();
    tmsg->set_value(1000);
    tmsg->set_message_tag(1);
    adk::ThreadManager::Instance()->SendMsg<BizProcessThread>(tmsg, 0);
    usleep(200000);
    BOOST_REQUIRE(biz_thread->nr_order_msg_ == 1);

    // block OrderTask, tag = 1
    auto tmsg_notify = SignalNotify::New();
    tmsg_notify->set_value(1); // value为要block/release的tag
    tmsg_notify->set_is_block(true);
    adk::ThreadManager::Instance()->SendMsg<BizProcessThread>(tmsg_notify, 0);
    usleep(200000);
    BOOST_REQUIRE(biz_thread->is_block_thread_ == true);

    // 发送委托查看是否能够处理   
    tmsg->set_value(1001);
    tmsg->set_message_tag(1);
    adk::ThreadManager::Instance()->SendMsg<BizProcessThread>(tmsg, 0);
    sleep(1);
    BOOST_REQUIRE(biz_thread->nr_order_msg_ == 1);

    usleep(10000000);
    tmsg_notify->set_value(1); // value为要block/release的tag
    tmsg_notify->set_is_block(false);
    adk::ThreadManager::Instance()->SendMsg<BizProcessThread>(tmsg_notify, 0);
    sleep(1);
    BOOST_REQUIRE(biz_thread->is_block_thread_ == false);
    BOOST_REQUIRE(biz_thread->nr_order_msg_ == 2);

    // 发送委托查看是否能够处理   
    tmsg->set_value(1002);
    tmsg->set_message_tag(1);
    adk::ThreadManager::Instance()->SendMsg<BizProcessThread>(tmsg, 0);
    sleep(1);
    BOOST_REQUIRE(biz_thread->nr_order_msg_ == 3);

    // 再发委托
    tmsg->set_value(1003);
    tmsg->set_message_tag(1);
    adk::ThreadManager::Instance()->SendMsg<BizProcessThread>(tmsg, 0);
    sleep(1);
    BOOST_REQUIRE(biz_thread->nr_order_msg_ == 4);

    tmsg_notify->set_value(0); // value为要block/release的tag  IdleMessage
    tmsg_notify->set_is_block(true);
    adk::ThreadManager::Instance()->SendMsg<BizProcessThread>(tmsg_notify, 0);
    usleep(200000);
    BOOST_REQUIRE(biz_thread->is_block_thread_ == true);
    uint32_t nr_prev_idle = biz_thread->nr_on_idle_call_;

    sleep(5);
    uint32_t idle_diff = biz_thread->nr_on_idle_call_ - nr_prev_idle;
    //WaitTimeoutNano 100 ms, 5 * (1000/100ms) = 50
    BOOST_REQUIRE_MESSAGE((idle_diff >= 45) && (idle_diff <= 60),
                          std::string("nr idle call: ") + std::to_string(biz_thread->nr_on_idle_call_)
                              + std::string("; nr idle diff: ") + std::to_string(idle_diff));

    adk::ThreadManager::Instance()->Finish();
}

