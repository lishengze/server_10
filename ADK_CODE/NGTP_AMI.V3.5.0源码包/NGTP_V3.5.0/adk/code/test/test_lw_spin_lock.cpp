#include <adk/arch/synchronize.h>
#include <boost/thread/thread.hpp>
#include <time.h>

using namespace adk;

uint64_t g_counter = 0;

LightWeightSpinLock lw_lock;

void Adder(uint64_t end)
{
    uint64_t i = 0;
    while (++i <= end)
    {
        lw_lock.lock();
        ++g_counter;
        lw_lock.unlock();
    }
}

#define TEST_END 100000000ul

int main(int argc, char const *argv[])
{
    boost::thread t1 = boost::thread(boost::bind(Adder, TEST_END));
    boost::thread t2 = boost::thread(boost::bind(Adder, TEST_END));

    t1.join();
    t2.join();

    assert(g_counter == 2 * TEST_END);

    LightWeightSpinLock lw_lock;
    int32_t min = ((1u << 31) - 1), max = 0;
    struct timespec ts_b, ts_e;
    int64_t sum = 0;
    for (uint64_t i = 0 ; i < TEST_END; ++i)
    {
        clock_gettime(CLOCK_REALTIME, &ts_b);
        lw_lock.lock();
        lw_lock.unlock();
        ADK_MB();
        clock_gettime(CLOCK_REALTIME, &ts_e);
        int32_t diff = ts_e.tv_sec * 1000000000ul + ts_e.tv_nsec - 
                       ts_b.tv_sec * 1000000000ul - ts_b.tv_nsec;
        sum += diff;
        max = std::max(max, diff);
        min = std::min(min, diff);
    }

    std::cout << "avg = " << sum / TEST_END << " min = " << min << " max = " << max << std::endl;
    return 0;
}

