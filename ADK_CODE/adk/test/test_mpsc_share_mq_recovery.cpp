#include <adk/mem_pool.h>
#include <adk/lock_free_msg_queue.h>

using namespace adk;

int main(int argc, char const *argv[])
{
    MQManager::Destroy("test_mpsc_recovery");

    MQManager* mqm = MQManager::Create("test_mpsc_recovery", sizeof(ShmPointer), 1024);
    MPSCQueue* mq = mqm->CreateSharedMPSCQueue("total_order", 1024);

    ShmPointer shm_ptr;
    shm_ptr.value = 1234;
    adk::Entry* qentry;
    mq->AllocEntry(&qentry);
    assert(qentry->pos == 1);

    // recovery
    MQManager* mqm_rec = MQManager::Attach("test_mpsc_recovery");
    MPSCQueue* mq_rec = mqm_rec->AttachSharedMPSCQueue("total_order");

    assert(mq_rec->mem_header_ != mq->mem_header_);
    assert(mq_rec->entries_ != mq->entries_);
    assert(mq_rec->Consistent() == ErrorCode::kSuccess);

    ShmPointer shm_ptr_rec;
    int32_t ret = mq_rec->Pop(shm_ptr_rec);
    assert(ret != ErrorCode::kSuccess);

    // reuse
    adk::Entry* qentry_rec;
    mq->AllocEntry(&qentry_rec);
    assert(qentry_rec->pos == 1);
    char* buf = (char*)qentry_rec->buffer;
    ShmPointer& shm_ptr_ref = *((ShmPointer*)buf);
    shm_ptr_ref.value = 1234;
    mq->PostEntry(qentry_rec);

    ret = mq_rec->Pop(shm_ptr_rec);
    assert(ret == ErrorCode::kSuccess);
    assert(shm_ptr_rec.value == 1234);

    return 0;
}
