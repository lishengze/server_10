#define BOOST_TEST_MODULE lock_free_queue_variant
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk_pack/lock_free_queue_variant.h>
#include <adk_pack/error_code.h>

#include <set>
#include <string>
#include <vector>
#include <map>

volatile bool is_bug_on = false;
using namespace adk;

template <typename QueueType>
void QConcumser(QueueType* mq, uint64_t total_messages)
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
                return;
            }
        }
    }
}

template <typename QueueType>
void QMConcumser(QueueType* mq, uint64_t total_messages)
{
    uint64_t counter = 0;
    uint64_t prev_val = 0;
    while (counter != total_messages)
    {
        uint64_t value = 0;
        if (mq->Pop(value) == ErrorCode::kSuccess)
        {
            ++counter;
            if (value < prev_val)
            {
                is_bug_on = true;
                return;
            }
            prev_val = value;
        }
    }
}

template <typename QueueType>
void QMPConcumser(QueueType* mq, uint64_t total_messages)
{
    uint64_t counter = 0;
    while (counter != total_messages)
    {
        uint64_t value = 0;
        if (mq->Pop(value) == ErrorCode::kSuccess)
        {
            ++counter;
        }
    }
}

template <typename QueueType>
void QProducer(QueueType* mq, uint64_t total_messages)
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

template <typename QueueType>
void QMProducer(QueueType* mq, uint64_t total_messages)
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
        if (counter % 1000 == 0)
        {
            usleep(0);
        }
    }
}


BOOST_AUTO_TEST_CASE(spscqueue_with_1_instance)
{
    is_bug_on = false;
    variant::SPSCQueue<uint64_t>* mq_p = variant::SPSCQueue<uint64_t>::Create("test_spsc", 8192);

    boost::thread p = boost::thread(QProducer<variant::SPSCQueue<uint64_t>>, mq_p, 600000000);
    boost::thread c = boost::thread(QConcumser<variant::SPSCQueue<uint64_t>>, mq_p, 600000000);

    p.join();
    c.join();
    BOOST_REQUIRE(is_bug_on == false);
}


BOOST_AUTO_TEST_CASE(spmcqueue)
{
    is_bug_on = false;
    variant::SPMCQueue<uint64_t>* mq_p = variant::SPMCQueue<uint64_t>::Create("test_spmc", 8192);

    boost::thread p = boost::thread(QProducer<variant::SPMCQueue<uint64_t>>, mq_p, 600000000);
    boost::thread c1 = boost::thread(QMConcumser<variant::SPMCQueue<uint64_t>>, mq_p, 200000000);
    boost::thread c2 = boost::thread(QMConcumser<variant::SPMCQueue<uint64_t>>, mq_p, 200000000);
    boost::thread c3 = boost::thread(QMConcumser<variant::SPMCQueue<uint64_t>>, mq_p, 200000000);

    p.join();
    c1.join();
    c2.join();
    c3.join();
    BOOST_REQUIRE(is_bug_on == false);
}

BOOST_AUTO_TEST_CASE(mpscqueue)
{
    is_bug_on = false;
    variant::MPSCQueue<uint64_t>* mq_p = variant::MPSCQueue<uint64_t>::Create("test_mpsc", 8192);

    boost::thread p1 = boost::thread(QProducer<variant::MPSCQueue<uint64_t>>, mq_p, 200000000);
    boost::thread p2 = boost::thread(QProducer<variant::MPSCQueue<uint64_t>>, mq_p, 200000000);
    boost::thread p3 = boost::thread(QProducer<variant::MPSCQueue<uint64_t>>, mq_p, 200000000);
    boost::thread c1 = boost::thread(QMPConcumser<variant::MPSCQueue<uint64_t>>, mq_p, 600000000);

    p1.join();
    p2.join();
    p3.join();
    c1.join();
    BOOST_REQUIRE(is_bug_on == false);
}

BOOST_AUTO_TEST_CASE(mpmcqueue)
{
    is_bug_on = false;
    variant::MPMCQueue<uint64_t>* mq_p = variant::MPMCQueue<uint64_t>::Create("test_mpsc", 8192);

    boost::thread p1 = boost::thread(QMProducer<variant::MPMCQueue<uint64_t>>, mq_p, 300000000);
    boost::thread p2 = boost::thread(QMProducer<variant::MPMCQueue<uint64_t>>, mq_p, 300000000);
    boost::thread c1 = boost::thread(QMPConcumser<variant::MPMCQueue<uint64_t>>, mq_p, 300000000);
    boost::thread c2 = boost::thread(QMPConcumser<variant::MPMCQueue<uint64_t>>, mq_p, 300000000);

    p1.join();
    p2.join();
    c1.join();
    c2.join();
    BOOST_REQUIRE(is_bug_on == false);
}