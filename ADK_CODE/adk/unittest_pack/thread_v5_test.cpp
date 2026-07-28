#define BOOST_TEST_MODULE thread_v4
#include <boost/test/included/unit_test.hpp>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <thread>
#include <bitset>

#include <adk_pack/error_code.h>
#include <adk_pack/thread.h>
#include <adk_pack/property.h>

std::mutex g_log_mutex;
#define LOG_MSG(msg)                                                          \
    do                                                                        \
    {                                                                         \
        std::lock_guard<std::mutex> lock_guard(g_log_mutex);                  \
        std::cout << std::left << std::setw(40) << __PRETTY_FUNCTION__ << " " \
                  << std::this_thread::get_id() << " | "                      \
                  << msg << std::endl;                                        \
    } while (false)

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

    void OnExit()
    {
        LOG_MSG("");
    }

    void OnExitOnce()
    {
        LOG_MSG("");
    }
};

BOOST_AUTO_TEST_SUITE(max_timer)

std::mutex g_lock_timer_checker;
std::map<adk::TimerIdType, std::pair<int64_t, int64_t>> g_timer_checker;
uint64_t g_handle_counter_ = 0;
void CounterHandler(adk::TimerHandler& hdl)
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    std::lock_guard<std::mutex> guard(g_lock_timer_checker);
    g_timer_checker[hdl.timer_id].second = ts.tv_sec * 1000000000 + ts.tv_nsec;
    ++g_handle_counter_;
}

BOOST_AUTO_TEST_CASE(max_Timer)
{
    std::cout << "begin run max_timer" << std::endl;
    ADK_REGISTER_THREAD_BEGIN()

        (ADK_THREAD_CLASS(MyThread),
            adk::thread::InstanceNumber = 1)

    ADK_REGISTER_THREAD_END();

    adk::ThreadManager* thr_mana = adk::ThreadManager::Instance();

    thr_mana->Start();
    adk::ThreadTimerManager ttm;
    ttm.Start();

    uint32_t nr_i = 0;
    uint32_t timer_size = ADK_THREAD_MAX_TIMERS;
    for (; nr_i < timer_size; ++nr_i)
    {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);

        auto timer_hdl = ttm.CreateTimer<MyThread>(adk::TimerType::kOneShot,
                                                   boost::bind(CounterHandler, _1),
                                                   adk::thread::Milliseconds(1000)
                                                    );
        if (timer_hdl.timer_id == adk::kInvalidTimerHandler.timer_id
            && timer_hdl.timer_version == adk::kInvalidTimerHandler.timer_version)
        {
            LOG_MSG("Failed: failed to create timer, idx: " + std::to_string(nr_i));
            break;
        }

        std::lock_guard<std::mutex> guard(g_lock_timer_checker);
        g_timer_checker[timer_hdl.timer_id].first = ts.tv_sec * 1000000000 + ts.tv_nsec;
    }

    BOOST_REQUIRE(adk::kInvalidTimerHandler.timer_id == ttm.CreateTimer<MyThread>(adk::TimerType::kOneShot,
                                                   boost::bind(CounterHandler, _1),
                                                   adk::thread::Milliseconds(1000)).timer_id);

    BOOST_REQUIRE(nr_i == timer_size);

    while (g_handle_counter_ != timer_size)
    {
        std::cout << "handle count = " << g_handle_counter_ << std::endl;
        sleep(1);
    }

    for (auto &item : g_timer_checker)
    {
        BOOST_REQUIRE(item.second.second - item.second.first >= 1000 * 1000000);
        BOOST_REQUIRE(item.second.second - item.second.first <= 1100 * 1000000);
    }

    ttm.Finish();
    thr_mana->Finish();
}

BOOST_AUTO_TEST_SUITE_END();