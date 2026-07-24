#include <boost/function.hpp>
#include <boost/thread/thread.hpp>

#include <adk/mem_pool.h>
#include <adk/lock_free_msg_queue.h>

using namespace adk;

int main(int argc, char const *argv[])
{
	MQManager::Destroy("test");
	MQManager& mqm = *MQManager::Create("test", 10, 1024);
	MPSCQueue* mq = mqm.CreateSharedMPSCQueue("test_shm_queue", 1024);
	int64_t counter = 0;
	
	++counter;
	mq->Push(counter);
	++counter;
	mq->Push(counter);
	++counter;
	mq->Push(counter);
	++counter;
	mq->Push(counter);
	++counter;
	mq->Push(counter);
	++counter;
	mq->Push(counter);

	struct Entry* entry;
	mq->WaitEntry(&entry);
	assert(*(int64_t*)(entry->buffer) == 1);

	mq->WaitEntry(&entry);
	assert(*(int64_t*)(entry->buffer) == 2);

	mq->WaitEntry(&entry);
	assert(*(int64_t*)(entry->buffer) == 3);

	mq->WaitEntry(&entry);
	assert(*(int64_t*)(entry->buffer) == 4);

	MPSCQueue* mq2 = mqm.AttachSharedMPSCQueue("test_shm_queue");

	mq2->WaitEntry(&entry);
	assert(*(int64_t*)(entry->buffer) == 1);
	mq2->FreeEntry(entry);

	mq2->WaitEntry(&entry);
	assert(*(int64_t*)(entry->buffer) == 2);
	mq2->FreeEntry(entry);

	mq2->WaitEntry(&entry);
	assert(*(int64_t*)(entry->buffer) == 3);

	mq2->WaitEntry(&entry);
	assert(*(int64_t*)(entry->buffer) == 4);

	MPSCQueue* mq3 = mqm.AttachSharedMPSCQueue("test_shm_queue");
	mq3->WaitEntry(&entry);
	assert(*(int64_t*)(entry->buffer) == 3);

	MPSCQueue* mq4 = mqm.AttachSharedMPSCQueue("test_shm_queue_none");
	assert(mq4 == NULL);

	MQManager::Destroy("test");
	return 0;
}
