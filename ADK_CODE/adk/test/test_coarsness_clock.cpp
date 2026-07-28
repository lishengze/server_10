#include <adk/coarsness_clock.h>
#include <iostream>

int main(int argc, char const *argv[])
{
    adk::CoarsnessClock::Start();

    uint32_t counter = 0;
    if (argc == 1)
    while (1)
    {
        std::cout << adk::CoarsnessClock::Now<adk::Nanosecond>() << std::endl;
        sleep(1);
        std::cout << "stats : " << adk::CoarsnessClock::GetClockStats().nr_update_times << std::endl;
    }

    if (argc == 2 && *argv[1] == 'm')
    while (1)
    {
        std::cout << adk::CoarsnessClock::Now<adk::Millisecond>() << std::endl;
        usleep(100000);
        ++counter;
        if (counter == 10)
        {
            counter = 0;
            std::cout << "stats : " << adk::CoarsnessClock::GetClockStats().nr_update_times << std::endl;
        }
    }

    if (argc == 2 && *argv[1] == 'u')
    while (1)
    {
        std::cout << adk::CoarsnessClock::Now<adk::Microsecond>() << std::endl;
        usleep(100000);
        ++counter;
        if (counter == 10)
        {
            counter = 0;
            std::cout << "stats : " << adk::CoarsnessClock::GetClockStats().nr_update_times << std::endl;
        }
    }

    int32_t exit = 3;
    if (argc == 2 && *argv[1] == 'e')
    while ((--exit) >= 0)
    {
        std::cout << adk::CoarsnessClock::Now<adk::Microsecond>() << std::endl;
        usleep(100000);
        ++counter;
        if (counter == 10)
        {
            counter = 0;
            std::cout << "stats : " << adk::CoarsnessClock::GetClockStats().nr_update_times << std::endl;
        }
    }
    adk::CoarsnessClock::Stop();
    return 0;
}