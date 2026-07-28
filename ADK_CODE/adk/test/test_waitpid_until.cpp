#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>

#include <adk/util.h>

void child()
{
    sleep(30);
    std::cout << "[child] exit" << std::endl;
}

int main()
{
    pid_t pid = fork();
    
    if (pid == 0)
    {
        // child process
        child();
        return 0;
    }
    else if (pid < 0)
    {
        // error
        std::cout << "fork error" << std::endl;
    }
    else
    {
        // parent process
        std::cout << "[parent] fork child: " << pid << std::endl;
        uint64_t timeout = 50000000000;     // 50s
        adk::WaitPidUntil(pid, timeout);
    }
    
    std::cout << "[parent] exit" << std::endl;
    return 0;
}