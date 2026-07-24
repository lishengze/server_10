#include <adk/coarsness_clock.h>
#include <iostream>
#include <boost/thread/thread.hpp>

int main(int argc, char const *argv[])
{
    boost::asio::io_service io_service;
    boost::asio::io_service::work io_service_work(io_service);
    boost::thread io_loop_thread = boost::thread(boost::bind(&boost::asio::io_service::run, &io_service));

    adk::CoarsnessClock::Run(io_service);

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
    int64_t time_value = adk::CoarsnessClock::Now<adk::Microsecond>();
    for (uint32_t i = 0; i < 10; ++i)
    {
        usleep(100000);
        assert(time_value == adk::CoarsnessClock::Now<adk::Microsecond>());
    }
    io_service.stop();
    io_loop_thread.join();
    return 0;
}