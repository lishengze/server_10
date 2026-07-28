#include <boost/lexical_cast.hpp>

#include <adk/rate_limit.h>

namespace adk_impl
{

bool IsRateLimit(RateLimitState& rs, LogFunction log_func)
{
    bool ret;
    timeval current_time;

    pthread_spin_lock(&rs.lock);
    gettimeofday(&current_time, NULL);
    const uint64_t current_time_value = current_time.tv_sec * (1000UL * 1000UL) + current_time.tv_usec;
    if (current_time_value > rs.begin)
    {
    	#ifdef __ADK_DEBUG__
    	std::cout << "current_time_value = " << current_time_value << " rs.begin = " << rs.begin << std::endl;
    	#endif

        if (log_func != NULL && rs.missed > 0)
        {
            log_func(std::string("Information: <") + boost::lexical_cast<std::string>(rs.missed)
                     + "> logs are missed during rate control");
        }

        rs.begin = current_time_value + rs.interval_micro;
        rs.printed = 0;
        rs.missed = 0;
    }

    if (rs.burst && rs.printed < rs.burst)
    {
    	#ifdef __ADK_DEBUG__
        std::cout << "rs.begin = " << rs.begin << std::endl;
        #endif

        ++rs.printed;
        ret = false;
    }
    else
    {
        ++rs.missed;
        ret = true;
    }
    pthread_spin_unlock(&rs.lock);

    return ret;
}

} // adk
