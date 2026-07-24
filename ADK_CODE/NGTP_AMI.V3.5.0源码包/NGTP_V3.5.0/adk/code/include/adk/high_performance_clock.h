#ifndef ADK_IMPL_HIGH_PERFORMANCE_CLOCK_H_
#define ADK_IMPL_HIGH_PERFORMANCE_CLOCK_H_

#include <string>

#include "arch/generic.h"

namespace adk_impl
{

namespace tick
{

extern uint64_t g_ticks_per_sec;
extern double g_ticks_per_sec_double;

inline uint64_t Read()
{
	return GetTSC();
}

inline uint64_t SyncRead()
{
	ADK_MB();
	uint64_t ret = GetTSC();
	return ret;
}

const int kSecond = 0;
const int kMilliseconds = 1;
const int kMicroseconds = 2;
const int kNanoseconds = 3;

/*
 * kernel/include/linux/math64.h: __iter_div_u64_rem()
 */
inline uint64_t div_rem(uint64_t dividend, uint64_t divisor)
{
	uint64_t ret = 0;

	while (dividend >= divisor)
	{
		/* The following asm() prevents the compiler from
		   optimising this loop into a modulo operation */
		asm volatile ("" : "+rm" (dividend));

		dividend -= divisor;
		ret++;
	}

	return ret;
}

inline uint64_t div_rem(uint64_t dividend, uint64_t divisor, uint64_t *remainder)
{
	uint64_t ret = 0;

	while (dividend >= divisor)
	{
		/* The following asm() prevents the compiler from
		optimising this loop into a modulo operation */
		asm volatile ("" : "+rm" (dividend));

		dividend -= divisor;
		ret++;
	}

	*remainder = dividend;

	return ret;
}

template<int precise>
inline uint64_t Diff(uint64_t end, uint64_t begin)
{
	uint64_t diff = end - begin;
	if (precise == kSecond)
	{
		return div_rem(diff, g_ticks_per_sec);
	}
	if (precise == kMilliseconds)
	{
		return div_rem(diff * 1000, g_ticks_per_sec);
	}
	if (precise == kMicroseconds)
	{
		return div_rem(diff * 1000000, g_ticks_per_sec);
	}
	if (precise == kNanoseconds)
	{
		if (diff < g_ticks_per_sec)
		{
			return div_rem(diff * 1000000000, g_ticks_per_sec);
		}
		else
		{
			uint64_t remainder;
			uint64_t ret = div_rem(diff, g_ticks_per_sec, &remainder);
			return div_rem(remainder * 1000000000, g_ticks_per_sec)
				+ ret * 1000000000;
		}
	}
}

template<int precise>
inline double DiffAsDouble(uint64_t end, uint64_t begin)
{
	uint64_t diff = end - begin;
	if (precise == kSecond)
	{
		return (diff / g_ticks_per_sec_double);
	}
	if (precise == kMilliseconds)
	{
		return ((diff * 1000UL) / g_ticks_per_sec_double);
	}
	if (precise == kMicroseconds)
	{
		return ((diff * 1000000UL) / g_ticks_per_sec_double);
	}
	if (precise == kNanoseconds)
	{
		if (diff < g_ticks_per_sec_double)
		{
			return (diff * 1000000000UL / g_ticks_per_sec_double);
		}
		else
		{
			return (diff % g_ticks_per_sec) * 1000000000UL / g_ticks_per_sec_double
				+ diff / g_ticks_per_sec_double * 1000000000UL;
		}
	}
}

void Adjust(uint64_t ticks, int seconds);
void Adjust();

template<uint64_t waterline = 100000000>
inline void ndelay(uint64_t nsec)
{
	uint64_t now = GetTSC();
	uint64_t time_arrive = now + (nsec / 1000000000) * g_ticks_per_sec
		+ (nsec % 1000000000) * g_ticks_per_sec / 1000000000;
	if (nsec > waterline)
	{
		uint64_t diff = nsec - waterline;
		struct timespec ts;
		ts.tv_sec = diff / 1000000000;
		ts.tv_nsec = diff % 1000000000;
		nanosleep(&ts, NULL);
	}
	while (GetTSC() < time_arrive);
}

template<uint64_t waterline = 100000000>
inline void delay_until(uint64_t ticks_arrive)
{
	uint64_t now = GetTSC();
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
	while (GetTSC() < ticks_arrive);
}

/**
 * @brief      将CPU的时钟滴答数转换成人可读的时间
 */
class TickClock
{
public:
	TickClock();

	/**
	 * @brief      调整精度，每次调整的间隔在1秒~5秒具有最好的效果
	 */
	void Adjust();

	/**
	 * @brief      将Tick转纳秒
	 *
	 * @param[in]  tick  tick值
	 *
	 * @return     纳秒值
	 */
	uint64_t ToNanoseconds(uint64_t tick)
	{
		return tick * 1000ul / tick_per_us_;
	}

	/**
	 * @brief      Tick转微秒
	 *
	 * @param[in]  tick  tick值
	 *
	 * @return     微秒值
	 */
	uint64_t ToMicroseconds(uint64_t tick)
	{
		return tick / tick_per_us_;
	}

	uint64_t tick_per_sec() { return tick_per_sec_; }

private:
	uint64_t tick_per_sec_;
	uint64_t tick_per_us_;
	uint64_t last_tick_saved_;
	struct timespec last_ts_saved_;
};

}

} // adk

#endif // ADK_HIGH_PERFORMANCE_CLOCK_H_
