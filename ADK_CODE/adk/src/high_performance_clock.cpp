#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

#include <map>
#include <iostream>

#include <boost/algorithm/string.hpp>
#include <adk/high_performance_clock.h>

#include <vector>

namespace adk_impl
{

namespace tick
{
#define ADK_READ_PROC_BUFFER_SIZE (1024*64)

uint64_t g_ticks_per_sec = 0;
double g_ticks_per_sec_double = 0.0;

class GetCpuInfo
{
public:
	GetCpuInfo()
	{
	#if (defined __GNUC__ && defined __x86_64__)
		FILE* fp = fopen("/proc/cpuinfo", "r");
		if (fp == NULL)
		{
			fprintf(stderr, "Failure: open \"/proc/cpuinfo\" failed\n");
			return;
		}
		char read_buf[ADK_READ_PROC_BUFFER_SIZE];
		size_t line_size = ADK_READ_PROC_BUFFER_SIZE;
		char* buf_ptr = read_buf;
		while (getline(&buf_ptr, &line_size, fp) > 0)
		{
			std::vector<std::string> splits;
			boost::split(splits, buf_ptr, boost::is_any_of(":"), boost::token_compress_on);
			if (splits.size() < 2)
				continue;

			boost::algorithm::trim(splits[0]);
			boost::algorithm::trim(splits[1]);

			if (splits[0] == "model name")
			{
				std::vector<std::string> splits2;
				boost::split(splits2, splits[1], boost::is_any_of(" "), boost::token_compress_on);
				uint64_t hz_unit = 1000 * 1000 * 1000;
				for (char c : splits2[splits2.size() - 1])
				{
					if (c >= '0' && c <= '9')
					{
						g_ticks_per_sec = g_ticks_per_sec + (c - '0') * hz_unit;
					}
					else if (c == '.')
					{
						hz_unit = 1000 * 1000 * 100;
						continue;
					}
					else if (c == 'G')
					{
						break;
					}
				}
				break;
			}

			line_size = ADK_READ_PROC_BUFFER_SIZE;
			buf_ptr = read_buf;
		}

		fclose(fp);

        // support more cpu, i.e. E312xx
		if (!g_ticks_per_sec)
        {
            uint64_t begin = Read();				// FIXME : using gettimeofday to delay?
            sleep(1);
            uint64_t end = Read();
            g_ticks_per_sec = end - begin;
        }

		g_ticks_per_sec_double = (double)g_ticks_per_sec;
	#elif (defined __GNUC__ && defined __aarch64__)
		g_ticks_per_sec = 1000000000UL;
		g_ticks_per_sec_double = (double)g_ticks_per_sec;
	#endif
	}

	~GetCpuInfo()
	{}
} g_get_cpu_info;

void Adjust(uint64_t ticks, int seconds)
{
	g_ticks_per_sec = (g_ticks_per_sec * 70 + (ticks * 30 / seconds) ) / 100;
}

static struct timeval 	g_last_tv_saved;
static uint64_t 	  	g_last_tick_saved;

void Adjust()
{
	if (ADK_UNLIKELY(g_last_tv_saved.tv_sec == 0))
	{
		gettimeofday(&g_last_tv_saved, NULL);
		g_last_tick_saved = tick::Read();
		return;
	}
	struct timeval	tv_current;
	gettimeofday(&tv_current, NULL);
	uint64_t tick_current = tick::Read();
	uint64_t microseconds = (tv_current.tv_sec - g_last_tv_saved.tv_sec) * 1000000UL + tv_current.tv_usec - g_last_tv_saved.tv_usec;
	if (microseconds > 1000000UL && microseconds < 5000000UL)
	{
		g_ticks_per_sec = (g_ticks_per_sec * 70 + ((tick_current - g_last_tick_saved) * 1000000UL * 30 / microseconds) ) / 100;
	}

	g_last_tick_saved = tick_current;
	g_last_tv_saved = tv_current;
}

TickClock::TickClock()
{
	tick_per_sec_ = g_ticks_per_sec;
	tick_per_us_ = g_ticks_per_sec / 1000000UL;

	if (tick_per_sec_ == 0)
		tick_per_sec_ = 1000000000ul;

	if (tick_per_us_ == 0)
		tick_per_us_ = 1000ul;

	last_tick_saved_ = 0;
	memset(&last_ts_saved_, 0, sizeof(last_ts_saved_));
}

void TickClock::Adjust()
{
	if (ADK_UNLIKELY(last_ts_saved_.tv_sec == 0))
	{
		clock_gettime(CLOCK_MONOTONIC_RAW, &last_ts_saved_);
		last_tick_saved_ = tick::SyncRead();
		return;
	}
	struct timespec tv_current;
	clock_gettime(CLOCK_MONOTONIC_RAW, &tv_current);
	uint64_t tick_current = tick::SyncRead();

	uint64_t nanoseconds = (tv_current.tv_sec - last_ts_saved_.tv_sec) * 1000000000UL + tv_current.tv_nsec - last_ts_saved_.tv_nsec;
	if (nanoseconds > 200000UL && nanoseconds < 5000000000UL)
	{
		uint64_t new_value = (tick_current - last_tick_saved_) * 1000000000UL / nanoseconds;
		tick_per_sec_ = (tick_per_sec_ * 70 + new_value * 30) / 100;
		tick_per_us_ = tick_per_sec_ / 1000000UL;
		
		if (tick_per_sec_ == 0)
			tick_per_sec_ = 1000000000ul;

		if (tick_per_us_ == 0)
			tick_per_us_ = 1000ul;
	}

	last_tick_saved_ = tick_current;
	last_ts_saved_ = tv_current;
}

} // ticks
} // adk

