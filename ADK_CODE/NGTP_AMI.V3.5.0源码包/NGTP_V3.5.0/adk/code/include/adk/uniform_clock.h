#pragma once

#include <atomic>
#include <chrono>
#include <ctime>
#include <mutex>

namespace adk_impl
{

// A wall clock that cannot be set and represents monotonic time since the Epoch.
// This clock is not affected by discontinuous jumps in the system time (e.g., if the system
// administrator manually changes the clock), but is affected by the incremental adjustments performed
// by adjtime(3) and NTP.
//
// The uniform_clock has same interface with c++11 std::chrono::system_clock and std::chrono::steady_clock
// and similiar useage. In addition to now(), uniform_clock also offer offset() reset() interface which
// allows user to offset and reset this clock repsectively.
//
// Offset() still keep monotonic property of this clock.
class uniform_clock
{
public:
    typedef std::chrono::nanoseconds duration;
    typedef duration::rep rep;
    typedef duration::period period;
    typedef std::chrono::time_point<uniform_clock, duration> time_point;

    static_assert(sizeof(long int) == 8, "uniform_clock should be used only on 64 bit platform");

    static constexpr bool is_steady = true;

    // uniform_clock's view of time
    // thread safe
    static time_point now();

    // same with now(), but return result as 'struct timespec'
    // thread safe
    static struct timespec current();

    // how long from now, usually in the near future, if future < current(), return 0
    // thread safe
    static struct timespec how_long_from_now(const struct timespec& future);
    static struct timespec diff(const struct timespec& now, const struct timespec& future);

    // restart, ignore differences with std::system_clock
    static void reset();

    // adjust forward/backward of uniform_clock
    // not thread safe
    static void offset(struct timespec const&);  // {seconds, nanoseconds}

    // not thread safe
    static void offset(std::time_t t);  // seconds

    // not thread safe
    template <typename Duration, typename = typename std::enable_if<std::chrono::__is_duration<Duration>::value, void>::type>
    static void offset(Duration d);

    // Map to C API
    // thread safe
    static std::time_t
    to_time_t(const time_point& t) noexcept
    {
        return std::time_t(std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count());
    }

    // thread safe
    static time_point
    from_time_t(std::time_t t) noexcept
    {
        typedef std::chrono::time_point<uniform_clock, std::chrono::seconds> from;
        return std::chrono::time_point_cast<uniform_clock::duration>(from(std::chrono::seconds(t)));
    }

    static void adjust(struct timespec& ts);

private:
    static std::mutex mutex_;
    static struct timespec real_timestamp_;
    static struct timespec steady_timestamp_;
    static std::atomic<bool> is_timing;

    static void now(struct timespec& ts);

#ifndef NDEBUG
public:
    static uint64_t adjust_forward;
    static uint64_t adjust_backward;
    static uint64_t adjust_negtive;
#endif
};

#ifndef ADK_NANOSECONDS
#define ADK_NANOSECONDS 1000000000
#endif

template <typename Duration, typename>
void uniform_clock::offset(Duration d)
{
    uniform_clock::duration nano_d = std::chrono::duration_cast<uniform_clock::duration>(d);

    std::lock_guard<std::mutex> lock(mutex_);
    real_timestamp_.tv_nsec += nano_d.count();
    adjust(real_timestamp_);
}

template <>
void uniform_clock::offset<uniform_clock::duration>(uniform_clock::duration d)
{
    std::lock_guard<std::mutex> lock(mutex_);
    real_timestamp_.tv_nsec += d.count();
    adjust(real_timestamp_);
}

}  // namespace adk_impl