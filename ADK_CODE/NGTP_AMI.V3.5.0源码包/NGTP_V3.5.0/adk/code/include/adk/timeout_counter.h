/**
 * @file
 * @brief      timeout counter implementation
 * @author     zhaonan, zhaonan@archforce.com.cn
 * @date       2017-03-01
 */ 
#ifndef ADK_IMPL_TIMEOUT_COUNTER_H_
#define ADK_IMPL_TIMEOUT_COUNTER_H_

#ifdef __GNUC__
#include <time.h>
#include <unistd.h>
#endif
#include <adk/util.h>

namespace adk_impl
{

#define ADK_TOC_POLLING         -1

/**
 * @brief      timeout counter is aim to simplify timeout counting.
 */
class TimeoutCounter
{
public:
    /**
     * @brief      构造函数
     *
     * @param[in]  sleep_interval  counter未超时delay的时长，单位为us
     * @param[in]  timeout_ns      counter的超时时间，单位为ns，该重载接口中的timeout_ns为外部变量
     */
    TimeoutCounter(int32_t sleep_interval, uint64_t* timeout_ns)
        :   sleep_inteval_(sleep_interval),
            is_reset_(true),
            initial_timeout_ns_(*timeout_ns),
            timeout_ns_(timeout_ns)
    {}

    /**
     * @brief      构造函数
     *
     * @param[in]  sleep_interval  counter未超时delay的时长，单位为us
     * @param[in]  timeout_ns      counter的超时时间，单位为ns
     */
    TimeoutCounter(int32_t sleep_interval, uint64_t timeout_ns)
        :   sleep_inteval_(sleep_interval),
            is_reset_(true),
            initial_timeout_ns_(timeout_ns),
            var_timeout_ns_(timeout_ns),
            timeout_ns_(&var_timeout_ns_)
    {}

    /**
     * @brief      运行timeout counter，该接口会delay sleep_inteval_微妙
     */
    inline void Run()
    {
        if (is_reset_)
        {
            is_reset_ = false;
            clock_gettime(CLOCK_MONOTONIC_RAW, &ts_begin_);
        }

        if (sleep_inteval_ != ADK_TOC_POLLING)
            usleep(sleep_inteval_);
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts_end_);
        const uint64_t time_use = time_diff(ts_end_, ts_begin_);

        if (time_use > *timeout_ns_)
        {
            *timeout_ns_ = 0;
        }
        else
        {
            *timeout_ns_ -= time_use;
        }
        
        ts_begin_ = ts_end_;
    }

    /**
     * @brief      判断timeout counter是否超时
     *
     * @return     如果超时则返回true，否则返回false
     */
    inline bool IsTimeout()
    {
        return *timeout_ns_ == 0;
    }

    /**
     * @brief      重置timeout counter，timeout将会从新开始计时，超时时间
     *             间隔不变
     */
    inline void Reset()
    {
        *timeout_ns_ = initial_timeout_ns_;
        is_reset_ = true;
    }

    inline void Reset(uint64_t timeout_ns)
    {
        *timeout_ns_ = timeout_ns;
        is_reset_ = true;
    }

    inline uint64_t timeout_ns() { return initial_timeout_ns_; }

    inline uint64_t timeout_remain_ns() { return *timeout_ns_; }

private:
    const int32_t   sleep_inteval_;
    bool            is_reset_;
    const uint64_t  initial_timeout_ns_;
    uint64_t        var_timeout_ns_;
    uint64_t*       timeout_ns_;
    struct timespec ts_begin_;
    struct timespec ts_end_;
};

} // adk

#endif // ADK_TIMEOUT_COUNTER_H_
