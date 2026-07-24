#include <adk/lock_free_msg_queue.h>
#include <adk/error_code.h>
#include <adk/arch/generic.h>

#include <boost/thread/thread.hpp>

using namespace adk;

int main(int argc, char const *argv[])
{
    SPSCQueue<uint64_t>* mq_p = SPSCQueue<uint64_t>::Create("test_spsc", 1024);
    SPSCQueue<uint64_t>* mq_c = SPSCQueue<uint64_t>::Duplicate(*mq_p);

    assert(mq_c->Head() == NULL);

    uint64_t counter = 1024;
    auto ec = mq_p->Push(counter);
    assert(ec == adk::ErrorCode::kSuccess);

    assert(*(mq_c->Head()) == 1024);

    counter = 1023;
    ec = mq_p->Push(counter);
    assert(ec == adk::ErrorCode::kSuccess);

    assert(*(mq_c->Head()) == 1024);
    
    mq_c->Pop();
    assert(*(mq_c->Head()) == 1023);
    assert(*(mq_c->Head()) == 1023);

    mq_c->Pop();
    assert(mq_c->Head() == NULL);

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
        assert(*(mq_c->Head()) == 1);

    mq_c->Pop<uint64_t>();

    counter = 0;
    while ((++counter) <= 1024)
        assert(*(mq_c->Head()) == 2);
    
    mq_c->Pop<uint64_t>();

    counter = 2;
    while ((++counter) <= 1024)
    {
        assert(*(mq_c->Head()) == counter);
        mq_c->Pop();
    }

    return 0;
}
