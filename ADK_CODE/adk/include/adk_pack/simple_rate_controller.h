/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*  @file simple_rate_controller.h
*  @brief 极简的流速限制工具类
**/

#ifndef AMI_SIMPLE_RATE_CONTROLLER_H_
#define AMI_SIMPLE_RATE_CONTROLLER_H_

#include <stdint.h>

namespace adk
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

private:
    int32_t rate_;
    void*   impl_;
};
} // adk

#endif // AMI_SIMPLE_RATE_CONTROLLER_H_
