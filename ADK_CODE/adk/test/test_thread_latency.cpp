#include <adk/thread.h>
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


ADK_THREAD_MESSAGE(OrderTask)
{
public:
    void set_value(uint64_t val) 
    { 
        for (uint32_t i = 0; i < 7; ++i)
        {
            ADK_BARRIER();
            values[i] = val;
        }
    }

    struct timespec& ts_begin() { return ts_begin_; }

private:
    uint64_t values[7];
    struct timespec ts_begin_;
};

ADK_DEFINE_THREAD(ProcessOrder, "ProcessOrder")
{
public:
    int32_t OnInit()
    {
        msg_lat_array_ = new uint64_t [100000000]();
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
            (OnMessage, OrderTask)
    )

    uint64_t counter() { return counter_; }

    void inc_counter() { ++counter_; }

    uint64_t* msg_lat_array() { return &msg_lat_array_[0]; }

private:
    uint64_t counter_;
    uint64_t* msg_lat_array_;
};

void ProcessOrder::OnMessage(OrderTask* order_task)
{
    struct timespec ts_now;
    clock_gettime(CLOCK_REALTIME, &ts_now);
    msg_lat_array_[counter_] = adk::time_diff(ts_now, order_task->ts_begin());
    ++counter_;
    return;
};

ADK_REGISTER_THREAD_BEGIN()

    (ADK_THREAD_CLASS(ProcessOrder), 
                adk::thread::EventMode = adk::thread::kInterrupt,
                adk::thread::InstanceNumber = 1, 
                adk::thread::BusyPollNano = adk::thread::Milliseconds(1000))
    
ADK_REGISTER_THREAD_END()

int CompareLatency(const void *a, const void *b)
{
    const uint64_t a_v = *reinterpret_cast<const uint64_t*>(a);
    const uint64_t b_v = *reinterpret_cast<const uint64_t*>(b);
    return a_v - b_v;
}

int main(int argc, char const *argv[])
{
    auto& thr_mana = *adk::ThreadManager::Instance();

    int32_t nr_instance = 1;
    if (argc != 1)
    {
        nr_instance = atoi(argv[1]);
        thr_mana.ChangeParams<ProcessOrder>(adk::thread::InstanceNumber = nr_instance);
    }

    uint64_t rate = 10000;
    if (argc > 2)
    {
        rate = atol(argv[2]);
    }
    
    thr_mana.Start();

    adk::SimpleRateController<> rate_ctrl(rate);

    uint32_t counter = 0;
    uint32_t total = rate * 10;
    while ((counter++) != total)
    {
        struct timespec ts_begin;
        clock_gettime(CLOCK_REALTIME, &ts_begin);
        auto order = OrderTask::New();
        order->set_value(10ul);
        order->ts_begin() = ts_begin;
        adk::SendMsg<ProcessOrder>(order);
        rate_ctrl.Wait();
    }

    thr_mana.ThreadInstance<ProcessOrder>()->msg_lat_array();
    qsort(thr_mana.ThreadInstance<ProcessOrder>()->msg_lat_array(),
          thr_mana.ThreadInstance<ProcessOrder>()->counter(), sizeof(uint64_t), CompareLatency);

    uint64_t sum = 0;
    for (uint64_t index = 0; index < thr_mana.ThreadInstance<ProcessOrder>()->counter(); ++index)
        sum += thr_mana.ThreadInstance<ProcessOrder>()->msg_lat_array()[index];

    std::cout << "min = " << thr_mana.ThreadInstance<ProcessOrder>()->msg_lat_array()[0] << "\n"
              << "max = " << thr_mana.ThreadInstance<ProcessOrder>()->msg_lat_array()[thr_mana.ThreadInstance<ProcessOrder>()->counter() - 1]  << "\n"
              << "95% = " << thr_mana.ThreadInstance<ProcessOrder>()->msg_lat_array()[(thr_mana.ThreadInstance<ProcessOrder>()->counter()) * 95 / 100]  << "\n"
              << "avg = " << sum / thr_mana.ThreadInstance<ProcessOrder>()->counter()
              << std::endl;
    
    sleep(1000);

    return 0;
}


