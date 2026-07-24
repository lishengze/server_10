#define BOOST_TEST_MODULE lock_free_unbounded_queue_variant
#include <boost/test/included/unit_test.hpp>

#include <adk_pack/error_code.h>
#include <adk_pack/lock_free_unbounded_queue_variant.h>

#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

volatile bool is_bug_on = false;
using namespace adk::variant;
using adk::ErrorCode;

void QConcumser(MPSCUnboundedQueue<uint64_t>* mq, uint64_t total_msgs)
{
    uint64_t counter = 0;
    while (counter != total_msgs)
    {
        uint64_t value = 0;
        if (mq->Pop(value) == ErrorCode::kSuccess)
        {
            ++counter;
        }
    }
}

void QProducer(MPSCUnboundedQueue<uint64_t>* mq, uint64_t total_msgs)
{
    uint64_t counter = 0;
    while (counter != total_msgs)
    {
        ++counter;
        while (mq->Push(counter) != ErrorCode::kSuccess)
        {
            is_bug_on = true;
            return;
        }
    }
}

/**
 * @brief MPSCUnboundedQueue 队列多线程生产 单线程消费测试样例
 * 
 */
BOOST_AUTO_TEST_CASE(spscqueue_with_1_instance)
{
    constexpr uint64_t kProducerNumber = 6;
    constexpr uint64_t kElementCounter = 100000000;

    std::cout << "start MPSCUnboundedQueue test, element counter: " << kElementCounter
              << "\t thread num: " << kProducerNumber << std::endl;
    is_bug_on = false;
    MPSCUnboundedQueue<uint64_t>* mq_p = MPSCUnboundedQueue<uint64_t>::Create("test_spsc");
    BOOST_REQUIRE(mq_p->length() == 0);

    std::vector<std::thread> p_vec;
    // 创建多个线程，竞争同一个队列的入队操作
    for (int i = 0; i < kProducerNumber; ++i)
    {
        // std::thread p = std::thread(QProducer, mq_p, 600000000);
        p_vec.push_back(std::thread(QProducer, mq_p, kElementCounter));
    }
    BOOST_REQUIRE(mq_p->length() >= 0);
    BOOST_REQUIRE(mq_p->length() <= kProducerNumber * kElementCounter);

    std::thread c = std::thread(QConcumser, mq_p, kProducerNumber * kElementCounter);

    for (std::thread& thrd : p_vec)
    {
        thrd.join();
    }
    BOOST_REQUIRE(mq_p->length() < kProducerNumber * kElementCounter);

    c.join();
    BOOST_REQUIRE(mq_p->length() == 0);

    BOOST_REQUIRE(is_bug_on == false);
}
