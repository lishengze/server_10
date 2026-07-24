#include <unistd.h>
#include <stdint.h>
#include <iostream>

#include <adk/high_performance_clock.h>

int main(int argc, char const *argv[])
{
	uint64_t tp_1 = adk::tick::Read();
	sleep(1);
	uint64_t tp_2 = adk::tick::Read();
	std::cout << adk::tick::Diff<adk::tick::kMilliseconds>(tp_2, tp_1) << std::endl;

	std::cout << adk::tick::Diff<adk::tick::kMicroseconds>(tp_2, tp_1) << std::endl;

	std::cout << adk::tick::Diff<adk::tick::kNanoseconds>(tp_2, tp_1) << std::endl;

	tp_1 = adk::tick::Read();
	for (int i = 0; i < 10; ++i) {}
	tp_2 = adk::tick::Read();

	std::cout << adk::tick::Diff<adk::tick::kNanoseconds>(tp_2, tp_1) << std::endl;	

	std::cout << adk::tick::DiffAsDouble<adk::tick::kNanoseconds>(tp_2, tp_1) << std::endl;
	return 0;
}