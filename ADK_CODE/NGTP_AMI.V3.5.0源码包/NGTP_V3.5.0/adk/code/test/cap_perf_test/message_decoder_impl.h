#ifndef CAP_MESSAGE_DECODER_IMPL_H_ 
#define CAP_MESSAGE_DECODER_IMPL_H_

#include "message_decoder.h"

#include <time.h>

#include <iomanip>
#include <iostream>
#include <boost/locale.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace cap
{

class MessageDecoderImpl : public MessageDecoder
{
public:
    MessageDecoderImpl()
    {
        Reset();
    }

    int32_t OnMessage(char* data, uint32_t len) override
    {
        if (ACCESS_ONCE(reset_))
        {
            Reset();
        }

        struct timespec current_tp;
        clock_gettime(CLOCK_REALTIME, &current_tp);

        const char* consume_data = data;
        uint32_t left_len = len;

        do 
        {
            if (left_len < 4)
            {
                break;
            }

            const uint32_t data_len = *(uint32_t*)consume_data;
            if (left_len >= data_len)
            {
                struct timespec* message_tp = (struct timespec*)(consume_data + sizeof(uint32_t));
                const auto time_diff = current_tp.tv_sec * 1000000000 + current_tp.tv_nsec
                    - message_tp->tv_sec * 1000000000 - message_tp->tv_nsec;

                min_ = std::min<uint64_t>(min_, time_diff);
                max_ = std::max<uint64_t>(max_, time_diff);
                total_ += time_diff;
                ++counter_;

                left_len -= data_len;
                consume_data += data_len;
            }
            else
            {
                break;
            }
        } while (true);

        return consume_data - data;
    }

    void PrintStatistics()
    {
        const auto counter = ACCESS_ONCE(counter_);
        if (0 != counter)
        {
            std::cout.precision(3);

            std::cout << boost::posix_time::second_clock::local_time() 
                << " | total:" << std::setw(10) << counter
                << " | avg(us):" << std::setw(10) << std::fixed << (double)ACCESS_ONCE(total_) / (double)(counter * 1000)
                << " | min(us):" << std::setw(10) << std::fixed << (double)ACCESS_ONCE(min_) / (double)1000
                << " | max(us):" << std::setw(10) << std::fixed << (double)ACCESS_ONCE(max_) / (double)1000
                << std::endl;
        }
        else
        {
            std::cout << boost::posix_time::second_clock::local_time() 
                << " | total:" << std::setw(10) << 0
                << " | avg(us):" << std::setw(10) << "NA"
                << " | min(us):" << std::setw(10) << "NA"
                << " | max(us):" << std::setw(10) << "NA"
                << std::endl;
        }
        reset_ = true;
    }

private:
    void Reset()
    {
        min_ = 0xffffffff;
        max_ = 0;
        total_ = 0;
        counter_ = 0;
        reset_ = false;
    }

    uint64_t min_;
    uint64_t max_;
    uint64_t total_;
    uint64_t counter_;
    bool     reset_;
};

}

#endif