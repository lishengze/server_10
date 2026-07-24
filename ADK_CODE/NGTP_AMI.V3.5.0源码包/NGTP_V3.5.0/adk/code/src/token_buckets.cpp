#include <adk/token_buckets.h>
#include <adk/entry_wrapper.h>

namespace adk_impl
{

RateControl::RateControl()
{
    tokens_gc_ = variant::ThreadLocalQueue<TokenBucket*>::Create("token bucket", 1024);
    assert(tokens_gc_);

    rate_ctrls_gc_ = variant::ThreadLocalQueue<ConstRateCtrl*>::Create("rate ctrl", 1024);
    assert(rate_ctrls_gc_);

    token_buckets_ = variant::MPSCUnboundedQueue<TokenBucket*>::Create("Buckets");
    assert(token_buckets_);

    const_rate_ctrls_ = variant::MPSCUnboundedQueue<ConstRateCtrl*>::Create("Constants");
    assert(const_rate_ctrls_);

    rhythm_us_ = kMaxRhythmus;
    running_ = true;

    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
    current_time_ = current_time.tv_nsec + current_time.tv_sec * 1000000000;

    Start();
}

RateControl* RateControl::GetRateControl()
{
    static RateControl* rate_control = new RateControl;
    return rate_control;
}

TokenBucket* RateControl::CreateTokenBucket(double rate_limit_us, bool high_performance)
{
    if (ADK_UNLIKELY(0 == rate_limit_us))
    {
        return nullptr;
    }

    void* const token_bucket_mem = memalign(ADK_CACHE_LINE_SIZE, sizeof(TokenBucket));
    if (ADK_UNLIKELY(nullptr == token_bucket_mem))
    {
        return nullptr;
    }

    new (token_bucket_mem) TokenBucket();

    TokenBucket* const token_bucket = reinterpret_cast<TokenBucket*>(token_bucket_mem);

    token_bucket->valid_ = true;
    token_bucket->rate_control_ = this;

    if (adk_impl::IsEnvSetLowUtilization())
    {
        high_performance = false;
    }

    if (high_performance)
    {
        rhythm_us_ = 0;
        token_bucket->capacity_ = rate_limit_us * 100;
    }
    else
    {
        const uint32_t rhythm_us = (uint32_t)(0.5 / rate_limit_us);
        if (rhythm_us < rhythm_us_)
        {
            rhythm_us_ = std::max(rhythm_us, kMinRhythmus);
        }

        token_bucket->capacity_ = ((uint32_t)(rhythm_us_ * rate_limit_us) * 3);
    }

    token_bucket->capacity_ = std::max(token_bucket->capacity_, (uint32_t)3);
    token_bucket->rate_limit_ = rate_limit_us / 1000;
    token_bucket->last_time_ = GetRealTime();
    token_bucket->history_token_ = 0;
    token_bucket->token_num_ = 0;
    token_buckets_->Push(token_bucket);

    return token_bucket;
}

void RateControl::DestroyTokenBucket(TokenBucket* token_bucket)
{
    if (nullptr != token_bucket)
    {
        if (ADK_UNLIKELY(ErrorCode::kSuccess != tokens_gc_->Push(token_bucket)))
        {
            TokenBucket* temp_bucket = nullptr;;
            const auto gc_batch_size = std::max<uint64_t>(tokens_gc_->length() >> 2, 1);
            for (uint64_t index = 0; index < gc_batch_size; ++index)
            {
                ADK_ASSERT_SUCCESS(tokens_gc_->Pop(temp_bucket));
                free(temp_bucket);
            }

            ADK_ASSERT_SUCCESS(tokens_gc_->Push(token_bucket));
        }
    }
}

ConstRateCtrl* RateControl::CreateConstRateCtrl(uint32_t rate_limit, uint64_t timespan)
{
    if (ADK_UNLIKELY(0 == rate_limit))
    {
        return nullptr;
    }

    void* const const_rate_ctrl_mem = memalign(ADK_CACHE_LINE_SIZE, sizeof(ConstRateCtrl));
    if (ADK_UNLIKELY(nullptr == const_rate_ctrl_mem))
    {
        return nullptr;
    }

    new (const_rate_ctrl_mem) ConstRateCtrl();

    ConstRateCtrl* const const_rate_ctrl = reinterpret_cast<ConstRateCtrl*>(const_rate_ctrl_mem);

    const_rate_ctrl->valid_ = true;
    const_rate_ctrl->rate_control_ = this;

    const_rate_ctrl->required_ = 0;
    const_rate_ctrl->release_thrdhold_ = 0;
    const_rate_ctrl->rate_limit_ = rate_limit;
    const_rate_ctrl->rate_window_ = variant::MPSCQueue<ConstRateCtrl::RateNode>::Create("constant instance", rate_limit);
    const_rate_ctrl->released_ = 0;
    const_rate_ctrl->timespan_ = timespan * 1000;

    const uint32_t rhythm_us = (uint32_t)((double)timespan / (double)(3 * rate_limit));
    if (rhythm_us < rhythm_us_)
    {
        rhythm_us_ = std::max(rhythm_us, kMinRhythmus);
    }

    const_rate_ctrls_->Push(const_rate_ctrl);
    return const_rate_ctrl;
}

void RateControl::DestroyConstRateCtrl(ConstRateCtrl* const_rate_ctrl)
{
    if (nullptr != const_rate_ctrl)
    {
        if (ADK_UNLIKELY(ErrorCode::kSuccess != rate_ctrls_gc_->Push(const_rate_ctrl)))
        {
            ConstRateCtrl* temp_rate_ctrl = nullptr;
            const auto gc_batch_size = std::max<uint64_t>(rate_ctrls_gc_->length() >> 2, 1);
            for (uint64_t index = 0; index < gc_batch_size; ++index)
            {
                ADK_ASSERT_SUCCESS(rate_ctrls_gc_->Pop(temp_rate_ctrl));
                if (nullptr != temp_rate_ctrl->rate_window_)
                {
                    variant::MPSCQueue<ConstRateCtrl::RateNode>::Delete(temp_rate_ctrl->rate_window_);
                }
                free(temp_rate_ctrl);
            }

            ADK_ASSERT_SUCCESS(rate_ctrls_gc_->Push(const_rate_ctrl));
        }
    }
}

void RateControl::WorkerThread()
{
    TokenBucket* token_bucket = nullptr;
    ConstRateCtrl* const_rate_ctrl = nullptr;
    while (ACCESS_ONCE(running_))
    {
        const uint64_t start_time = GetRealTime();
        const uint32_t tbs_length = token_buckets_->length();
        for (uint32_t index = 0; index < tbs_length; ++index)
        {
            if (ADK_UNLIKELY(ErrorCode::kSuccess != token_buckets_->Pop(token_bucket)))
            {
                break;
            }

            if (token_bucket->valid_)
            {
                token_bucket->DoPeriodFeed(GetRealTime());
                token_buckets_->Push(token_bucket);
            }
            else
            {
                DestroyTokenBucket(token_bucket);
            }
        }

        const uint32_t rcs_length = const_rate_ctrls_->length();
        for (uint32_t index = 0; index < rcs_length; ++index)
        {
            if (ADK_UNLIKELY(ErrorCode::kSuccess != const_rate_ctrls_->Pop(const_rate_ctrl)))
            {
                break;
            }

            if (const_rate_ctrl->valid_)
            {
                const_rate_ctrl->DoPeriodFeed(GetRealTime());
                const_rate_ctrls_->Push(const_rate_ctrl);
            }
            else
            {
                DestroyConstRateCtrl(const_rate_ctrl);
            }
        }

        const uint64_t diff_time = (GetRealTime() - start_time) / 1000;
        if (rhythm_us_ > diff_time)
        {
            usleep(rhythm_us_ - diff_time);
        }
        else
        {
            ADK_PAUSE();
        }
    }
}

void RateControl::Start()
{
    running_ = true;
    worker_thread_ = std_thread("adk-ratectrl", "rate control worker thread", std::bind(&RateControl::WorkerThread, this));
}

void RateControl::Stop()
{
    running_ = false;
    if (worker_thread_.joinable())
    {
        worker_thread_.join();
    }
}

inline void TokenBucket::DoPeriodFeed(uint64_t current_time)
{
    const uint64_t total_adden = (current_time - last_time_) * rate_limit_;

    if (total_adden > history_token_)
    {
        const uint64_t adden = total_adden - history_token_;

        const uint32_t token_num = ACCESS_ONCE(token_num_);
        if (ADK_UNLIKELY(token_num + adden > capacity_))
        {
            if (token_num == __sync_fetch_and_add(&token_num_, capacity_ - token_num))
            {
                history_token_ = 0;
                last_time_ = current_time;
            }
            else
            {
                history_token_ = total_adden;
            }
        }
        else
        {
            __sync_fetch_and_add(&token_num_, adden);
            history_token_ = total_adden;
        }
    }
}

inline void ConstRateCtrl::DoPeriodFeed(uint64_t real_time)
{
    struct variant::VariantEntry* entry_ptr = nullptr;
    while (ErrorCode::kSuccess == rate_window_->WaitEntry(&entry_ptr))
    {
        char* const buffer = entry_ptr->buffer;
        if (((RateNode*)buffer)->timepoint + timespan_ < real_time)
        {
            const auto require = ((RateNode*)buffer)->require;
            rate_window_->FreeEntry(entry_ptr);
            ADK_BARRIER();
            released_ += require;
        }
        else
        {
            break;
        }
    }
}

}