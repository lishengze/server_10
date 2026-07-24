#define BOOST_TEST_MODULE lock_free_unbounded_queue
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk/error_code.h>
#include <adk/lock_free_unbounded_queue.h>

#include <map>
#include <set>
#include <string>
#include <vector>

volatile bool is_bug_on = false;
using namespace adk;

constexpr uint64_t kTestCounter = 10000000ull * 6;
constexpr uint64_t kTestCounterWithDelay = 1000000 * 600;
constexpr int kMaxQueueThreadNum = 8;

struct QDate
{
    int64_t tid;
    uint64_t counter;
};


/**
 * spsc队列消费端
 * QueueType代表队列类型，kDoDelay为true代表开启延时
*/
template<typename QueueType, bool kDoDelay>
void QConcumser(QueueType* mq, uint64_t total_msgs)
{
    uint64_t value;
    uint64_t counter = 0;
    while (counter != total_msgs)
    {
        if (kDoDelay)
        {
            usleep(0);
        }

        if (mq->Pop(value) == ErrorCode::kSuccess)
        {
            if (value != counter + 1)
            {
                std::cout << "value: " << value << " counter + 1: " << counter + 1 << std::endl;
                abort();// 当发送来的消息与线程中的计数不符时代表存在数据丢失
                is_bug_on = true;
            }

            counter = value;
        }
    }
}

/**
 * spsc队列发送端
 * QueueType代表队列类型，kDoDelay为true代表开启延时
*/
template<typename QueueType, bool kDoDelay>
void QProducer(QueueType* mq, uint64_t total_msgs)
{
    uint64_t counter = 0;
    while (counter != total_msgs)
    {
        if (kDoDelay)
        {
            usleep(0);
        }

        ++counter;
        while (mq->Push(counter) != ErrorCode::kSuccess)
        {
            abort();
            is_bug_on = true;
        }
    }
}


/**
 * @brief SPSC 无限长队列单线程入队 单线程出队
 * 
 */
BOOST_AUTO_TEST_CASE(spscqueue_test)
{
    std::cout << "start SPSCUnboundedQueue test, element counter: " << 600000000 << std::endl;
    is_bug_on = false;
   boost::thread p[10];
    boost::thread c[10];
    // 多线程测试，同时创建10个线程跑同样的逻辑
    for (uint32_t index = 0; index < 10; ++index)
    {
        SPSCUnboundedQueue<uint64_t>* mq_p = SPSCUnboundedQueue<uint64_t>::Create("test_spsc");

        p[index] = boost::thread(QProducer<SPSCUnboundedQueue<uint64_t>, false>, mq_p, 600000000);
        c[index] = boost::thread(QConcumser<SPSCUnboundedQueue<uint64_t>, false>, mq_p, 600000000);
    }
    for (uint32_t index = 0; index < 10; ++index)
    {
        p[index].join();
	    c[index].join();
    }
    BOOST_REQUIRE(is_bug_on == false);
}


/**
 * @brief SPSC 无限长队列,慢出队
 * 
 */
BOOST_AUTO_TEST_CASE(slow_pop)
{
    std::cout << "start SPSCUnboundedQueue test, element counter: " << 600000 << std::endl;
    is_bug_on = false;
    boost::thread p[10];
    boost::thread c[10];
    // 多线程测试，同时创建10个线程跑同样的逻辑
    for (uint32_t index = 0; index < 10; ++index)
    {
        SPSCUnboundedQueue<uint64_t>* mq_p = SPSCUnboundedQueue<uint64_t>::Create("test_spsc");

        p[index] = boost::thread(QProducer<SPSCUnboundedQueue<uint64_t>, false>, mq_p, 600000);
        c[index] = boost::thread(QConcumser<SPSCUnboundedQueue<uint64_t>, true>, mq_p, 600000);
    }
    for (uint32_t index = 0; index < 10; ++index)
    {
        p[index].join();
	    c[index].join();
    }

    BOOST_REQUIRE(is_bug_on == false);
}

/**
 * @brief SPSC 无限长队列单线程入队 单线程出队,慢入队
 * 
 */
BOOST_AUTO_TEST_CASE(slow_push)
{
    std::cout << "start SPSCUnboundedQueue test, element counter: " << 600000 << std::endl;
    is_bug_on = false;
    boost::thread p[10];
    boost::thread c[10];
    // 多线程测试，同时创建10个线程跑同样的逻辑
    for (uint32_t index = 0; index < 10; ++index)
    {
        SPSCUnboundedQueue<uint64_t>* mq_p = SPSCUnboundedQueue<uint64_t>::Create("test_spsc");

        p[index] = boost::thread(QProducer<SPSCUnboundedQueue<uint64_t>, true>, mq_p, 600000);
        c[index] = boost::thread(QConcumser<SPSCUnboundedQueue<uint64_t>, false>, mq_p, 600000);
    }
    for (uint32_t index = 0; index < 10; ++index)
    {
        p[index].join();
	    c[index].join();
    }
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