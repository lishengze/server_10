#include <time.h>

#include <boost/thread/thread.hpp>
#include <boost/asio/steady_timer.hpp>

#include <adk/util.h>
#include <adk/constant.h>
#include <adk/arch/generic.h>
#include <adk/entry_wrapper.h>
#include <adk/coarsness_clock.h>

namespace adk_impl
{

#define NANO_PER_SEC    (1000L * 1000L * 1000L)
#define MICRO_PER_SEC   (1000L * 1000L)
#define MILLI_PER_SEC   (1000L)

uint64_t CoarsnessClock::timepoint_value_nano_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
uint64_t CoarsnessClock::timepoint_value_micro_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
uint64_t CoarsnessClock::timepoint_value_milli_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
uint64_t CoarsnessClock::timepoint_value_second_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));

boost::thread       g_coarsness_clock_thread;
volatile bool       g_is_clock_running = false;
volatile bool       g_is_using_io_service = false;
volatile int32_t    g_clock_freq = 1000;
boost::mutex        g_clock_mutex;
boost::asio::steady_timer*  g_update_timer;
uint32_t                    g_timer_term;
boost::system::error_code*  g_timer_ec;
ClockStats          g_clock_stats;

void CoarsnessClock::SetClockTickFreq(int32_t tick_freq)
{
    g_clock_freq = (tick_freq >= 10) ? tick_freq : 1000;
    g_clock_freq = (g_clock_freq > 10000) ? 1000 : g_clock_freq;
}

static struct timespec g_last_update, g_next_wakeup, g_time_now;

static void UpdateCore();
static void InitCore()
{
    clock_gettime(CLOCK_MONOTONIC_RAW, &g_time_now);
    UpdateCore();
}

static void CalcNextUpdate()
{
    if (g_last_update.tv_sec == 0)
    {
        InitCore();
    }
    
    uint64_t interval = NANO_PER_SEC / g_clock_freq;
    g_next_wakeup.tv_nsec = g_last_update.tv_nsec + interval;
    g_next_wakeup.tv_sec = (g_next_wakeup.tv_nsec > NANO_PER_SEC) ? ({g_next_wakeup.tv_nsec -= NANO_PER_SEC; g_last_update.tv_sec += 1;}) 
                                                                    : g_last_update.tv_sec;
}

static void UpdateCore()
{
    CoarsnessClock::timepoint_value_nano_ = g_time_now.tv_sec * NANO_PER_SEC + g_time_now.tv_nsec;
    CoarsnessClock::timepoint_value_micro_ = CoarsnessClock::timepoint_value_nano_ / MILLI_PER_SEC;
    CoarsnessClock::timepoint_value_milli_ = CoarsnessClock::timepoint_value_nano_ / MICRO_PER_SEC;
    CoarsnessClock::timepoint_value_second_ = CoarsnessClock::timepoint_value_nano_ / NANO_PER_SEC;
    g_last_update = g_time_now;
    ++g_clock_stats.nr_update_times;
}

void CoarsnessClock::RunOne()
{
    CalcNextUpdate();
    do {
        clock_gettime(CLOCK_MONOTONIC_RAW, &g_time_now);
        if (time_diff(g_next_wakeup, g_time_now) <= 0)
        {
            UpdateCore();
            break;
        }

        clock_nanosleep(CLOCK_MONOTONIC_RAW, TIMER_ABSTIME, &g_next_wakeup, NULL); 
    } while (true);
}

static void CoarsnessClockMain()
{
    while (g_is_clock_running)
    {
        CoarsnessClock::RunOne();
    }
}

void CoarsnessClock::Start()
{
    boost::mutex::scoped_lock lock_guard(g_clock_mutex);
    if (g_is_clock_running)
    {
        return;
    }

    InitCore();
    g_is_clock_running = true;
    g_coarsness_clock_thread = boost_thread("adk-coarsnessclock", "process thread", boost::bind(CoarsnessClockMain));
}

void CoarsnessClock::Run()
{
    {
        boost::mutex::scoped_lock lock_guard(g_clock_mutex);
        if (g_is_clock_running)
        {
            return;
        }
        g_is_clock_running = true;
    }

    CoarsnessClockMain();
}

static void TimerQuit()
{
    delete g_update_timer;
    g_update_timer = NULL;

    delete g_timer_ec;
    g_timer_ec = NULL;
    g_is_clock_running = false;
}

static void TimerQuitSafe(uint32_t term, boost::asio::steady_timer* this_timer, boost::system::error_code* timer_ec)
{
    boost::mutex::scoped_lock lock_guard(g_clock_mutex);
    if (g_timer_term != term)
    {
        delete timer_ec;
        delete this_timer;
        return;
    }

    TimerQuit();
}

static void UpdateTimer(const boost::system::error_code& ec, uint32_t term, boost::asio::steady_timer* this_timer, boost::system::error_code* timer_ec)
{
    if (ec || term != g_timer_term)
    {
        TimerQuitSafe(term, this_timer, timer_ec);
        return;
    }

    if (!g_is_clock_running)
    {
        TimerQuitSafe(term, this_timer, timer_ec);
        return;
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &g_time_now);
    UpdateCore();
    
    this_timer->expires_from_now(std::chrono::nanoseconds(NANO_PER_SEC / g_clock_freq), *timer_ec);
    if (*timer_ec)
    {
        TimerQuitSafe(term, this_timer, timer_ec);
        return;
    }
    this_timer->async_wait(boost::bind(UpdateTimer, _1, term, this_timer, timer_ec));
}

void CoarsnessClock::Run(boost::asio::io_service& io_service)
{
    boost::mutex::scoped_lock lock_guard(g_clock_mutex);
    if (g_is_clock_running)
    {
        return;
    }

    InitCore();
    g_is_clock_running = true;
    g_is_using_io_service = true;
    g_update_timer = new boost::asio::steady_timer(io_service);
    g_timer_ec = new boost::system::error_code();
    ++g_timer_term;

    g_update_timer->expires_from_now(std::chrono::nanoseconds(NANO_PER_SEC / g_clock_freq), *g_timer_ec);
    if (*g_timer_ec)
    {
        TimerQuit();
        return;
    }
    g_update_timer->async_wait(boost::bind(UpdateTimer, _1, g_timer_term, g_update_timer, g_timer_ec));
}

void CoarsnessClock::Stop()
{
    boost::mutex::scoped_lock lock_guard(g_clock_mutex);
    g_is_clock_running = false;
    if (g_coarsness_clock_thread.joinable())
    {
        g_coarsness_clock_thread.join();
    }
}

ClockStats& CoarsnessClock::GetClockStats()
{
    return g_clock_stats;
}

#undef NANO_PER_SEC
#undef MICRO_PER_SEC
#undef MILLI_PER_SEC
} // adk
