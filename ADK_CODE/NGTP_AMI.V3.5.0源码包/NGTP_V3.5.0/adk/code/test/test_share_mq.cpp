#include <adk/mem_pool.h>
#include <adk/lock_free_msg_queue.h>

using namespace adk;

int main(int argc, char const *argv[])
{
    MQManager::Destroy("test_mqm");

    MQManager* mqm = MQManager::Create("test_mqm", sizeof(ShmPointer), 1024);
    MPSCQueue* mq = mqm->CreateSharedMPSCQueue("total_order", 1024);

    MQManager* mqm_rec = MQManager::Attach("test_mqm");
    MPSCQueue* mq_rec = mqm_rec->AttachSharedMPSCQueue("total_order");

    assert(mq_rec->mem_header_ != mq->mem_header_);
    assert(mq_rec->entries_ != mq->entries_);

    ShmPointer shm_ptr;
    shm_ptr.value = 1234;
    mq->Push(shm_ptr);

    ShmPointer shm_ptr_rec;
    mq_rec->Pop(shm_ptr_rec);
    assert(shm_ptr_rec.value == 1234);

    MQManager::Destroy("test_mqm");
    return 0;
}