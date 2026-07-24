#include <boost/function.hpp>
#include <boost/thread/thread.hpp>

#include <adk/mem_pool.h>
#include <adk/lock_free_msg_queue.h>

using namespace adk;

void AmiUser(MPSCQueue* gc_channel, MemoryPool* mp, int64_t pos)
{
	int64_t counter = 1;

	while (1)
	{
		MemoryBuffer* mem_buf = mp->NewBuffer();
		if (mem_buf == NULL)
		{
			ADK_PAUSE();
			continue;
		}

		*(int64_t*)(mem_buf->data) = pos;
		*(int64_t*)(mem_buf->data + sizeof(int64_t)) = counter;

		while (gc_channel->Push(mem_buf->shm_ptr) != ErrorCode::kSuccess) ADK_PAUSE();

		++counter;
	}
}

void AmiGC(MPSCQueue* gc_channel, MPManager* mpm)
{
	int64_t counter_array[2] = {1 , 1};

	while (1)
	{
		ShmPointer shm_ptr;
		if (gc_channel->Pop(shm_ptr) != ErrorCode::kSuccess)
		{
			ADK_PAUSE();
			continue;
		}

		MemoryBuffer* mem_buf = mpm->ShmPtrToMemBuf(&shm_ptr);
		assert(mem_buf != NULL);

		int64_t& counter = counter_array[*(int64_t*)(mem_buf->data)];

		if (*(int64_t*)(mem_buf->data + sizeof(int64_t)) != counter)
		{
			abort();
			std::cout << "bug on, counter = " << counter
					  << " buf value = " << *(int64_t*)(mem_buf->data + sizeof(int64_t)) << std::endl;
			sleep(10000);
		}

		++counter;

		if ((counter & (1024*1024*8 - 1)) == 0)
			std::cout << "counter = " << counter << std::endl;

		int32_t ec = mpm->DeleteBuffer(mem_buf);
		assert(ec == ErrorCode::kSuccess);
	}
}

int main(int argc, char const *argv[])
{
	MPSCQueue* gc_channel = MPSCQueue::Create<ShmPointer>("test", 1024);
	MPSCQueue* gc_channel2 = NULL;

	MPManager mpm;
	mpm.CreateMPTable("test_mpm");
	MemoryPool* mp = mpm.CreateSharedPool("test_mp", 1024, 1024);

	boost::thread ami_user_thread1 = boost::thread(boost::bind(AmiUser, gc_channel, mp, 0));

	if (argc > 1)
	{
		// gc_channel2 = MPSCQueue::Duplicate(*gc_channel);
		boost::thread ami_user_thread2 = boost::thread(boost::bind(AmiUser, gc_channel, mp, 1));
	}

	boost::thread ami_gc_thread = boost::thread(boost::bind(AmiGC, gc_channel, &mpm));

	ami_user_thread1.join();

	return 0;
}
