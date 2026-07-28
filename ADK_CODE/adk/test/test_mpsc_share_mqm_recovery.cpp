#include <adk/mem_pool.h>
#include <adk/lock_free_msg_queue.h>

using namespace adk;

int main(int argc, char const *argv[])
{
    MQManager::Destroy("test_mpsc_recovery");

    MQManager* mqm = MQManager::Create("test_mpsc_recovery", sizeof(ShmPointer), 1024);
    MPSCQueue* mq1 = mqm->CreateSharedMPSCQueue("total_order1", 1024, 0);
    MPSCQueue* mq2 = mqm->CreateSharedMPSCQueue("total_order2", 1024, 0);
    // simulate failure
    MPSCQueue* mq3 = mqm->CreateSharedMPSCQueue("total_order3", 1024, 1);
    assert(mq3 == nullptr);


    // recovery
    MQManager* mqm_rec = MQManager::Attach("test_mpsc_recovery");
    MPSCQueue* mq_rec1 = mqm_rec->AttachSharedMPSCQueue("total_order1");
    assert(mq_rec1 != nullptr);
    MPSCQueue* mq_rec2 = mqm_rec->AttachSharedMPSCQueue("total_order2");
    assert(mq_rec2 != nullptr);
    MPSCQueue* mq_rec3 = mqm_rec->AttachSharedMPSCQueue("total_order3");
    assert(mq_rec3 == nullptr);
    mq_rec3 = mqm_rec->CreateSharedMPSCQueue("total_order3", 1024, 0);
    assert(mq_rec3 != nullptr);

    MPSCQueue* mq_rec4 = mqm_rec->CreateSharedMPSCQueue("total_order4", 1024, 2);
    assert(mq_rec4 == nullptr);

    // second recovery
    MQManager* mqm_rec_2 = MQManager::Attach("test_mpsc_recovery");
    MPSCQueue* mqm_rec_2_1 = mqm_rec_2->AttachSharedMPSCQueue("total_order1");
    assert(mqm_rec_2_1 != nullptr);
    MPSCQueue* mqm_rec_2_2 = mqm_rec_2->AttachSharedMPSCQueue("total_order2");
    assert(mqm_rec_2_2 != nullptr);
    MPSCQueue* mqm_rec_2_3 = mqm_rec_2->AttachSharedMPSCQueue("total_order3");
    assert(mqm_rec_2_3 != nullptr);
    MPSCQueue* mqm_rec_2_4 = mqm_rec_2->AttachSharedMPSCQueue("total_order4");
    assert(mqm_rec_2_4 != nullptr);

    MQManager::Destroy("test_mpsc_recovery");
    return 0;
}
