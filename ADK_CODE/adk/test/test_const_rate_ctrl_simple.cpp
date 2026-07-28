#include <adk/rate_control_simple.h>

#include <iostream>
#include <boost/thread.hpp>

#ifdef __GNUC__
#include <stdlib.h>
#else
#include <windows.h>
#endif

inline void DoDelay()
{
#ifdef __GNUC__
	usleep(100000);
#else
	Sleep(100);
#endif
}

int main(int argc, char* argv[])
{
    uint32_t rate_limit = 10000;
    if (argc > 1)
    {
        rate_limit = atoi(argv[1]);
    }

    std::cout << "test limit rate = " << rate_limit << std::endl;

    adk::ConstRateCtrlSimple* const rate_control = adk::ConstRateCtrlSimple::NewInstance(rate_limit);

    uint64_t acquire_counter = 0;
    boost::thread _thread = boost::thread([&]() {
        do
        {
            if (rate_control->TryAcquire())
            {
                ++acquire_counter;
            }
            else
            {
                boost::thread::yield();
            }
        } while (true);
    });

    uint64_t acquire_counter_rec = 0;
    do 
    {
        DoDelay();
        const uint64_t temp_counter = *(volatile uint64_t*)(&acquire_counter);
        std::cout << "acquire counter = " << temp_counter - acquire_counter_rec << std::endl;
        acquire_counter_rec = temp_counter;
    } while (true);

    return 0;
}