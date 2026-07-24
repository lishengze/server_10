/**
 * @file       coarsness_clock.h
 * @brief      clock for high performance invoking and coarsness pricesion scenarios
 * @author     zhaonan@archforce.com.cn
 * @date       2017/08/11
 */ 
#ifndef ADK_IMPL_COARSNESS_TIMER_H_
#define ADK_IMPL_COARSNESS_TIMER_H_

#include <boost/asio.hpp>

namespace adk_impl
{

struct Second {};
struct Nanosecond {};
struct Millisecond {};
struct Microsecond {};

struct ClockStats
{
    uint64_t nr_update_times;
};

class CoarsnessClock
{
public:
    CoarsnessClock() {}
    ~CoarsnessClock() {}

    template<typename Precision = Second>
    static inline uint64_t Now() { return timepoint_value_second_; }

    static void Run();
    static void Run(boost::asio::io_service& io_service);
    static void RunOne();
    static void Start();
    static void Stop();
    static void SetClockTickFreq(int32_t tick_freq);
    static ClockStats& GetClockStats();

    static uint64_t timepoint_value_nano_;
    static uint64_t timepoint_value_micro_;
    static uint64_t timepoint_value_milli_;
    static uint64_t timepoint_value_second_;
};

template<>
inline uint64_t CoarsnessClock::Now<Millisecond>()
{
    return timepoint_value_milli_;
}

template<>
inline uint64_t CoarsnessClock::Now<Microsecond>()
{
    return timepoint_value_micro_;
}

template<>
inline uint64_t CoarsnessClock::Now<Nanosecond>()
{
    return timepoint_value_nano_;
}


} // adk

#endif // ADK_COARSNESS_TIMER_H_
