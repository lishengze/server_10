#include <time.h>
#include <stdlib.h>
#include <adk/token_buckets.h>

#include <vector>
#include <thread>
#include <iostream>

void DoDelay()
{
    for (uint32_t index = 0; index < 100; ++index)
    {
        ADK_PAUSE();
    }
}

int main(int argc, char* argv[])
{
    uint32_t rate_ctrl = 10000;
    uint32_t timespan = 1;
    if (argc > 1)
    {
        rate_ctrl = atoi(argv[1]);
    }

    if (argc > 2)
    {
        timespan = atoi(argv[2]);
    }

    auto* const_rate_ctrl = adk::RateControl::GetInstance(rate_ctrl, timespan);

    constexpr uint32_t kRateBucketSize = 1000;
    std::vector<uint64_t> rate_bucket(kRateBucketSize);
    std::vector<uint64_t> rate_bucket_rec(kRateBucketSize);

    std::thread consumer_actor = std::thread([&]() {
        const uint32_t bucket_span = timespan * 1000000000 / kRateBucketSize;

        struct timespec time_start;
        struct timespec time_current;
        clock_gettime(CLOCK_REALTIME, &time_current);
        do 
        {
            while (adk::ErrorCode::kSuccess == const_rate_ctrl->TryAcquire(1))
            {
                clock_gettime(CLOCK_REALTIME, &time_current);
                const uint64_t time_diff = time_current.tv_sec * 1000000000 + time_current.tv_nsec - time_start.tv_sec * 1000000000 - time_start.tv_nsec;
                ++rate_bucket[((time_diff / bucket_span) % kRateBucketSize)];

                DoDelay();
            }
        } while (true);
    });

    do
    {
        usleep(100000);

        uint32_t max = 0;
        uint32_t min = (uint32_t)(-1);
        uint32_t total = 0;

        for (uint32_t index = 0; index < kRateBucketSize; ++index)
        {
            const auto value = rate_bucket[index];
            auto& value_rec = rate_bucket_rec[index];
            const auto diff = value - value_rec;
            value_rec = value;

            max = std::max<uint32_t>(max, diff);
            min = std::min<uint32_t>(min, diff);
            total += diff;
        }

        std::cout << "rate control info: total <" << total
                  << ">\t min <" << min
                  << ">\t max <" << max << ">" << std::endl;
    } while (true);

    return 0;
}