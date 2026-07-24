#pragma once

#include <string.h>
#include <cstdint>
#include <time.h>

namespace adk
{

static inline int64_t time_diff(struct timespec& lv, struct timespec& rv)
{
    return (lv.tv_sec - rv.tv_sec) * 1000000000L + lv.tv_nsec - rv.tv_nsec;
}

#define ADK_CALC_RATE(saved_ts, saved_stats, cur_stats, rate) do {  \
    const auto local_cur_stats = cur_stats; \
    struct timespec time_cur;   \
    clock_gettime(CLOCK_MONOTONIC_RAW, &time_cur);   \
    const int64_t diff = time_diff(time_cur, saved_ts);    \
    const int64_t stats_diff = local_cur_stats - saved_stats;    \
    double d_rate = stats_diff * 1000000000UL / (double)(diff); \
    d_rate += 0.5f;    \
    rate = (uint64_t)d_rate;    \
    saved_ts = time_cur;    \
    saved_stats = local_cur_stats;  \
} while(false)

} // namespace adk