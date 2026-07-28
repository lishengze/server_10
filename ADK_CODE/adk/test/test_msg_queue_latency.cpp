#include <thread>

#include <time.h>
#include <stdlib.h>
#include <adk/lock_free_queue_variant.h>

#if 1
#define NS adk::variant
#define Entry adk::variant::VariantEntry
#define QueueDuplicate(x) x->Duplicate()
#else
#define NS adk
#define Entry adk::Entry
#define QueueDuplicate(x) adk::SPSCQueue<struct timespec>::Duplicate(*x)
#endif

int main(int argc, char* argv[])
{
	uint32_t queue_size = 8192;
	if (argc > 1)
	{
		queue_size = atoi(argv[1]);
	}
	
	auto* queue = NS::SPSCQueue<struct timespec>::Create("", queue_size);
	auto producer = std::thread([&]() {
		auto* queue_p = QueueDuplicate(queue);
		Entry* entry_ptr = nullptr;

		do
		{
			if (adk::ErrorCode::kSuccess == queue_p->AllocEntry(&entry_ptr))
			{
				clock_gettime(CLOCK_REALTIME, (struct timespec*)(entry_ptr->buffer));
				queue_p->PostEntry(entry_ptr);
				usleep(0);
			}
		} while (true);
	});

	volatile uint64_t counter = 0;
	volatile uint64_t total_nanosecond = 0;
	volatile uint64_t min_nanosecond = 0xffffffff;

	auto ob = std::thread([&]() {
		uint64_t ob_counter = 0;
		uint64_t ob_total_nanosecond = 0;
		do
		{
			sleep(1);
			const auto counter_l = counter;
			const auto total_nanosecond_l = total_nanosecond;
			const auto min_nanosecond_l = min_nanosecond;
			
			if (counter_l > ob_counter)
			{
				std::cout << "nr = " << counter_l - ob_counter 
				          << ". min = " << min_nanosecond_l
				          << ", avg = " << (total_nanosecond_l - ob_total_nanosecond) / (counter_l - ob_counter) << std::endl;
			}
			else
			{
				std::cout << "nr = 0" << std::endl;
			}
			
			ob_counter = counter_l;
			ob_total_nanosecond = total_nanosecond;
		} while (true);
	});
	
	struct timespec clock_now;
	Entry* entry_ptr = nullptr;
	do
	{
		if (adk::ErrorCode::kSuccess == queue->WaitEntry(&entry_ptr))
		{
			clock_gettime(CLOCK_REALTIME, &clock_now);

			++counter;
			char* const buffer = entry_ptr->buffer;
			const auto time_diff = (clock_now.tv_sec - ((struct timespec*)(buffer))->tv_sec) * 1000000000
			                     + clock_now.tv_nsec - ((struct timespec*)(buffer))->tv_nsec;
			total_nanosecond += time_diff;
			min_nanosecond = time_diff < min_nanosecond ? time_diff : min_nanosecond;
			queue->FreeEntry(entry_ptr);
		}
	} while (true);
	return true;
}