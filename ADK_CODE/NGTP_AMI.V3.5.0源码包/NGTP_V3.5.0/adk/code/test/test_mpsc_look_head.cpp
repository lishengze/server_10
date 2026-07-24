#include <adk/lock_free_msg_queue.h>
#include <adk/error_code.h>
#include <adk/arch/generic.h>

#include <boost/thread/thread.hpp>

using namespace adk;

int main(int argc, char const *argv[])
{
    MPSCQueue* mq_p = MPSCQueue::Create("test_spsc", sizeof(uint64_t), 1024);
    MPSCQueue* mq_c = MPSCQueue::Duplicate(*mq_p);

    assert(mq_c->Head<uint64_t>() == NULL);

    uint64_t counter = 1024;
    auto ec = mq_p->Push(counter);
    assert(ec == adk::ErrorCode::kSuccess);

    assert(*(mq_c->Head<uint64_t>()) == 1024);

    counter = 1023;
    ec = mq_p->Push(counter);
    assert(ec == adk::ErrorCode::kSuccess);

    assert(*(mq_c->Head<uint64_t>()) == 1024);
    
    mq_c->Pop();
    assert(*(mq_c->Head<uint64_t>()) == 1023);
    assert(*(mq_c->Head<uint64_t>()) == 1023);

    mq_c->Pop();
    assert(mq_c->Head<uint64_t>() == NULL);

    counter = 0;
    while ((++counter) <= 1024)
    {
        ec = mq_p->Push(counter);
        assert(ec == ErrorCode::kSuccess);
    }

    ec = mq_p->Push(counter);
    assert(ec != ErrorCode::kSuccess);

    counter = 0;
    while ((++counter) <= 1024)
        assert(*(mq_c->Head<uint64_t>()) == 1);

    mq_c->Pop<uint64_t>();

    counter = 0;
    while ((++counter) <= 1024)
        assert(*(mq_c->Head<uint64_t>()) == 2);
    
    mq_c->Pop<uint64_t>();

    counter = 2;
    while ((++counter) <= 1024)
    {
        assert(*(mq_c->Head<uint64_t>()) == counter);
        mq_c->Pop();
    }

    return 0;
}
