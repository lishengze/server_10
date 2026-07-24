#include <iostream>
#include <adk/perf_measure.h>
#include <boost/thread/thread.hpp>

using namespace adk;

// ADK_DEFINE_PERF_DURATIONS(kP1);

// ADK_DEFINE_PERF_DURATIONS(kP2)

volatile bool is_running = true;

// // ADK_DEFINE_PERF_DURATIONS((kM1)(kM2))
// ADK_DEFINE_PERF_DURATIONS(kM1, kM2);

ADK_DEFINE_PERF_THREAD("TMain", ADK_DEFINE_PERF_DURATION_SEQ(kM1, kM2, kM3))

void TMain()
{
	std::map<std::string, std::string> smap;
	std::map<int, int> imap;
	// perf::RegisterThread("TMain")
	// 					("record_point1", perf::duration::kP1);
	perf::ThreadOnline("TMain", "TMain_1");
	uint32_t counter = 0;
	while (is_running)
	{
		struct timespec ts_begin, ts_end;
		clock_gettime(CLOCK_REALTIME, &ts_begin);
		// perf::ScopedSave save_guard;
		ADK_PERF_SAVE_GUARD();
		// SaveBegin();
		{
			// perf::ScopedCount<perf::duration::kP1> count_guard;
			ADK_PERF_COUNT_GUARD(perf::duration::kM1);
			// CountBegin(perf::kP1);
			
			// CountEnd(perf::kP1);
			{
				ADK_PERF_COUNT_GUARD(perf::duration::kM2);
				smap.emplace(std::to_string(counter), std::to_string(counter));
			}

			{
				ADK_PERF_COUNT_GUARD(perf::duration::kM3);
				imap.emplace((int)random(), (int)random());
			}
		}
		clock_gettime(CLOCK_REALTIME, &ts_end);

		// SaveEnd();

		struct timeval tv1, tv2;
		gettimeofday(&tv1, NULL);
		usleep(1000000);
		gettimeofday(&tv2, NULL);
		std::cout << (tv2.tv_sec - tv1.tv_sec) * 1000000 + tv2.tv_usec - tv1.tv_usec << std::endl;
		std::cout << (ts_end.tv_sec - ts_begin.tv_sec) * 1000000 + ts_end.tv_nsec - ts_begin.tv_nsec << std::endl;
	}
}

// void InitFunc()
// {
// 	std::cout << "InitFunc" << std::endl;
// 	perf::RegisterThread("In3rd")
// 						("3rd_record_p1", perf::duration::kP2);
// }



void TMain2()
{
	while (is_running)
	{
		perf::ThreadOnline3rd("TMain", "TMain_2");
		perf::ScopedSave save_guard;
		// SaveBegin();
		{
			perf::ScopedCount<perf::duration::kM2> count_guard;
			// CountingBegin(perf::duration::kP1);
			// CountingEnd(perf::duration::kP1);
		}
	}
}

int main(int argc, char const *argv[])
{
	perf::SetDataFilePath("./perf_result");

	int a = perf::duration::kM1;
	int b = perf::duration::kM2;
	std::cout << a << " "<< b << std::endl;
	// return 0;
	
	boost::thread test_thread1 = boost::thread(boost::bind(TMain));
	boost::thread test_thread2 = boost::thread(boost::bind(TMain2));

	// int32_t counter = 5;
	while (1)
	{
		// uint64_t begin = tick::Read();
		sleep(1);
		// uint64_t end = tick::Read();
		// tick::Adjust(end - begin, 1);
		tick::Adjust();
		// counter--;
		// if (counter == 0)
		// {
		// 	perf::StopService();
		// 	break;
		// }
	}
	test_thread1.join();
	
	is_running = 0;
	// test_thread2.join();	
	return 0;
}




