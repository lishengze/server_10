#include "adk.h"

#include <thread>
#include <functional>

volatile bool g_is_start = false;
volatile bool g_need_reset = false;
uint64_t g_sum_ns = 0;
uint64_t g_counter = 0;
uint64_t g_delta = 0;

void Producer(SPSCQueue<uint64_t>* p_to_c, SPSCQueue<uint64_t>* c_to_p)
{
    while (!g_is_start);

    while (1)
    {
        struct timespec ts_begin, ts_now;

        clock_gettime(CLOCK_REALTIME, &ts_begin);
        uint64_t time_begin = ts_begin.tv_sec * 1000000000ul + ts_begin.tv_nsec;
        while (p_to_c->Push(time_begin) != adk::ErrorCode::kSuccess)
        {
            ADK_PAUSE();
        }

        uint64_t time_begin_piggy_back;
        while (c_to_p->Pop(time_begin_piggy_back) != adk::ErrorCode::kSuccess)
        {
            ADK_PAUSE();
        }

        clock_gettime(CLOCK_REALTIME, &ts_now);
        uint64_t time_now = ts_now.tv_sec * 1000000000ul + ts_now.tv_nsec;
        g_sum_ns += time_now - time_begin_piggy_back;
        ++g_counter;

        if (g_need_reset)
        {
            g_need_reset = false;
            g_counter = 0;
            g_sum_ns = 0;
        }
    }
}

void Consumer(SPSCQueue<uint64_t>* p_to_c, SPSCQueue<uint64_t>* c_to_p)
{
    while (!g_is_start);

    uint64_t time_begin = 0;

    while (1)
    {
        while (p_to_c->Pop(time_begin) != adk::ErrorCode::kSuccess)
        {
            ADK_PAUSE();
        }

        while (c_to_p->Push(time_begin) != adk::ErrorCode::kSuccess)
        {
            ADK_PAUSE();
        }
    }
}

void Obverser()
{
    while (!g_is_start);

    while (1)
    {
        if (g_counter > 0)
        {
            uint64_t sum = g_sum_ns;
            uint64_t rate = g_counter;
            auto avg = sum / rate;
            std::cout << "rate = " << rate << ", "
                      << "avg latency = " << (avg - g_delta) / 2 << " ns"
                      << std::endl;    
            g_need_reset = true;
        }
        sleep(1);
    }
}

int main(int argc, char const *argv[])
{

    struct timespec ts_begin, ts_now;
    clock_gettime(CLOCK_REALTIME, &ts_begin);
    uint64_t time_begin = ts_begin.tv_sec * 1000000000ul + ts_begin.tv_nsec;
    uint64_t time_now;
    uint64_t rounds = atol(argv[1]);
    for (uint32_t i = 1; i <= rounds; ++i)
    {
        ADK_BARRIER();
        clock_gettime(CLOCK_REALTIME, &ts_now);
        time_now = ts_now.tv_sec * 1000000000ul + ts_now.tv_nsec;        
    }

    std::cout << "delta = " << (g_delta = (time_now - time_begin)  / 1000000) << std::endl;


    SPSCQueue<uint64_t>* p_to_c1 = SPSCQueue<uint64_t>::Create("producer_to_consumer", 8192);
    SPSCQueue<uint64_t>* p_to_c2 = SPSCQueue<uint64_t>::Duplicate(*p_to_c1);

    SPSCQueue<uint64_t>* c_to_p1 = SPSCQueue<uint64_t>::Create("consumer_to_producer", 8192);
    SPSCQueue<uint64_t>* c_to_p2 = SPSCQueue<uint64_t>::Duplicate(*c_to_p1);

    std::thread p_thread(std::bind(Producer, p_to_c1, c_to_p1));
    std::thread c_thread(std::bind(Consumer, p_to_c2, c_to_p2));
    std::thread o_thread(std::bind(Obverser));

    sleep(1);

    g_is_start = true;

    p_thread.join();

    return 0;
}
