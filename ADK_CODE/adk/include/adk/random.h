/**
 * @file random.h
 * @brief 随机数发生器
 * @author Li Yunchong
 * @version 0.1
 * @date 2016-12-05
 */
#ifndef ADK_IMPL_RANDOM_H_
#define ADK_IMPL_RANDOM_H_

#include <sys/times.h>

#include <cstdlib>
#include <iostream>
#include <boost/thread/mutex.hpp>
#include <boost/random/taus88.hpp>
#include <boost/random/uniform_int_distribution.hpp>
#include <boost/random/uniform_real_distribution.hpp>

/**
 * @brief ArchForce Develop Kit，华锐高性能计算工具集
 */

namespace adk_impl
{

/**
 * @brief 获取整形随机数
 *
 * @tparam T 整数类型
 * @param min 随机数取值范围最小值
 * @param max 随机数取值范围最大值
 *
 * @return 随机数
 */
template <typename T>
T Random(T min, T max)
{
    static boost::mutex s_mutex;
    static boost::random::taus88 s_rng(times(NULL));
    boost::mutex::scoped_lock lock_guard(s_mutex);
    return boost::random::uniform_int_distribution<T>(min, max)(s_rng);
}

/**
 * @brief 获取double型随机数
 *
 * @param min 随机数取值范围最小值
 * @param max 随机数取值范围最大值
 *
 * @return 随机数
 */
inline double Random(double min, double max)
{
    static boost::mutex s_mutex;
    static boost::random::taus88 s_rng(times(NULL));
    boost::mutex::scoped_lock lock_guard(s_mutex);
    return boost::random::uniform_real_distribution<double>(min, max)(s_rng);
}

} // namespace adk_impl

#endif /* ADK_RANDOM_H_ */
