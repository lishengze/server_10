/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_HIGH_PERFORMANCE_CLOCK_H_
#define ADK_HIGH_PERFORMANCE_CLOCK_H_

#include <string>

namespace adk
{

namespace tick
{

uint64_t Read();

const int kSecond = 0;
const int kMilliseconds = 1;
const int kMicroseconds = 2;
const int kNanoseconds = 3;

uint64_t div_rem(uint64_t dividend, uint64_t divisor);

uint64_t div_rem(uint64_t dividend, uint64_t divisor, uint64_t *remainder);

namespace impl
{

uint64_t div_rem_sec(uint64_t dividend);

uint64_t div_rem_milli(uint64_t dividend);

uint64_t div_rem_micro(uint64_t dividend);

uint64_t div_rem_nano(uint64_t dividend);

double diff_double_sec(uint64_t dividend);

double diff_double_milli(uint64_t dividend);

double diff_double_micro(uint64_t dividend);

double diff_double_nano(uint64_t dividend);

uint64_t get_tick_now();

uint64_t get_tick_arrive(uint64_t nsec);

void tick_delay_until(uint64_t ticks_arrive);

}

template<int precise>
inline uint64_t Diff(uint64_t end, uint64_t begin)
{
    uint64_t diff = end - begin;
    if (precise == kSecond)
    {
        return impl::div_rem_sec(diff);
    }
    if (precise == kMilliseconds)
    {
        return impl::div_rem_milli(diff);
    }
    if (precise == kMicroseconds)
    {
        return impl::div_rem_micro(diff);
    }
    if (precise == kNanoseconds)
    {
        return impl::div_rem_nano(diff);
    }
}

template<int precise>
inline double DiffAsDouble(uint64_t end, uint64_t begin)
{
    uint64_t diff = end - begin;
    if (precise == kSecond)
    {
        return impl::diff_double_sec(diff);
    }
    if (precise == kMilliseconds)
    {
        return impl::diff_double_milli(diff);
    }
    if (precise == kMicroseconds)
    {
        return impl::diff_double_micro(diff);
    }
    if (precise == kNanoseconds)
    {
        return impl::diff_double_nano(diff);
    }
}

void Adjust(uint64_t ticks, int seconds);

void Adjust();

template<uint64_t waterline = 100000000>
inline void ndelay(uint64_t nsec)
{
    uint64_t time_arrive = impl::get_tick_arrive(nsec);
    if (nsec > waterline)
    {
        uint64_t diff = nsec - waterline;
        struct timespec ts;
        ts.tv_sec = diff / 1000000000;
        ts.tv_nsec = diff % 1000000000;
        nanosleep(&ts, NULL);
    }
    impl::tick_delay_until(time_arrive);
}

template<uint64_t waterline = 100000000>
inline void delay_until(uint64_t ticks_arrive)
{
    uint64_t now = impl::get_tick_now();
    if (ticks_arrive <= now)
    {
        return;
    }
    uint64_t nsec = Diff<kNanoseconds>(ticks_arrive, now);
    if (nsec > waterline)
    {
        uint64_t diff = nsec - waterline;
        struct timespec ts;
        ts.tv_sec = diff / 1000000000;
        ts.tv_nsec = diff % 1000000000;
        nanosleep(&ts, NULL);
    }
    impl::tick_delay_until(ticks_arrive);
}

}

} // adk

#endif // ADK_HIGH_PERFORMANCE_CLOCK_H_
