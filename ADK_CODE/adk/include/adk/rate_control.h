/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*  @file rate_contorl.h
*  @brief 
**/

#ifndef ADK_IMPL_RATE_CONTROL_H_
#define ADK_IMPL_RATE_CONTROL_H_

#include <time.h>
#include <stdint.h>
#include <unistd.h>

namespace adk_impl
{
class RateController
{
public:
    constexpr static bool kBlock = true;

    /**
     * @brief      创建并初始化流控对象
     *
     * @param[in]  rate_per_sec  每秒的流速
     * @param[in]  max_apdu      最大应用协议单元，即每次Wait的最小流量
     *
     * @return     成功时返回流控对象
     */
    static RateController* Create(const ssize_t rate_per_sec,
                                  const size_t max_apdu = 1);

    /**
     * @brief      销毁流控对象
     *
     * @param      rc    被销毁的流控对象
     */
    static void Destroy(RateController* rc);

    /**
     * @brief      等待流量控制
     *
     * @param[in]  msgs_or_bytes  请求流量
     * @param[in]  is_block       是否阻塞等待，默认为非阻塞
     *
     * @return     流控通过时返回true，否则返回false
     */
    bool Wait(const size_t msgs_or_bytes, bool is_block = false);

    /**
     * @brief      释放在Wait接口上等待的线程
     */
    void ReleaseWaitThread();

protected:
    ssize_t         rate_per_sec_ = 0;
    ssize_t         rate_per_msec_ = 0;
    ssize_t         rate_limit_ = 0;
    ssize_t         rate_burst_ = 1;
    struct timespec last_rate_check_ = {0,0};
    volatile bool   is_release_alert_ = false;

    RateController() {}
    RateController(const RateController&) {}
};
} // adk
#endif // ADK_RATE_CONTROL_H_
