#include <adk/rate_control.h>
#include <adk/util.h>

namespace adk_impl
{
#define AMI_RC_BURST_FACTOR 4

RateController* RateController::Create(const ssize_t rate_per_sec,
                                       const size_t max_apdu)
{
	if (rate_per_sec < 0 
		|| max_apdu == 0
		|| (rate_per_sec < (ssize_t)max_apdu && rate_per_sec != 0))
		return nullptr;

 	RateController* rc = new RateController();
	rc->rate_per_sec_ = rate_per_sec;
	clock_gettime(CLOCK_MONOTONIC_RAW, &rc->last_rate_check_);

	rc->rate_burst_ = max_apdu * AMI_RC_BURST_FACTOR;
	if ((rc->rate_per_sec_ / 1000) >= (ssize_t)max_apdu)
	{
		rc->rate_per_msec_ = rc->rate_per_sec_ / 1000;
		if (rc->rate_burst_ > rc->rate_per_msec_)
			rc->rate_burst_ = rc->rate_per_msec_;
	}
	else
	{
		if (rc->rate_burst_ > rc->rate_per_sec_)
			rc->rate_burst_ = rc->rate_per_sec_;
	}
	return rc;
}

void RateController::Destroy(RateController* rc)
{
	delete rc;
}

bool RateController::Wait(const size_t msgs_or_bytes, bool is_block)
{
	assert(msgs_or_bytes > 0);
	if (0 == rate_per_sec_)
	{
		return true;
	}

	if (rate_limit_ >= (ssize_t)msgs_or_bytes)
	{
		rate_limit_ -= msgs_or_bytes;
		return true;
	}

	int64_t new_rate_limit;
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC_RAW, &now);

	if (rate_per_msec_)
	{
		const int64_t time_since_last_rate_check = time_diff(now, last_rate_check_) / 1024;
		if (time_since_last_rate_check >= 1024) 	// 1ms
		{
			new_rate_limit = rate_burst_;
		}
		else 
		{
			new_rate_limit = rate_limit_ + ((rate_per_msec_ * time_since_last_rate_check) / 1024);
			if (new_rate_limit > rate_burst_)
				new_rate_limit = rate_burst_;
		}
	}
	else
	{
		const int64_t time_since_last_rate_check = time_diff(now, last_rate_check_) / 1024;
		if (time_since_last_rate_check >= 1024*1024)
		{
			// new_rate_limit = rate_per_sec_;
			new_rate_limit = rate_burst_;
		}
		else 
		{
			new_rate_limit = rate_limit_ + ((rate_per_sec_ * time_since_last_rate_check) / (1024*1024));
			if (new_rate_limit > rate_burst_)
				new_rate_limit = rate_burst_;
		}
	}

	new_rate_limit -= msgs_or_bytes;
	if (new_rate_limit < 0)
	{
		if (!is_block)
			return false;

		rate_limit_ = new_rate_limit;
		last_rate_check_ = now;
		if (rate_limit_ < 0) 
		{
			ssize_t sleep_amount;
			do {
				if (is_release_alert_)
				{
					is_release_alert_ = false;
					rate_limit_ = 0;
					return false;
				}

				sched_yield();

				clock_gettime(CLOCK_MONOTONIC_RAW, &now);

				sleep_amount = (rate_per_sec_ * (time_diff(now, last_rate_check_) / 1024))
							   / (1024*1024);
			} while (sleep_amount + rate_limit_ < 0);
			rate_limit_ += sleep_amount;
			last_rate_check_ = now;
		}
	}
	else
	{
		rate_limit_ = new_rate_limit;
		last_rate_check_ = now;
	}

	return true;
}

void RateController::ReleaseWaitThread()
{
    is_release_alert_ = true;
}

} // adk
