#include <time.h>
#include <string>
#include <thread>
#include <iomanip>
#include <iostream>

#include <adk/error_code.h>
#include <adk/lock_free_queue_variant.h>

struct Element
{
    struct timespec timepoint;
    uint64_t value1;
    uint64_t value2;
    uint64_t value3;
};

int main()
{
    volatile bool reset = false;
    volatile uint64_t total_nr = 0;
    volatile uint64_t latency_min = std::numeric_limits<uint64_t>::max();
    volatile uint64_t latency_max = 0;
    volatile uint64_t latency_total = 0;

    auto* const queue = adk::variant::MPSCQueue<Element>::Create("", 8192);
    std::thread consumer = std::thread([&]() {
        struct timespec timepoint;
        adk::variant::VariantEntry* entry_ptr;
        do
        {
            if (ADK_UNLIKELY(adk::ErrorCode::kSuccess != queue->TryWaitEntry(&entry_ptr)))
            {
                ADK_PAUSE();
                continue;
            }

            char* const buffer = entry_ptr->buffer;
            const auto& timepoint_b = ((Element*)buffer)->timepoint;
            clock_gettime(CLOCK_REALTIME, &timepoint);
            const uint64_t time_diff = (timepoint.tv_sec  - timepoint_b.tv_sec) * 1000000000
                                     + timepoint.tv_nsec - timepoint_b.tv_nsec;
            queue->FreeEntry(entry_ptr);

            if (ADK_UNLIKELY(reset))
            {
                total_nr = 1;
                latency_min = time_diff;
                latency_max = time_diff;
                latency_total = time_diff;
                reset = false;
            }
            else
            {
                ++total_nr;
                if (ADK_UNLIKELY(time_diff < latency_min))
                {
                    latency_min = time_diff;
                }

                if (ADK_UNLIKELY(time_diff > latency_max))
                {
                    latency_max = time_diff;
                }

                latency_total += time_diff;
            }
        } while (true);
    });

    std::thread ob([&]() {
        do
        {
            sleep(1);

            const auto temp_total_nr = total_nr;
            const auto temp_latency_min = latency_min;
            const auto temp_latency_max = latency_max;
            const auto temp_latency_total = latency_total;
            if (temp_total_nr > 0)
            {
                std::cout << "total:" << std::setw(10) << temp_total_nr
                    << " | avg(ns):" << std::setw(10) << temp_latency_total / temp_total_nr
                    << " | min(ns):" << std::setw(10) << temp_latency_min
                    << " | max(ns):" << std::setw(10) << temp_latency_max
                    << std::endl;
            }
            else
            {
                std::cout << "total:" << std::setw(10) << 0
                    << " | avg(ns):" << std::setw(10) << "NA"
                    << " | min(ns):" << std::setw(10) << "NA"
                    << " | max(ns):" << std::setw(10) << "NA"
                    << std::endl;
            }

            reset = true;
        } while (true);
    });

    uint64_t counter = 0;
    adk::variant::VariantEntry* entry_ptr;
    while (true)
    {
        if (adk::ErrorCode::kSuccess == queue->AllocEntry(&entry_ptr))
        {
            auto* const buffer = entry_ptr->buffer;
            clock_gettime(CLOCK_REALTIME, &((Element*)buffer)->timepoint);
            ((Element*)buffer)->value1 = ++counter;
            ((Element*)buffer)->value2 = ++counter;
            ((Element*)buffer)->value3 = ++counter;
            queue->PostEntry(entry_ptr);
        }

        for (int32_t index = 0; index < 32; ++index)
        {
            ADK_PAUSE();
        }
    }

    return 0;
}