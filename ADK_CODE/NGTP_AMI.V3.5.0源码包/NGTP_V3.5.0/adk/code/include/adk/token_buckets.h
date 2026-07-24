#ifndef ADK_IMPL_TOKEN_BUCKET_H_
#define ADK_IMPL_TOKEN_BUCKET_H_

#include "error_code.h"
#include "arch/generic.h"
#include "lock_free_unbounded_queue_variant.h"

#include <mutex>
#include <thread>
#include <algorithm>

namespace adk_impl
{

constexpr uint32_t kMinRhythmus = 100;
constexpr uint32_t kMaxRhythmus = 1000000;

namespace rate_unit
{
    struct Second {};
    struct Millisecond {};
    struct Microsecond {};
}

class RateControl;

class RateCtrlBase
{
public:
    uint64_t GetCurrentTime() const;

    uint64_t GetRealTime();

    inline void Release()
    {
        valid_ = false;
    }

protected:
    RateCtrlBase()
    {
        valid_ = true;
    }

    volatile bool valid_;
    RateControl* rate_control_;
};

class TokenBucket : public RateCtrlBase
{
public:
    int32_t Acquire(uint32_t require)
    {
        while (ErrorCode::kSuccess != TryAcquire(require))
        {
            if (ACCESS_ONCE(valid_))
            {
                std::this_thread::yield();
            }
            else
            {
                return ErrorCode::kWouldblock;
            }
        }

        return ErrorCode::kSuccess;
    }

    template<typename Policy>
    int32_t Acquire(uint32_t require, const Policy& delay_policy)
    {
        while (ErrorCode::kSuccess != TryAcquire(require))
        {
            if (ACCESS_ONCE(valid_))
            {
                delay_policy();
            }
            else
            {
                return ErrorCode::kWouldblock;
            }
        }

        return ErrorCode::kSuccess;
    }

    int32_t TryAcquire(uint32_t require)
    {
        uint32_t token_num;
        do
        {
            token_num = ACCESS_ONCE(token_num_);
            if (ADK_UNLIKELY(token_num < require))
            {
                return ErrorCode::kFailure;
            }
        } while (!__sync_bool_compare_and_swap(&token_num_, token_num, token_num - require));
        return ErrorCode::kSuccess;
    }

    void SetCapacity(uint32_t capacity)
    {
        capacity_ = capacity;
    }

private:
    TokenBucket() = default;

    void DoPeriodFeed(uint64_t current_time);

    uint32_t capacity_;
    double   rate_limit_;
    uint64_t last_time_;
    uint64_t history_token_;
    uint32_t token_num_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));

    friend class RateControl;
};

class ConstRateCtrl : public RateCtrlBase
{
public:
    int32_t Acquire(uint32_t require)
    {
        while (ErrorCode::kSuccess != TryAcquire(require))
        {
            if (ACCESS_ONCE(valid_))
            {
                std::this_thread::yield();
            }
            else
            {
                return ErrorCode::kWouldblock;
            }
        }
        return ErrorCode::kSuccess;
    }

    template<typename Policy>
    int32_t Acquire(uint32_t require, const Policy& delay_policy)
    {
        while (ErrorCode::kSuccess != TryAcquire(require))
        {
            if (ACCESS_ONCE(valid_))
            {
                delay_policy();
            }
            else
            {
                return ErrorCode::kWouldblock;
            }
        }
        return ErrorCode::kSuccess;
    }

    int32_t TryAcquire(uint32_t require)
    {
        uint64_t monotonic_require;
        uint64_t new_required;
        do
        {
            monotonic_require = ACCESS_ONCE(required_);
            new_required = monotonic_require + require;
            if (new_required > ACCESS_ONCE(release_thrdhold_) + rate_limit_)
            {
                release_thrdhold_ = ACCESS_ONCE(released_);
                if (new_required > release_thrdhold_ + rate_limit_)
                {
                    return ErrorCode::kFailure;
                }
            }
        } while (!__sync_bool_compare_and_swap(&required_, monotonic_require, new_required));

        variant::VariantEntry* entry_ptr = nullptr;
        __attribute__((unused)) const auto ec = rate_window_->AllocEntry(&entry_ptr);
        assert(ErrorCode::kSuccess == ec);

        char* const buffer = entry_ptr->buffer;
        ((RateNode*)buffer)->timepoint = GetRealTime();
        ((RateNode*)buffer)->require = require;

        rate_window_->PostEntry(entry_ptr);
        return ErrorCode::kSuccess;
    }

private:
    ConstRateCtrl() = default;

    struct RateNode
    {
        uint64_t timepoint;
        uint32_t require;
    };

    void DoPeriodFeed(uint64_t real_time);

    uint64_t required_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint64_t release_thrdhold_;
    uint32_t rate_limit_;
    variant::MPSCQueue<RateNode>* rate_window_;

    uint64_t released_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint64_t timespan_;

    friend class RateControl;
};

class RateControl
{
public:
    template<typename Precision = rate_unit::Microsecond>
    static TokenBucket* GetInstance(double rate_limit, bool high_performance = false)
    {
        auto* const rate_control = RateControl::GetRateControl();
        assert(rate_control);

        return rate_control->CreateTokenBucket(RateLimitConvert<Precision>(rate_limit), high_performance);
    }

    template<typename Precision = rate_unit::Second>
    static ConstRateCtrl* GetInstance(uint32_t rate_limit, uint32_t timespan)
    {
        auto* const rate_control = RateControl::GetRateControl();
        assert(rate_control);

        return rate_control->CreateConstRateCtrl(rate_limit, ToMicrosecond<Precision>(timespan));
    }

    inline uint64_t GetCurrentTime()
    {
        return ACCESS_ONCE(current_time_) / 1000;
    }

    inline uint64_t GetRealTime()
    {
        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
        const uint64_t time_ns = current_time.tv_nsec + current_time.tv_sec * 1000000000;
        current_time_ = std::max(current_time_, time_ns);
        return current_time_;
    }

    void Stop();

protected:
    RateControl();

    static RateControl* GetRateControl();

    TokenBucket* CreateTokenBucket(double rate_limit_us, bool high_performance);

    void DestroyTokenBucket(TokenBucket* token_bucket);

    ConstRateCtrl* CreateConstRateCtrl(uint32_t rate_limit, uint64_t timespan);

    void DestroyConstRateCtrl(ConstRateCtrl* const_rate_ctrl);

    void WorkerThread();

    void Start();

    template<typename Precision>
    static inline double RateLimitConvert(double rate_limit);

    template<typename Precision>
    static inline uint64_t ToMicrosecond(uint32_t timespan);

private:
    bool        running_;
    uint32_t    rhythm_us_;
    uint64_t    current_time_;
    std::thread worker_thread_;
    variant::ThreadLocalQueue<TokenBucket*>*   tokens_gc_;
    variant::ThreadLocalQueue<ConstRateCtrl*>* rate_ctrls_gc_;

    variant::MPSCUnboundedQueue<TokenBucket*>* token_buckets_;
    variant::MPSCUnboundedQueue<ConstRateCtrl*>* const_rate_ctrls_;
};

template<>
inline double RateControl::RateLimitConvert<rate_unit::Second>(double rate_limit)
{
    return rate_limit / 1000000;
}

template<>
inline double RateControl::RateLimitConvert<rate_unit::Millisecond>(double rate_limit)
{
    return rate_limit / 1000;
}

template<>
inline double RateControl::RateLimitConvert<rate_unit::Microsecond>(double rate_limit)
{
    return rate_limit;
}

template<>
inline uint64_t RateControl::ToMicrosecond<rate_unit::Second>(uint32_t timespan)
{
    return timespan * 1000000;
}

template<>
inline uint64_t RateControl::ToMicrosecond<rate_unit::Millisecond>(uint32_t timespan)
{
    return timespan * 1000;
}

template<>
inline uint64_t RateControl::ToMicrosecond<rate_unit::Microsecond>(uint32_t timespan)
{
    return timespan;
}

inline uint64_t RateCtrlBase::GetCurrentTime() const
{
    return rate_control_->GetCurrentTime();
}

inline uint64_t RateCtrlBase::GetRealTime()
{
    return rate_control_->GetRealTime();
}

}
#endif