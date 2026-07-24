#include "../include/adk/arch/synchronize.h"
#include <boost/thread/thread.hpp>
#include <iostream>

#include <sys/mman.h>

int32_t* g_wait_addr = nullptr;

const int32_t test_rounds = 6;

void WaiterThreadMain()
{
    int32_t counter = 0;
    while (++counter <= test_rounds)
    {
        int ret = adk::FutexWait(g_wait_addr, 10);  // g_wait_addr == 10 的时候会被挂起
        *g_wait_addr = 9;
        assert(ret == 0);
        std::cout << "awake, counter = " << counter << ", ret: " << ret << std::endl;

        sleep(2);
        *g_wait_addr = 10;
    }
}


int main()
{
    void* ptr = mmap(NULL, sizeof(int32_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED)
    {
        std::cout << "mmap failed, errno: " << errno << ", str errno: " << std::strerror(errno) << std::endl;
        return 1;
    }
    g_wait_addr = (int32_t*)ptr;
    *g_wait_addr = 10;

    pid_t pid;
    pid = fork();
    if (pid < 0)
    {
        std::cout << "fork failed, errno: " << errno << ", str errno: " << std::strerror(errno) << std::endl;
        return 1;
    }

    // child
    if (pid == 0)
    {
        WaiterThreadMain();
    }
    else
    {
        // std::cin.get();
        int32_t counter = 0;
        while (++counter <= test_rounds)
        {
            sleep(1);
            if (*g_wait_addr == 10)
            {
                std::cout << "go to wake" << std::endl;
                adk::FutexWake(g_wait_addr);
            }
            else
            {
                std::cout << "is not wait" << std::endl;
            }
        }
    }

    while (true)
    {
        sleep(1);
    }
    return 0;
}

