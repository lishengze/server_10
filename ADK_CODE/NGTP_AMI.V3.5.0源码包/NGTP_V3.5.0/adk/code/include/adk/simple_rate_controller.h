/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*  @file simple_rate_controller.h
*  @brief 极简的流速限制工具类
**/

#ifndef AMI_IMPL_SIMPLE_RATE_CONTROLLER_H_
#define AMI_IMPL_SIMPLE_RATE_CONTROLLER_H_

#include <stdint.h>

namespace adk_impl
{
class SimpleRateCtrl
{
public:
    /**
     * @brief   构造函数
     *
     * @param[in]  rate  流速上限
     */
    SimpleRateCtrl(int32_t rate);

    /**
     * @brief      等待流速控制
     */
    void Wait();
#ifndef __ADK_DEBUG__
private:
#endif
    int32_t rate_;
    void*   impl_;
};

class SimpleVariableRateCtrl
{
public:
    /**
     * @brief   构造函数
     *
     * @param[in]  min_rate  流速下限
     * @param[in]  max_rate  流速上限
     */
    SimpleVariableRateCtrl(int32_t min_rate, int32_t max_rate);

    /**
     * @brief      等待流速控制
     */
    void Wait();

    /**
     * @brief      获取当前速率
     */
    int32_t GetCurrentRate();

#ifndef __ADK_DEBUG__
private:
#endif
    int32_t min_rate_;
    int32_t max_rate_;
    void*   impl_;
};

} // adk

#endif // AMI_SIMPLE_RATE_CONTROLLER_H_
