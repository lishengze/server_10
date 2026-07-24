#define BOOST_TEST_MODULE pipe
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk_pack/error_code.h>
#include <adk_pack/lock_free_msg_queue.h>

#include <map>
#include <set>
#include <string>
#include <vector>

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
            // 检查每个元素是否符合预期
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
            // 如果出错 提前退出
            if (is_bug_on)
                return;
        }
    }
}

/**
 * @brief SPSC 队列单线程入队 单线程出队测试样例
 * 
 */
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
