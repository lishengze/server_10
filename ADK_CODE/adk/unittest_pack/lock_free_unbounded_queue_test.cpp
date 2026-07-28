#define BOOST_TEST_MODULE lock_free_unbounded_queue
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk_pack/error_code.h>
#include <adk_pack/lock_free_unbounded_queue.h>

#include <map>
#include <set>
#include <string>
#include <vector>

volatile bool is_bug_on = false;
using namespace adk;

void QConcumser(SPSCUnboundedQueue<uint64_t>* mq, uint64_t total_msgs)
{
    uint64_t counter = 0;
    while (counter != total_msgs)
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

void QProducer(SPSCUnboundedQueue<uint64_t>* mq, uint64_t total_msgs)
{
    uint64_t counter = 0;
    while (counter != total_msgs)
    {
        ++counter;
        while (mq->Push(counter) != ErrorCode::kSuccess)
        {
            if (is_bug_on)
                return;
        }
    }
}

/**
 * @brief SPSC 无限长队列单线程入队 单线程出队测试样例
 * 
 */
BOOST_AUTO_TEST_CASE(spscqueue_with_1_instance)
{
    std::cout << "start SPSCUnboundedQueue test, element counter: " << 600000000 << std::endl;
    is_bug_on = false;
    SPSCUnboundedQueue<uint64_t>* mq_p = SPSCUnboundedQueue<uint64_t>::Create("test_spsc");

    boost::thread p = boost::thread(QProducer, mq_p, 600000000);
    boost::thread c = boost::thread(QConcumser, mq_p, 600000000);

    p.join();
    c.join();
    BOOST_REQUIRE(is_bug_on == false);
}

/**
 * @brief SPSC 无限长队列随机访问接口测试
 * 
 */
BOOST_AUTO_TEST_CASE(spscqueue_access)
{
    std::cout << "spscqueue_access" << std::endl;

    SPSCUnboundedQueue<uint64_t>* mq = SPSCUnboundedQueue<uint64_t>::Create("test_unbounded_queue", 8, 64);
    BOOST_REQUIRE(mq->Head() == NULL);
    BOOST_REQUIRE(mq->ElementAt(0) == NULL);
    BOOST_REQUIRE(mq->ElementAt(1) == NULL);

    // 从 1 到 260 放入队列
    uint64_t counter = 0;
    while (++counter <= 260)
    {
        mq->Push(counter);
    }

    // 队列的head 从1 开始，  0 返回NULL
    BOOST_REQUIRE(mq->ElementAt(0) == NULL);
    BOOST_REQUIRE(mq->ElementAt(261) == NULL);
    BOOST_REQUIRE(*(mq->ElementAt(1)) == 1);
    BOOST_REQUIRE(*(mq->ElementAt(260)) == 260);

    BOOST_REQUIRE(*(mq->Head()) == 1);

    mq->Pop();

    BOOST_REQUIRE(*(mq->Head()) == 2);
    BOOST_REQUIRE(*(mq->Head()) == 2);

    uint64_t counter2 = 2;
    while (true)
    {
        ++counter2;
        mq->Pop();
        if (counter2 == counter)
            break;
        // 出队后检查 HEAD
        BOOST_REQUIRE(*(mq->Head()) == counter2);
    }

    BOOST_REQUIRE(mq->Head() == NULL);
    // 清空队列后再次入队
    while (counter <= 512)
    {
        mq->Push(counter);
        ++counter;
    }
    BOOST_REQUIRE(*(mq->Head()) == 261);

    BOOST_REQUIRE(mq->ElementAt(260) == NULL);
    BOOST_REQUIRE(mq->ElementAt(513) == NULL);
    BOOST_REQUIRE(*(mq->ElementAt(261)) == 261);
    BOOST_REQUIRE(*(mq->ElementAt(512)) == 512);
}

BOOST_AUTO_TEST_CASE(spscqueue_foreach)
{
    std::cout << "spscqueue_foreach" << std::endl;

    SPSCUnboundedQueue<uint64_t>* queue = SPSCUnboundedQueue<uint64_t>::Create("test_unbounded_queue");
    BOOST_REQUIRE(queue);

    for (uint64_t index = 0; index < 10000000; ++index)
    {
        BOOST_REQUIRE(ErrorCode::kSuccess == queue->Push(index));
    }
    
    uint64_t counter = 0;
    uint64_t check_counter = 0;
    queue->ForeachElement([&](uint64_t* value) {
        BOOST_REQUIRE(*value == check_counter);

        BOOST_REQUIRE(check_counter < 10000);

        return ++check_counter < 10000;
    });

    BOOST_REQUIRE(check_counter == 10000);

    while (counter + 1 < check_counter)
    {
        BOOST_REQUIRE(queue->Pop(counter) == ErrorCode::kSuccess);
    }

    queue->ForeachElement([&](uint64_t* value) {
        BOOST_REQUIRE(*value == check_counter);

        BOOST_REQUIRE(check_counter < 1000000);

        return ++check_counter < 1000000;
    });

    BOOST_REQUIRE(check_counter == 1000000);

    while (counter + 1 < check_counter)
    {
        BOOST_REQUIRE(queue->Pop(counter) == ErrorCode::kSuccess);
    }

    queue->ForeachElement([&](uint64_t* value) {
        BOOST_REQUIRE(*value == check_counter);
        ++check_counter;
        return true;
    });

    BOOST_REQUIRE(check_counter == 10000000);
}