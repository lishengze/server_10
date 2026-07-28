#include "../include/adk/arch/synchronize.h"
#include <iostream>
#include <sys/time.h>

int main()
{
    int abc = 0;
    struct timeval tv1, tv2;
    gettimeofday(&tv1, NULL);
    adk::FutexTimedWaitPrivate(&abc, 0, 3000000000ul);
    gettimeofday(&tv2, NULL);
    std::cout << "time wait = " 
              << (tv2.tv_sec - tv1.tv_sec) * 1000000 + tv2.tv_usec - tv1.tv_usec
              << std::endl;;
    return 0;
}

