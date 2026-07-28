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
    auto* queue = MPSCQueue::Create("peek", sizeof(uint64_t), 8192);
    BOOST_REQUIRE(queue != nullptr);

    BOOST_REQUIRE(queue->Push(1UL) == ErrorCode::kSuccess);
    BOOST_REQUIRE(queue->Push(2UL) == ErrorCode::kSuccess);

    adk::Entry* entry;
    BOOST_REQUIRE(queue->WaitEntry<MPSCQueue::peek>(&entry) == ErrorCode::kSuccess);
    char* buffer = entry->buffer;
    BOOST_REQUIRE(*(uint64_t*)buffer == 1UL);

    entry = nullptr;
    BOOST_REQUIRE(queue->WaitEntry<MPSCQueue::peek>(&entry) == ErrorCode::kSuccess);
    buffer = entry->buffer;
    BOOST_REQUIRE(*(uint64_t*)buffer == 1UL);

    entry = nullptr;
    BOOST_REQUIRE(queue->WaitEntry<MPSCQueue::peek>(&entry) == ErrorCode::kSuccess);
    buffer = entry->buffer;
    BOOST_REQUIRE(*(uint64_t*)buffer == 1UL);

    BOOST_REQUIRE(queue->FreeEntry<MPSCQueue::peek>(entry) == ErrorCode::kSuccess);
    
    entry = nullptr;
    BOOST_REQUIRE(queue->WaitEntry<MPSCQueue::peek>(&entry) == ErrorCode::kSuccess);
    buffer = entry->buffer;
    BOOST_REQUIRE(*(uint64_t*)buffer == 2UL);

    BOOST_REQUIRE(queue->FreeEntry<MPSCQueue::peek>(entry) == ErrorCode::kSuccess);

    entry = nullptr;
    BOOST_REQUIRE(queue->WaitEntry<MPSCQueue::peek>(&entry) != ErrorCode::kSuccess);
}

BOOST_AUTO_TEST_CASE(MPSCQueue_pop)
{
    auto* queue = MPSCQueue::Create("peek", sizeof(uint64_t), 8192);
    BOOST_REQUIRE(queue != nullptr);

    BOOST_REQUIRE(queue->Push(1UL) == ErrorCode::kSuccess);
    BOOST_REQUIRE(queue->Push(2UL) == ErrorCode::kSuccess);

    adk::Entry* entry;
    BOOST_REQUIRE(queue->WaitEntry(&entry) == ErrorCode::kSuccess);
    char* buffer = entry->buffer;
    BOOST_REQUIRE(*(uint64_t*)buffer == 1UL);
   

    adk::Entry* entry2 = nullptr;
    BOOST_REQUIRE(queue->WaitEntry(&entry2) == ErrorCode::kSuccess);
    buffer = entry2->buffer;
    BOOST_REQUIRE(*(uint64_t*)buffer == 2UL);

    BOOST_REQUIRE(queue->FreeEntry(entry) == ErrorCode::kSuccess);
    BOOST_REQUIRE(queue->FreeEntry(entry2) == ErrorCode::kSuccess);

    adk::Entry* entry3 = nullptr;
    BOOST_REQUIRE(queue->WaitEntry(&entry3) != ErrorCode::kSuccess);
}

