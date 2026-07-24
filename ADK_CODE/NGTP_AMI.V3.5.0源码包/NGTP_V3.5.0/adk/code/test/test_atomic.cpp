#include <adk/util.h>
#include <boost/thread/thread.hpp>
#include <iostream>

#define TOTAL_OPS 100000000UL

void DoIncrease(uint64_t* var, uint64_t end)
{
    while (end != 0)
    {
        --end;
        adk::atomic_inc(*var);
    }
}

void DoDecrease(uint64_t* var, uint64_t end)
{
    while (end != 0)
    {
        --end;
        adk::atomic_dec(*var);
    }
}

#define BUG_ON std::cout << "bug on !!!!!!" << std::endl;

int main(int argc, char const *argv[])
{
    std::cout << "test atomic_inc" << std::endl;
    {
        uint64_t var = 0;
        boost::thread t1 = boost::thread(boost::bind(DoIncrease, &var, TOTAL_OPS));
        boost::thread t2 = boost::thread(boost::bind(DoIncrease, &var, TOTAL_OPS));
        t1.join();
        t2.join();
        if (var != (TOTAL_OPS << 1))
        {
            BUG_ON;
        }
    }
    
    std::cout << "test atomic_dec" << std::endl;
    {
        uint64_t var = TOTAL_OPS << 1;
        boost::thread t1 = boost::thread(boost::bind(DoDecrease, &var, TOTAL_OPS));
        boost::thread t2 = boost::thread(boost::bind(DoDecrease, &var, TOTAL_OPS));
        t1.join();
        t2.join();
        if (var != 0)
        {
            BUG_ON;
        }
    }
    return 0;
}