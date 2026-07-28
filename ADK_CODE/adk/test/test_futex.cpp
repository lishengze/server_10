#include "../include/adk/arch/synchronize.h"
#include <boost/thread/thread.hpp>
#include <iostream>

int32_t g_wait_addr = 0;

const int32_t test_rounds = 6;

void WaiterThreadMain()
{
    int32_t counter = 0;
    while (++counter <= test_rounds)
    {
        int ret = adk::FutexWaitPrivate(&g_wait_addr, 0);
        assert(ret == 0);
        std::cout << "awake, counter = " << counter << std::endl;
    }
}


int main()
{
    boost::thread waiter_thread = boost::thread(boost::bind(&WaiterThreadMain));
    int32_t counter = 0;
    while (++counter <= test_rounds)
    {
        sleep(1);
        adk::FutexWakePrivate(&g_wait_addr);
    }

    waiter_thread.join();
    return 0;
}

