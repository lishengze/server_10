#include <adk/lock_free_msg_queue.h>
#include <adk/util.h>

#include <iostream>
#include <boost/thread/thread.hpp>

using namespace adk;

struct AppData
{
    uint64_t seq __attribute__((aligned(32)));
};

uint64_t g_counter = 0;

void Producer(SPSCQueue<AppData>* mq, uint64_t begin, uint64_t nr_pro)
{
    AppData data;
    data.seq = begin;
    while (1)
    {
        while (mq->ReorderPush(data, data.seq) != ErrorCode::kSuccess)
        {
            for (uint32_t i = 64; i != 0; --i)
                ADK_PAUSE();
        }
        data.seq += nr_pro;
    }
}

char padding[128];
uint64_t g_counter2 = 0;
void Consumer(SPSCQueue<AppData>* mq)
{
    AppData data;
    while (1)
    {
        ++g_counter2;
        while (mq->ReorderPop(data) != ErrorCode::kSuccess)
        {
            for (uint32_t i = 16; i != 0; --i)
                ADK_PAUSE();
        }

        if (data.seq != g_counter2)
        {
            std::cout << "Bug on, g_counter2 = " << g_counter2
                      << ", seq = " << data.seq << std::endl;;
            sleep(100000);
        }
    }
}

void Observer(SPSCQueue<AppData>* mq)
{
    uint64_t saved_counter = 0;
    while (1)
    {
        QueueStats stats;
        mq->GetStats(stats);
        uint64_t temp = g_counter2;
        std::cout << "counter_diff = " << temp - saved_counter << ", "
                  << "nr_failed = " << stats.nr_forward_fail << ", "
                  << "max_length = " << stats.max_queue_length << ", "
                  << "counter = " << temp
                  << std::endl;
        saved_counter = temp;
        sleep(1);
    }
}

int main(int argc, char const *argv[])
{
    SPSCQueue<AppData>* qp1 = SPSCQueue<AppData>::Create("test", 65535);
    SPSCQueue<AppData>* qp2 = SPSCQueue<AppData>::Duplicate(*qp1);
    SPSCQueue<AppData>* qc = SPSCQueue<AppData>::Duplicate(*qp1);

    boost::thread thread1 = boost::thread(boost::bind(Consumer, qc));
    // boost::thread thread2 = boost::thread(boost::bind(Producer, qp1, 1, 1));
    boost::thread thread2 = boost::thread(boost::bind(Producer, qp1, 1, 2));
    boost::thread thread3 = boost::thread(boost::bind(Producer, qp2, 2, 2));
    boost::thread thread4 = boost::thread(boost::bind(Observer, qc));
    thread1.join();
    return 0;
}

