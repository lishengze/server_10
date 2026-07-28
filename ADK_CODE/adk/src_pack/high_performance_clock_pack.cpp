#include <adk/high_performance_clock.h>
#include <adk_pack/high_performance_clock.h>

namespace adk
{

namespace tick
{

uint64_t TicksPerSec()
{
    return adk_impl::tick::g_ticks_per_sec;
}

double TicksPerSecDouble()
{
    return adk_impl::tick::g_ticks_per_sec_double;
}

uint64_t Read()
{
    return adk_impl::tick::Read();
}

uint64_t div_rem(uint64_t dividend, uint64_t divisor)
{
    return adk_impl::tick::div_rem(dividend, divisor);
}

uint64_t div_rem(uint64_t dividend, uint64_t divisor, uint64_t *remainder)
{
    return adk_impl::tick::div_rem(dividend, divisor, remainder);
}

namespace impl
{

uint64_t div_rem_sec(uint64_t dividend)
{
    return adk_impl::tick::div_rem(dividend, adk_impl::tick::g_ticks_per_sec);
}

uint64_t div_rem_milli(uint64_t dividend)
{
    return adk_impl::tick::div_rem(dividend * 1000, adk_impl::tick::g_ticks_per_sec);
}

uint64_t div_rem_micro(uint64_t dividend)
{
    return adk_impl::tick::div_rem(dividend * 1000000, adk_impl::tick::g_ticks_per_sec);
}

uint64_t div_rem_nano(uint64_t dividend)
{
    if (dividend < adk_impl::tick::g_ticks_per_sec)
    {
        return adk_impl::tick::div_rem(dividend * 1000000000, adk_impl::tick::g_ticks_per_sec);
    }
    else
    {
        uint64_t remainder;
        uint64_t ret = adk_impl::tick::div_rem(dividend, adk_impl::tick::g_ticks_per_sec, &remainder);
        return adk_impl::tick::div_rem(remainder * 1000000000, adk_impl::tick::g_ticks_per_sec)
            + ret * 1000000000;
    }
}

double diff_double_sec(uint64_t dividend)
{
    return (dividend / adk_impl::tick::g_ticks_per_sec_double);
}

double diff_double_milli(uint64_t dividend)
{
    return ((dividend * 1000UL) / adk_impl::tick::g_ticks_per_sec_double);
}

double diff_double_micro(uint64_t dividend)
{
    return ((dividend * 1000000UL) / adk_impl::tick::g_ticks_per_sec_double);
}

double diff_double_nano(uint64_t dividend)
{
    if (dividend < adk_impl::tick::g_ticks_per_sec_double)
    {
        return (dividend * 1000000000UL / adk_impl::tick::g_ticks_per_sec_double);
    }
    else
    {
        return (dividend % adk_impl::tick::g_ticks_per_sec) * 1000000000UL / adk_impl::tick::g_ticks_per_sec_double
            + dividend / adk_impl::tick::g_ticks_per_sec_double * 1000000000UL;
    }
}

uint64_t get_tick_now()
{
    return adk_impl::GetTSC();
}

uint64_t get_tick_arrive(uint64_t nsec)
{
    return adk_impl::GetTSC() + (nsec / 1000000000) * adk_impl::tick::g_ticks_per_sec
        + (nsec % 1000000000) * adk_impl::tick::g_ticks_per_sec / 1000000000;
}

void tick_delay_until(uint64_t ticks_arrive)
{
    while (adk_impl::GetTSC() < ticks_arrive);
}

}

void Adjust(uint64_t ticks, int seconds)
{
    adk_impl::tick::Adjust(ticks, seconds);
}

void Adjust()
{
    adk_impl::tick::Adjust();
}

}

}