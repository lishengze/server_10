#include <adk/leaky_bucket.h>


namespace adk_impl
{

LeakyBucket::LeakyBucket(uint32_t leak_interval_micro, uint32_t leak_length)
{
	leak_interval_micro_ = leak_interval_micro;
	leak_length_ = leak_length;
	next_leak_timepoint_ = Clock::now();
}

uint32_t LeakyBucket::try_leak(uint32_t len)
{
	if (len == 0 || Clock::now() < next_leak_timepoint_ )
	{
		return 0;
	}

	if (len < leak_length_)
	{
		uint32_t interval_micro = (len * leak_interval_micro_) / leak_length_;
		next_leak_timepoint_ = Clock::now() + std::chrono::microseconds(interval_micro);
		return len;
	}
	else
	{
		next_leak_timepoint_ = Clock::now() + std::chrono::microseconds(leak_interval_micro_);
		return leak_length_;
	}
}

uint32_t LeakyBucket::leak(uint32_t len)
{
	if (len == 0)
	{
		return 0;
	}

	while (Clock::now() < next_leak_timepoint_ )
	{
		ADK_PAUSE();
	}

	if (len < leak_length_)
	{
		uint32_t interval_micro = (len * leak_interval_micro_) / leak_length_;
		next_leak_timepoint_ = Clock::now() + std::chrono::microseconds(interval_micro);
		return len;
	}
	else
	{
		next_leak_timepoint_ = Clock::now() + std::chrono::microseconds(leak_interval_micro_);
		return leak_length_;
	}
}

}	//namespace