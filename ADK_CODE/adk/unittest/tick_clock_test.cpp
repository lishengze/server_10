#define BOOST_TEST_MODULE tick_clock
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk/high_performance_clock.h>

#include <set>
#include <string>
#include <vector>
#include <map>

using namespace adk;

BOOST_AUTO_TEST_CASE(InitAndBasic)
{
	// 调整tick到人可读时间的转换关系
	tick::TickClock tc;
	int32_t counter = 60;
	while ((--counter) >= 0)
	{
		usleep(200);
		tc.Adjust();
	}

	// 校验经过以上调整，已经稳定
	uint64_t tps_now_0 = tc.tick_per_sec();	
	tc.Adjust();
	uint64_t tps_now_1 = tc.tick_per_sec();	
	tc.Adjust();
	uint64_t tps_now_2 = tc.tick_per_sec();	
	tc.Adjust();
	uint64_t tps_now_3 = tc.tick_per_sec();	
	tc.Adjust();
	uint64_t tps_now_4 = tc.tick_per_sec();	

	BOOST_REQUIRE(abs((long int)(tps_now_1 - tps_now_0)) < 2000);
	BOOST_REQUIRE(abs((long int)(tps_now_2 - tps_now_1)) < 2000);
	BOOST_REQUIRE(abs((long int)(tps_now_3 - tps_now_2)) < 2000);
	BOOST_REQUIRE(abs((long int)(tps_now_4 - tps_now_3)) < 2000);
	std::cout << "tc.tick_per_sec() = " << tc.tick_per_sec() << std::endl;

	// 和clock_gettime比较纳秒级的准确性
	struct timespec ts_begin, ts_end;
	clock_gettime(CLOCK_MONOTONIC, &ts_begin);
	uint64_t tk_begin, tk_end;
	tk_begin = GetTSC();
	for (int32_t i = 0; i < 10000; ++i)
	{
		ADK_PAUSE();
	}
	tk_end = GetTSC();
	clock_gettime(CLOCK_MONOTONIC, &ts_end);

	uint64_t nano_clock_gettime = (ts_end.tv_sec - ts_begin.tv_sec) * 1000000000ul + ts_end.tv_nsec - ts_begin.tv_nsec;
	uint64_t nano_tick_clock = tc.ToNanoseconds(tk_end - tk_begin);

	std::cout << "nano_clock_gettime = " << nano_clock_gettime << " nano_tick_clock = " << nano_tick_clock << std::endl;
	BOOST_REQUIRE(abs((long int)(nano_tick_clock - nano_clock_gettime)) < 1000);

	// 和gettimeofday比较微秒级的准确性
	struct timeval tv_begin, tv_end;
	gettimeofday(&tv_begin, nullptr);
	tk_begin = GetTSC();
	for (int32_t i = 0; i < 10000; ++i)
	{
		ADK_PAUSE();
	}
	tk_end = GetTSC();
	gettimeofday(&tv_end, nullptr);
	uint64_t us_gettimeofday = (tv_end.tv_sec - tv_begin.tv_sec) * 1000000 + tv_end.tv_usec - tv_begin.tv_usec;
	uint64_t us_tick = tc.ToMicroseconds(tk_end - tk_begin);

	BOOST_REQUIRE(abs((long int)(us_tick - us_gettimeofday)) < 3);

	// 注意太小的循环在CI docker环境中可能会因为调度而失败
	if (::getenv("AMI_DOCKER_UNIT_TEST") == nullptr)
	{
		uint32_t sucess = 0;
		for (uint32_t i = 0; i <= 100; ++i)
		{
			clock_gettime(CLOCK_MONOTONIC, &ts_begin);
			tk_begin = tick::SyncRead();
			for (int32_t i = 0; i < 64; ++i) 
			{
				ADK_BARRIER();
			}
			clock_gettime(CLOCK_MONOTONIC, &ts_end);
			tk_end = tick::SyncRead();	

			uint64_t a = (ts_end.tv_sec - ts_begin.tv_sec) * 1000000000ul + ts_end.tv_nsec - ts_begin.tv_nsec;
			uint64_t b = tc.ToNanoseconds(tk_end - tk_begin);
			std::cout << "a = " << a << " b = " << b << std::endl;
			if (abs((long int)(a - b)) < 100)
			{
				++sucess;
			}
		}
		BOOST_REQUIRE(sucess >= 95);
	}
}

