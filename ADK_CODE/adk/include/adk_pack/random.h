/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/
#ifndef ADK_RANDOM_H_
#define ADK_RANDOM_H_

/**
 * @brief ArchForce Develop Kit，华锐高性能计算工具集
 */

namespace adk
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

int16_t Random(int16_t min, int16_t max);

uint16_t Random(uint16_t min, uint16_t max);

int32_t Random(int32_t min, int32_t max);

uint32_t Random(uint32_t min, uint32_t max);

int64_t Random(int64_t min, int64_t max);

uint64_t Random(uint64_t min, uint64_t max);


/**
 * @brief 获取double型随机数
 *
 * @param min 随机数取值范围最小值
 * @param max 随机数取值范围最大值
 *
 * @return 随机数
 */
double Random(double min, double max);

} // namespace adk

#endif /* ADK_RANDOM_H_ */
