#define BOOST_TEST_MODULE pipe
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk_pack/lock_free_msg_queue.h>
#include <adk_pack/error_code.h>

#include <set>
#include <string>
#include <vector>
#include <map>

volatile bool is_bug_on = false;
using namespace adk;


void QConcumser(SPSCQueue<uint64_t>* mq, uint64_t total_messages)
{
    uint64_t counter = 0;
    while (counter != total_messages)
    {
        uint64_t value = 0;
        if (mq->Pop(value) == ErrorCode::kSuccess)
        {
            ++counter;
            if (value != counter)
            {
                is_bug_on = true;
            }
        }
    }
}

void QProducer(SPSCQueue<uint64_t>* mq, uint64_t total_messages)
{
    uint64_t counter = 0;
    while (counter != total_messages)
    {
        ++counter;
        while (mq->Push(counter) != ErrorCode::kSuccess)
        {
            if (is_bug_on)
                return;
        }
    }
}


BOOST_AUTO_TEST_CASE(spscqueue_with_1_instance)
{
    is_bug_on = false;
    SPSCQueue<uint64_t>* mq_p = SPSCQueue<uint64_t>::Create("test_spsc", 8192);

    boost::thread p = boost::thread(QProducer, mq_p, 600000000);
    boost::thread c = boost::thread(QConcumser, mq_p, 600000000);

    p.join();
    c.join();
    BOOST_REQUIRE(is_bug_on == false);
}

