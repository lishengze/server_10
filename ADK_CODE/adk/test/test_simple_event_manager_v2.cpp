#include <iostream>

#include <adk/lock_free_msg_queue.h>
#include <adk/error_code.h>
#include <adk/arch/generic.h>
#include <adk/event.h>

#include <boost/thread/thread.hpp>

using namespace adk;

volatile bool g_is_start = false;
void Producer(SPSCQueue<uint64_t>* mq, SimpleEventManager* ev_man)
{
    while (!g_is_start);
    uint64_t backoff = 0;
    uint64_t local_counter = 0;
    while (1)
    {
        ++local_counter;
        // backoff = 1;
        // while (ev_man->TryNotify([mq, local_counter](){
        //             return mq->Push(local_counter);
        //         }) != adk::ErrorCode::kSuccess)
        // {
        //     for (uint32_t i = 0; i < backoff; ++i)
        //         ADK_PAUSE();
        //     backoff <<= 1;
        // }
        ev_man->Notify([mq, local_counter](){
                    return mq->Push(local_counter);
                });
    }
}


uint64_t g_counter = 0;
void Consumer(SPSCQueue<uint64_t>* mq, SimpleEventManager* ev_man)
{
    while (!g_is_start);

    // uint64_t counter = 0;
    // uint64_t recv_counter = 0;

    adk::Entry* entry;
    while (1)
    {
        ++g_counter;
        ev_man->Wait([mq, &entry](){
            return mq->WaitEntry(&entry);
        });

        if (ADK_UNLIKELY(g_counter != *((uint64_t*)entry->buffer)))
        {
            std::cout << "BUG ON!" << std::endl;
            sleep(100000);
        }

        mq->FreeEntry(entry);
    }
}

void Obverser(SPSCQueue<uint64_t>* mq, SimpleEventManager* ev_man)
{
    while (!g_is_start);

    uint64_t saved_counter = 0;
    while (1)
    {
        QueueStats stats;
        mq->GetStats(stats);
        uint64_t temp = g_counter;
        std::cout << "============================================================" << std::endl;
        std::cout << "counter_diff = " << temp - saved_counter << ", "
                  << "nr_failed = " << stats.nr_forward_fail << ", "
                  << "max_length = " << stats.max_queue_length
                  << std::endl;

        SimpleEveManStats em_stat;
        ev_man->GetStats(em_stat);
        std::cout << "direct_success = " << em_stat.direct_success << ", "
                  << "poll_rounds = " << em_stat.poll_rounds << ", "
                  << "poll_success = " << em_stat.poll_success << ", "
                  << "number_waits = " << em_stat.number_waits
                  << std::endl;
        saved_counter = temp;
        sleep(1);
    }
}

int main(int argc, char const *argv[])
{
    SPSCQueue<uint64_t>* mq_p = SPSCQueue<uint64_t>::Create("test_spsc", 8192);
    SPSCQueue<uint64_t>* mq_c = SPSCQueue<uint64_t>::Duplicate(*mq_p);
    SimpleEventManager ev_man;

    boost::thread p_thread = boost::thread(boost::bind(Producer, mq_p, &ev_man));
    boost::thread c_thread = boost::thread(boost::bind(Consumer, mq_c, &ev_man));
    boost::thread o_thread = boost::thread(boost::bind(Obverser, mq_p, &ev_man));

    sleep(1);

    g_is_start = true;

    p_thread.join();

    return 0;
}
