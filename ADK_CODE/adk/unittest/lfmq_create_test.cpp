#define BOOST_TEST_MODULE pipe
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk/pipe.h>
#include <adk/lock_free_msg_queue.h>

#include <set>
#include <string>
#include <vector>
#include <map>

using namespace adk;

BOOST_AUTO_TEST_CASE(MPSCQueue_peek)
{
    auto* queue = MPSCQueue::Create("MPSC", sizeof(uint64_t), 8192);
    BOOST_REQUIRE(queue != nullptr);

    BOOST_REQUIRE(queue->Push(1UL) == ErrorCode::kSuccess);
    BOOST_REQUIRE(queue->Push(2UL) == ErrorCode::kSuccess);

    uint64_t out;
    ((SPMCQueue*)queue)->Pop(out);
    BOOST_REQUIRE(out == 1UL);

    ((SPMCQueue*)queue)->Pop(out);
    BOOST_REQUIRE(out == 2UL);

    auto* queue1 = SPMCQueue::Create("SPMC", sizeof(uint64_t), 8192);
    BOOST_REQUIRE(sizeof(*queue1) == sizeof(*queue));

    auto* queue2 = SPSCQueue<uint64_t>::Create("SPSC", sizeof(uint64_t), 8192);
    BOOST_REQUIRE(sizeof(*queue1) == sizeof(*queue2));

    BOOST_REQUIRE(queue2->Push(4UL) == ErrorCode::kSuccess);
    BOOST_REQUIRE(queue2->Push(3UL) == ErrorCode::kSuccess);

    ((SPMCQueue*)queue2)->Pop(out);
    BOOST_REQUIRE(out == 4UL);    

    ((SPMCQueue*)queue2)->Pop(out);
    BOOST_REQUIRE(out == 3UL);

    SCSequentialQueue* queue3 = SCSequentialQueue::Create("Sequential", sizeof(uint64_t), 8192);
    BOOST_REQUIRE(sizeof(*queue1) == sizeof(*queue3));

    BOOST_REQUIRE(queue3->Push(10UL, 1) == ErrorCode::kSuccess);
    BOOST_REQUIRE(queue3->Push(6UL, 2) == ErrorCode::kSuccess);

    queue3->Pop(out);
    BOOST_REQUIRE(out == 10UL);

    queue3->Pop(out);
    BOOST_REQUIRE(out == 6UL);
}

