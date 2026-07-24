#include <adk/util.h>

#include <boost/thread/thread.hpp>

volatile uint64_t g_counter = 0;
volatile bool g_is_app_running = true;

#define TARGE_MSG_RATE_300K      300000
#define TARGE_MSG_RATE_1         1
#define TARGE_MSG_RATE_10K       10000
#define TARGE_MSG_RATE_1M        1000000

void ThreadMain_300K()
{
    adk::SimpleRateController<> rate_ctrl(TARGE_MSG_RATE_300K);

    const uint64_t total_messages = 10 * TARGE_MSG_RATE_300K;
    while ((++g_counter) < total_messages)
    {
        rate_ctrl.Wait();
    }

    g_is_app_running = false;
}

void ThreadMain_1()
{
    adk::SimpleRateController<> rate_ctrl(TARGE_MSG_RATE_1);

    const uint64_t total_messages = 10 * TARGE_MSG_RATE_1;
    while ((++g_counter) < total_messages)
    {
        rate_ctrl.Wait();
    }

    g_is_app_running = false;
}

void ThreadMain_10K()
{
    adk::SimpleRateController<> rate_ctrl(TARGE_MSG_RATE_10K);

    const uint64_t total_messages = 10 * TARGE_MSG_RATE_10K;
    while ((++g_counter) < total_messages)
    {
        rate_ctrl.Wait();
    }

    g_is_app_running = false;
}

void ThreadMain_1M()
{
    adk::SimpleRateController<> rate_ctrl(TARGE_MSG_RATE_1M);

    const uint64_t total_messages = 10 * TARGE_MSG_RATE_1M;
    while ((++g_counter) < total_messages)
    {
        rate_ctrl.Wait();
    }

    g_is_app_running = false;
}

void ThreadMain_1M_BURST()
{
    adk::SimpleRateController<10> rate_ctrl(TARGE_MSG_RATE_1M);

    const uint64_t total_messages = 10 * TARGE_MSG_RATE_1M;
    while ((++g_counter) < total_messages)
    {
        rate_ctrl.Wait();
    }

    g_is_app_running = false;
}


int main(int argc, char const *argv[])
{
    boost::thread io_thread;

    g_counter = 0;
    g_is_app_running = true;
    std::cout << "rate = " << TARGE_MSG_RATE_300K << std::endl;
    io_thread = boost::thread(boost::bind(ThreadMain_300K));
    while (g_is_app_running)
    {
        sleep(1);
        std::cout << "g_counter = " << g_counter << std::endl;
    }
    io_thread.join();

    g_counter = 0;
    g_is_app_running = true;
    std::cout << "rate = " << TARGE_MSG_RATE_1 << std::endl;
    io_thread = boost::thread(boost::bind(ThreadMain_1));
    while (g_is_app_running)
    {
        sleep(1);
        std::cout << "g_counter = " << g_counter << std::endl;
    }
    io_thread.join();

    g_counter = 0;
    g_is_app_running = true;
    std::cout << "rate = " << TARGE_MSG_RATE_10K << std::endl;
    io_thread = boost::thread(boost::bind(ThreadMain_10K));
    while (g_is_app_running)
    {
        sleep(1);
        std::cout << "g_counter = " << g_counter << std::endl;
    }
    io_thread.join();

    g_counter = 0;
    g_is_app_running = true;
    std::cout << "rate = " << TARGE_MSG_RATE_1M << std::endl;
    io_thread = boost::thread(boost::bind(ThreadMain_1M));
    while (g_is_app_running)
    {
        sleep(1);
        std::cout << "g_counter = " << g_counter << std::endl;
    }
    io_thread.join();

    g_counter = 0;
    g_is_app_running = true;
    std::cout << "rate = " << TARGE_MSG_RATE_1M << ", burst = 10"<< std::endl;
    io_thread = boost::thread(boost::bind(ThreadMain_1M_BURST));
    while (g_is_app_running)
    {
        sleep(1);
        std::cout << "g_counter = " << g_counter << std::endl;
    }
    io_thread.join();
    return 0;
}

