#include <adk/log.h>
#include <iostream>
#include <ctime>

int main()
{
	adk::log::IntervalLogger logger(5);
	std::cout << time(0) << std::endl;
	int32_t counter = 0;
	while(counter < 3)
	{
		if (logger.ToLog())
        	{
                	std::cout << time(0) << std::endl;
        		++counter;
		}
	}
	return 0;
}
