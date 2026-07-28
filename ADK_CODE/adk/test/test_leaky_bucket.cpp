#include <adk/leaky_bucket.h>
#include <unistd.h>
#include <stdlib.h>
#include <iostream>

using namespace adk_impl;
 
int main(int argc, char* argv[])
{
	if (argc != 3)
	{
		std::cout << "please put in leak_interval_micro and per_leaky_length" << std::endl;
		return 1;
	}

	int leak_interval_micro = atoi(argv[1]);
	int per_leaky_length = atoi(argv[2]);

	LeakyBucket leakybucket(leak_interval_micro, per_leaky_length);
	
	char buffer[1024*1024] = {0};
	
	int couter = 0;
	int length = 1024* 1024;
	int leak_len = 0;

	struct timespec last_tp;
	struct timespec cur_tp;
	clock_gettime(CLOCK_REALTIME, &last_tp);
	do
	{
		leak_len = leakybucket.leak(length);
		clock_gettime(CLOCK_REALTIME, &cur_tp);
		std::cout << ++couter << ":"
				  << leak_len << " - " 
				  << cur_tp.tv_sec * 1000000000 + cur_tp.tv_nsec - last_tp.tv_sec * 1000000000 - last_tp.tv_nsec
				  << std::endl;

		//消费指定长度
		last_tp = cur_tp;
		length -= leak_len;

	}while (leak_len != 0);

	return 0;
}
