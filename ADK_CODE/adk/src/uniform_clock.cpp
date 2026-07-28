/**
*  Copyright (c) 2018 Archforce Financial Technology. All rights reserved.
*  Redistribution and use in source and binary forms, with or without modification, are not permitted.
*  For more information about Archforce, welcome to archforce.cn.
**/

#include <adk/uniform_clock.h>

namespace adk_impl
{

inline struct timespec getTimepoint(clockid_t id)
{
    struct timespec ts;
    ::clock_gettime(id, &ts);
    return ts;
}

constexpr bool uniform_clock::is_steady;

std::mutex uniform_clock::mutex_;
struct timespec uniform_clock::real_timestamp_   = getTimepoint(CLOCK_REALTIME);
struct timespec uniform_clock::steady_timestamp_ = getTimepoint(CLOCK_MONOTONIC_RAW);
std::atomic<bool> uniform_clock::is_timing {false};

#ifndef NDEBUG
uint64_t uniform_clock::adjust_forward  = 0;
uint64_t uniform_clock::adjust_backward = 0;
uint64_t uniform_clock::adjust_negtive  = 0;
#endif

// helper function
void uniform_clock::now(struct timespec& ts)
{
    struct timespec now_steady_timestamp;
    ::clock_gettime(CLOCK_MONOTONIC_RAW, &now_steady_timestamp);

    ts.tv_sec  = real_timestamp_.tv_sec + now_steady_timestamp.tv_sec - steady_timestamp_.tv_sec;
    ts.tv_nsec = real_timestamp_.tv_nsec + now_steady_timestamp.tv_nsec - steady_timestamp_.tv_nsec;

    // `if` is enough, `while` is more robust
    while (ts.tv_nsec >= ADK_NANOSECONDS)
    {
        ts.tv_sec += 1;
        ts.tv_nsec -= ADK_NANOSECONDS;
    }

    while (ts.tv_nsec < 0)
    {
        ts.tv_sec -= 1;
        ts.tv_nsec += ADK_NANOSECONDS;
    }
}

uniform_clock::time_point uniform_clock::now()
{
    struct timespec ret;
    now(ret);
    return time_point(duration(std::chrono::seconds(ret.tv_sec)
                               + std::chrono::nanoseconds(ret.tv_nsec)));
}

struct timespec uniform_clock::current()
{
    struct timespec ret;
    now(ret);
    return ret;
}

struct timespec uniform_clock::how_long_from_now(const struct timespec& future)
{
    struct timespec ref = current(), delta = {0, 0};

    // actually in the new future
    if (future.tv_sec > ref.tv_sec || (future.tv_sec == ref.tv_sec && future.tv_nsec >= ref.tv_nsec))
    {
        delta.tv_sec  = future.tv_sec - ref.tv_sec;
        delta.tv_nsec = future.tv_nsec - ref.tv_nsec;

        adjust(delta);
    }

    return delta;
}

struct timespec uniform_clock::diff(const struct timespec& now, const struct timespec& future)
{
    struct timespec delta = {0, 0};

    // actually in the new future
    if (future.tv_sec > now.tv_sec || (future.tv_sec == now.tv_sec && future.tv_nsec >= now.tv_nsec))
    {
        delta.tv_sec  = future.tv_sec - now.tv_sec;
        delta.tv_nsec = future.tv_nsec - now.tv_nsec;

        adjust(delta);
    }

    return delta;
}

void uniform_clock::reset()
{
    real_timestamp_   = getTimepoint(CLOCK_REALTIME);
    steady_timestamp_ = getTimepoint(CLOCK_MONOTONIC_RAW);
}

void uniform_clock::offset(struct timespec const& delta)
{
    std::lock_guard<std::mutex> lock(mutex_);

    real_timestamp_.tv_sec += delta.tv_sec;
    real_timestamp_.tv_nsec += delta.tv_nsec;
    adjust(real_timestamp_);
}

void uniform_clock::offset(std::time_t t)
{
    std::lock_guard<std::mutex> lock(mutex_);

    real_timestamp_.tv_sec += t;
    if (real_timestamp_.tv_sec < 0)
    {
        real_timestamp_.tv_sec = 0;
#ifndef NDEBUG
        ++adjust_negtive;
#endif
    }
}

// REQUIRES: mutex_ is locked
void uniform_clock::adjust(struct timespec& ts)
{
    if (ts.tv_nsec >= 0)
    {
        while (ts.tv_nsec >= ADK_NANOSECONDS)
        {
            ts.tv_nsec -= ADK_NANOSECONDS;
            ts.tv_sec += 1;
#ifndef NDEBUG
            ++adjust_forward;
#endif
        }
    }
    else
    {
        while (ts.tv_nsec < 0)
        {
            ts.tv_nsec += ADK_NANOSECONDS;
            ts.tv_sec -= 1;
#ifndef NDEBUG
            ++adjust_backward;
#endif
        }
    }

    if (ts.tv_sec < 0)
    {
        ts.tv_sec = 0;
#ifndef NDEBUG
        ++adjust_negtive;
#endif
    }
}

}  // namespace adk_impl