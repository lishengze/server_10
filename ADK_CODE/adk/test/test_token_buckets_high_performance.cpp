#include <adk/token_buckets.h>

#include <iomanip>
#include <iostream>
#include <boost/format.hpp>

int main()
{
    adk::TokenBucket* const token_buken = adk::RateControl::GetInstance<adk::rate_unit::Second>(10000000, true);
    assert(token_buken);

    bool   reset = false;
    double max = 0;
    double min = 0xffffffff;
    double avg = 0;
    uint64_t counter = 0;
    uint64_t rc_counter = 0;

    std::thread observer = std::thread([&]() {
        while (true)
        {
            sleep(1);
            const auto _counter = ACCESS_ONCE(counter);
            if (0 != _counter)
            {
                std::cout.precision(3);
                const double _avg = ACCESS_ONCE(avg) / _counter;
                std::cout << "total:" << std::setw(10) << _counter
                    << " | rc:" << std::setw(10) << rc_counter
                    << " | avg(us):" << std::setw(10) << std::fixed << _avg
                    << " | min(us):" << std::setw(10) << std::fixed << min
                    << " | max(us):" << std::setw(10) << std::fixed << max
                    << std::endl;
            }
            else
            {
                std::cout << "total:" << std::setw(10) << 0
                    << " | counter:" << std::setw(10) << "NA"
                    << " | avg(us):" << std::setw(10) << "NA"
                    << " | min(us):" << std::setw(10) << "NA"
                    << " | max(us):" << std::setw(10) << "NA"
                    << std::endl;
            }
            reset = true;
        }
    });

    struct timespec time_point1;
    struct timespec time_point2;

    do
    {
        if (ACCESS_ONCE(reset))
        {
            max = 0;
            min = 0xffffffff;
            avg = 0;
            counter = 0;
            rc_counter = 0;
            reset = false;
        }


        uint64_t acq_counter = 0;
        clock_gettime(CLOCK_REALTIME, &time_point1);

        const uint64_t _time_point1 = time_point1.tv_sec * 1000000000 + time_point1.tv_nsec;

        while (true)
        {
            clock_gettime(CLOCK_REALTIME, &time_point2);
            if (time_point2.tv_sec * 1000000000 + time_point2.tv_nsec - _time_point1 > 1000)
            {
                break;
            }

            ADK_PAUSE();
        }

        while (adk::ErrorCode::kSuccess == token_buken->TryAcquire(1))
        {
            ++acq_counter;
        }

        if (0 != acq_counter)
        {
            clock_gettime(CLOCK_REALTIME, &time_point2);
            const uint64_t time_diff = time_point2.tv_sec * 1000000000 + time_point2.tv_nsec - _time_point1;

            const double acq_permicro = double(acq_counter * 1000) / time_diff;
            max = std::max<double>(max, acq_permicro);
            min = std::min<double>(min, acq_permicro);
            avg += acq_permicro;
            rc_counter += acq_counter;
            ++counter;
        }
    } while (true);
}