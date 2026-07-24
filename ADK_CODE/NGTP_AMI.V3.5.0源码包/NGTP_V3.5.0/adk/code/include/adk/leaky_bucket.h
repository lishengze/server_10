#ifndef ADK_IMPL_TOKEN_BUCKET_H_
#define ADK_IMPL_TOKEN_BUCKET_H_

#include <chrono> //chrono
#include <adk/arch/generic.h>

namespace adk_impl
{

class LeakyBucket
{
public:
	typedef std::chrono::steady_clock Clock;
	typedef Clock::time_point TimePoint;

public:
	/**
     * @brief          构造函数
     *
     * @param[in]      leak_interval_micro   流控间隔
     *     			   per_leak_length 		 流控单位长度
     */
	LeakyBucket(uint32_t leak_interval_micro, uint32_t leak_length);

	/**
     * @brief          流控函数
     *
     * @param[in]      需要流控的总长度
     *     
     * @return		   返回可消费长度，如果无法消费返回0
     */
	uint32_t try_leak(uint32_t total_leak_length);

	/**
     * @brief          流控函数
     *
     * @param[in]      需要流控的总长度
     *     
     * @return		   返回可消费长度
     */
	uint32_t leak(uint32_t total_leak_length);

private:
	uint32_t leak_interval_micro_ 	= 0;
	uint32_t leak_length_ 			= 0;
	TimePoint next_leak_timepoint_;
};

} //namespace

#endif