#define BOOST_TEST_MODULE local_storage_queue

#include <deque>

#include <stdlib.h>

#include <local_storage_queue.h>
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(test_LocalStorageQueue)
{
    using TestType = uint64_t;
    constexpr uint32_t kTestSize = 7;

    adk::LocalStorageQueue<TestType, kTestSize> local_storage_queue;
    BOOST_CHECK_EQUAL(sizeof(local_storage_queue), sizeof(uint64_t) + kTestSize * sizeof(TestType));

    for (uint32_t index = 0; index < kTestSize; ++index)
    {
        BOOST_CHECK_EQUAL(local_storage_queue.length(), index);
        BOOST_CHECK_EQUAL(local_storage_queue.TryPush((TestType)index), adk::ErrorCode::kSuccess);
        BOOST_CHECK_EQUAL(local_storage_queue.length(), index + 1);
    }

    BOOST_CHECK_EQUAL(local_storage_queue.length(), kTestSize);
    BOOST_CHECK_EQUAL(local_storage_queue.TryPush((TestType)kTestSize), adk::ErrorCode::kQueueFull);
    BOOST_CHECK_EQUAL(local_storage_queue.length(), kTestSize);

    TestType value;
    for (uint32_t index = 0; index < kTestSize; ++index)
    {
        BOOST_CHECK_EQUAL(local_storage_queue.length(), kTestSize - index);
        BOOST_CHECK_EQUAL(local_storage_queue.TryPop(value), adk::ErrorCode::kSuccess);
        BOOST_CHECK_EQUAL(local_storage_queue.length(), kTestSize - index - 1);

        BOOST_CHECK_EQUAL(value, (TestType)index);
    }

    BOOST_CHECK_EQUAL(local_storage_queue.length(), 0);

    std::deque<TestType> substitute_deq;
    for (uint32_t index = 0; index < 100000; ++index)
    {
        auto random_value = random();

        BOOST_CHECK_EQUAL(local_storage_queue.length(), substitute_deq.size());

        if (0 == random_value % 2)
        {
            *(uint64_t*)(&value) = random_value;

            if (adk::ErrorCode::kSuccess == local_storage_queue.TryPush(value))
            {
                substitute_deq.push_back(value);
            }
            else
            {
                BOOST_CHECK_EQUAL(local_storage_queue.length(), kTestSize);
            }
        }
        else
        {
            if (adk::ErrorCode::kSuccess == local_storage_queue.TryPop(value))
            {
                BOOST_CHECK_EQUAL(value, substitute_deq.front());
                substitute_deq.pop_front();
            }
            else
            {
                BOOST_CHECK_EQUAL(local_storage_queue.length(), 0);
            }
        }

        BOOST_CHECK_EQUAL(local_storage_queue.length(), substitute_deq.size());
    }

    BOOST_CHECK_EQUAL(local_storage_queue.length(), substitute_deq.size());
}