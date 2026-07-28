#include <adk/ticket_lock.h>
#include <string.h>
#include <boost/thread.hpp>

using namespace adk;

volatile bool is_start = false;
void CountingThreadMain(uint64_t* counter, TicketLock* tlock, uint64_t counter_end)
{
    while (!is_start)
        ADK_PAUSE();

    uint64_t counter_begin = 0;

    while ((++counter_begin) <= counter_end)
    {
        tlock->lock();
        ++(*counter);
        tlock->unlock();
    }
}


int main(int argc, char const *argv[])
{
    uint64_t counter = 0;
    TicketLock tlock;
    uint64_t counter_end = 1000000;
    if (argc >= 3)
        counter_end = atol(argv[2]);

    uint32_t nr_threads = 2;
    if (argc >= 2)
        nr_threads = atol(argv[1]);

    std::vector<boost::thread> tvec;
    for (uint32_t i = nr_threads; i > 0; --i)
    {
        tvec.push_back(boost::thread(CountingThreadMain, &counter, &tlock, counter_end));
    }

    sleep(1);
    is_start = true;

    for (auto it = tvec.begin(); it != tvec.end(); ++it)
    {
        it->join();
    }

    std::cout << counter << std::endl;
    if (counter != (counter_end * nr_threads))
        abort();

    return 0;
}
