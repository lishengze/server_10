#include <boost/function.hpp>
#include <boost/thread/thread.hpp>

#include <adk/mem_pool.h>
#include <adk/lock_free_msg_queue.h>

using namespace adk;

int main(int argc, char const *argv[])
{
	// MPSCQueue* gc_channel = MPSCQueue::Create<ShmPointer>("test", 1024*1024);
	// MPSCQueue* gc_channel2 = NULL;
	remove("/dev/shm/test_mpm");
    remove("/dev/shm/test_mp");

	MPManager mpm;
	mpm.CreateMPTable("test_mpm");
	MemoryPool* mp = mpm.CreateSharedPool("test_mp", 512, 1024, 1024);

	assert(mp->pool_index_ == 0);
	assert(mp->pool_header_ != NULL);
	assert(mp->pool_header_->mp_block_size == 512);
	assert(mp->pool_header_->mp_block_num == 1024);
	assert(mp->pool_header_->mp_emergent_block_num == 1024);
	
	assert(mp->buffer_queue_.mem_header_ != NULL);
	assert((char*)(mp->buffer_queue_.entries_) > (char*)(mp->pool_header_) + sizeof(MemoryPoolHeader));
	// assert(mp->buffer_queue_.entry_size_ == ADK_CACHE_LINE_SIZE);
	// assert(mp->buffer_queue_.entry_bits_ == 6);
	assert(mp->buffer_queue_.entry_size_ == 16);
	assert(mp->buffer_queue_.entry_bits_ == 4);
	assert(mp->buffer_queue_.queue_mask_ == 1023);
	assert(mp->buffer_queue_.queue_size_ == 1024);
	assert(mp->buffer_queue_.release_alert_ == false);
	// assert(mp->buffer_queue_.mq_index_ == -1);	// meaningless
	assert(mp->buffer_queue_.reserve_threshold_ == 1024);
	assert(mp->buffer_queue_.head_threshold_ == 0);
	assert(mp->buffer_queue_.mem_header_->reserve == 1024);
	assert(mp->buffer_queue_.mem_header_->tail == 1024);
	assert(mp->buffer_queue_.mem_header_->head == 0);
	assert(mp->buffer_queue_.mem_header_->release == 0);

	assert(mp->emergent_buffer_queue_.mem_header_ != NULL);
	assert((char*)(mp->emergent_buffer_queue_.entries_) == ((char*)(mp->buffer_queue_.entries_) + mp->buffer_queue_.entry_size_ * mp->buffer_queue_.queue_size_));
	// assert(mp->emergent_buffer_queue_.entry_size_ == ADK_CACHE_LINE_SIZE);
	// assert(mp->emergent_buffer_queue_.entry_bits_ == 6);
	assert(mp->emergent_buffer_queue_.entry_size_ == 16);
	assert(mp->emergent_buffer_queue_.entry_bits_ == 4);
	assert(mp->emergent_buffer_queue_.queue_mask_ == 1023);
	assert(mp->emergent_buffer_queue_.queue_size_ == 1024);
	assert(mp->emergent_buffer_queue_.release_alert_ == false);
	assert(mp->emergent_buffer_queue_.reserve_threshold_ == 1024);
	assert(mp->emergent_buffer_queue_.head_threshold_ == 0);
	assert(mp->emergent_buffer_queue_.mem_header_->reserve == 1024);
	assert(mp->emergent_buffer_queue_.mem_header_->tail == 1024);
	assert(mp->emergent_buffer_queue_.mem_header_->head == 0);
	assert(mp->emergent_buffer_queue_.mem_header_->release == 0);


	assert((char*)(mp->pool_header_) + mp->pool_header_->mp_block_offset >= (char*)(mp->emergent_buffer_queue_.entries_) + mp->emergent_buffer_queue_.entry_size_ * mp->emergent_buffer_queue_.queue_size_);
	assert((char*)(mp->pool_header_->blocks()) == (char*)(mp->pool_header_) + mp->pool_header_->mp_block_offset);

	MemoryBuffer* first_mem_buf = (MemoryBuffer*)(mp->pool_header_->blocks());
	MemoryBuffer* last_mem_buf = (MemoryBuffer*)(mp->pool_header_->blocks() + mp->pool_header_->mp_block_size * (mp->pool_header_->mp_block_num + mp->pool_header_->mp_emergent_block_num - 1));
	assert(first_mem_buf->shm_ptr.value == mp->pool_header_->mp_block_offset);
	assert(last_mem_buf->shm_ptr.value == mp->pool_header_->mp_block_offset + mp->pool_header_->mp_block_size * (mp->pool_header_->mp_block_num + mp->pool_header_->mp_emergent_block_num - 1));

	assert(mp->pool_index_ == 0);

	MemoryBuffer* temp_buf;

	int32_t counter = 1024;
	MemoryBuffer* mem_buf;
	while (counter > 0)
	{
		mem_buf = mp->NewBuffer();
		assert(mem_buf != NULL);
		assert(!IS_EMERGENT_BUFFER(mem_buf));
		--counter;
	}

	temp_buf = mp->NewBuffer();
	assert(temp_buf == NULL);

	counter = 1024;
	MemoryBuffer* em_mem_buf;
	while (counter > 0)
	{
		em_mem_buf = mp->NewEmergentBuffer();
		assert(em_mem_buf != NULL);
		assert(IS_EMERGENT_BUFFER(em_mem_buf));
		--counter;
	}

	// temp_buf = mp->NewEmergentBuffer();		deadlock!
	// assert(temp_buf == NULL);

	int32_t ec = mp->DeleteBuffer(em_mem_buf);
	assert(ec == ErrorCode::kSuccess);
	ec = mp->DeleteBuffer(mem_buf);
	assert(ec == ErrorCode::kSuccess);

	temp_buf = mp->NewBuffer();
	assert(temp_buf != NULL);
	assert(temp_buf == mem_buf);

	temp_buf = mp->NewBuffer();
	assert(temp_buf == NULL);

	temp_buf = mp->NewEmergentBuffer();
	assert(temp_buf != NULL);
	assert(temp_buf == em_mem_buf);

	return 0;
}
