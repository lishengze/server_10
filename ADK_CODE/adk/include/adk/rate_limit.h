#ifndef ADK_IMPL_RATE_LIMIT_H_
#define ADK_IMPL_RATE_LIMIT_H_

#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>

#include <string>

#ifdef __ADK_DEBUG__
#include <iostream>
#endif

namespace adk_impl
{

#define ADK_DEFAULT_RATELIMIT_INTERVAL_MICRO        (500UL * 1000UL)
#define ADK_DEFAULT_RATELIMIT_BURST                 5

struct RateLimitState
{
    pthread_spinlock_t  lock;
    uint64_t            begin;
    uint32_t            interval_micro;
    uint32_t            burst;
    uint32_t            printed;
    uint32_t            missed;

    RateLimitState(uint32_t user_interval = 0, uint32_t user_burst = 0)
    {
    	#ifdef __ADK_DEBUG__
    	std::cout << "user_interval = " << user_interval << " user_burst = " << user_burst << std::endl;
    	#endif

        pthread_spin_init(&lock, PTHREAD_PROCESS_PRIVATE);
        if (user_interval == 0)
        	interval_micro = ADK_DEFAULT_RATELIMIT_INTERVAL_MICRO;
        else
        	interval_micro = user_interval;

        if (user_burst == 0)
        	burst = ADK_DEFAULT_RATELIMIT_BURST;
        else
        	burst = user_burst;

        timeval current_time;
        gettimeofday(&current_time, NULL);
        begin = current_time.tv_sec * (1000UL * 1000UL) + current_time.tv_usec;

        printed = 0;
        missed = 0;
    }
};

typedef void (*LogFunction)(const std::string& message);

#define ADK_DEFINE_RATELIMIT_STATE_DEFAULT(_rs)   static adk_impl::RateLimitState _rs
#define ADK_DEFINE_RATELIMIT_STATE(_rs, interval_micro, burst)   static adk_impl::RateLimitState _rs(interval_micro, burst)

#define  ADK_EXE_RATE_LIMIT_DEFAULT(op)    \
    do {    \
        ADK_DEFINE_RATELIMIT_STATE_DEFAULT(_rs) ;   \
        if (!IsRateLimit(_rs))  \
            op ;     \
    } while (false)

#define ADK_EXE_RATE_LIMIT(op,interval_micro,burst)      \
    do {    \
        ADK_DEFINE_RATELIMIT_STATE(_rs,interval_micro,burst) ;   \
        if (!IsRateLimit(_rs))  \
            op ;     \
    } while (false)

extern bool IsRateLimit(RateLimitState& rs, LogFunction log_func = NULL);
} // adk

#endif // ADK_RATE_LIMIT_H_
