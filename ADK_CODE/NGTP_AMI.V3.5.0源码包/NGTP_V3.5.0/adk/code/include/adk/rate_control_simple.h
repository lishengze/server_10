#ifndef ADK_IMPL_CONST_RATE_CTRL_SIMPLE_H_
#define ADK_IMPL_CONST_RATE_CTRL_SIMPLE_H_

#include <vector>
#include <boost/thread.hpp>
#include <boost/chrono/chrono.hpp>

namespace adk_impl
{

using spinlock = boost::detail::spinlock;
using time_point = boost::chrono::high_resolution_clock::time_point;

class ConstRateCtrlSimple
{
public:
    static ConstRateCtrlSimple* NewInstance(uint32_t rate_limit)
    {
        return new ConstRateCtrlSimple(rate_limit);
    }

    static void DestroyInstance(ConstRateCtrlSimple* rate_control)
    {
        delete rate_control;
    }

    bool TryAcquire()
    {
        if (tail_ < head_ + rate_limit_)
        {
            boost::lock_guard<spinlock> _(tokens_lock_);
            if (tail_ < head_ + rate_limit_)
            {
                rate_tokens_vec_[((tail_++) % rate_limit_)] = boost::chrono::high_resolution_clock::now();
                return true;
            }
        }

        bool acquire_res = false;
        const time_point now = boost::chrono::high_resolution_clock::now();
        boost::lock_guard<spinlock> _(tokens_lock_);
        for (; head_ < tail_; ++head_)
        {
            const time_point& acquire_time = rate_tokens_vec_[head_ % rate_limit_];
            if ((now - acquire_time).count() < 1000000000)
            {
                break;
            }
            acquire_res = true;
        }

        if (acquire_res)
        {
            rate_tokens_vec_[((tail_++) % rate_limit_)] = boost::chrono::high_resolution_clock::now();
        }
        return acquire_res;
    }

private:
    ConstRateCtrlSimple(uint32_t rate_limit)
    {
        head_ = 0;
        tail_ = 0;
        rate_limit_ = rate_limit;
        tokens_lock_ = BOOST_DETAIL_SPINLOCK_INIT;
        rate_tokens_vec_.resize(rate_limit);
    }

    volatile uint64_t head_;
    volatile uint64_t tail_;
    uint32_t rate_limit_;
    spinlock tokens_lock_;
    std::vector<time_point> rate_tokens_vec_;
};

}

#endif // ADK_CONST_RATE_CTRL_SIMPLE_