#include <adk/event.h>
#include <adk/util.h>

#include <time.h>

using namespace adk;

int main(int argc, char const *argv[])
{
    Backoff backoff;
    policy::Pause::Init(backoff);

    std::cout << "PauseBackoff:" << std::endl;
    uint32_t counter = 0;
    while (counter != 16)
    {
        ++counter;
        if (counter == 8)
        {
            backoff.Reset();
            uint32_t val = 8192;
            backoff.Config(ADK_BACKOFF_LIMIT, &val, sizeof val);
        }

        struct timespec ts_begin, ts_end;
        clock_gettime(CLOCK_REALTIME, &ts_begin);
        backoff.Run();
        clock_gettime(CLOCK_REALTIME, &ts_end);
        std::cout << "time use : " << time_diff(ts_end, ts_begin) << std::endl;
    }

    std::cout << "DelayBackoff:" << std::endl;
    counter = 0;
    policy::Delay::Init(backoff);
    while (counter != 16)
    {
        ++counter;
        if (counter == 8)
        {
            backoff.Reset();
            uint32_t val = 1000;
            backoff.Config(ADK_BACKOFF_DELAY, &val, sizeof val);
        }

        struct timespec ts_begin, ts_end;
        clock_gettime(CLOCK_REALTIME, &ts_begin);
        backoff.Run();
        clock_gettime(CLOCK_REALTIME, &ts_end);
        std::cout << "time use : " << time_diff(ts_end, ts_begin) << std::endl;
    }

    std::cout << "usleep : " << std::endl;
    for (uint32_t i = 0; i < 4; ++i)
    {
        struct timespec ts_begin, ts_end;
        clock_gettime(CLOCK_REALTIME, &ts_begin);
        usleep(100);
        clock_gettime(CLOCK_REALTIME, &ts_end);
        std::cout << "time use : " << time_diff(ts_end, ts_begin) << std::endl;    
    }
    

    return 0;
}

