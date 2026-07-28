/**
 * Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
 * Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
 * For more information about Archforce, welcome to archforce.cn.
 * @file pipe.h
 * @brief 命名管道，用于线程间或进程间低速通信
 */
#ifndef ADK_IMPL_PIPE_H_
#define ADK_IMPL_PIPE_H_

#include <adk/error_code.h>

#include <stdint.h>

#include <string>

namespace adk_impl
{

enum PipeFlag
{
    kReadOnly,
    kWriteOnly,
    kReadWrite,
};

class Pipe
{
public:
    ~Pipe();

    /**
     * @brief      创建管道
     *
     * @param[in]  path      管道的路径，同文件系统路径格式
     * @param[in]  rw_flag   读写标志
     * @param[in]  is_block  创建时是否阻塞等待管道对端
     *
     * @return     成功时返回管道对象，失败时返回nullptr
     */
    static Pipe* Create(const std::string& path, PipeFlag rw_flag, bool is_block = false);

    /**
     * @brief      从系统中销毁管道
     *
     * @param[in]  path  管道的路径，同文件系统路径格式
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    static int32_t Destroy(const std::string& path);

    /**
     * @brief      向管道中写入数据
     *
     * @param[in]       buf         数据的首地址
     * @param[in,out]   len         数据的长度
     * @param[in]       timeout_ns  写入超时时间
     * 
     * @note       当多进程同时写入的数据不大于4096字节时，该接口可以保证写入的原子性
     *
     * @return     成功时返回ErrorCode::kSuceess,写入超时时返回ErrorCode::kWouldblock,
     *             当对端不存在时返回ErrorCode::kFailure,实际写入的长度由len返回
     */
    int32_t Write(const void* buf, uint32_t& len, uint64_t timeout_ns);

    /**
     * @brief      从管道中读取数据
     *
     * @param[in]       buf         数据的首地址
     * @param[in,out]   len         数据的长度
     * @param[in]       timeout_ns  读取超时时间
     *
     * @return     读取成功时返回ErrorCode::kSuccess,读取超时时返回ErrorCode::kWouldblock,
     *             当对端不存在时返回ErrorCode::kFailure,实际读取的长度由len返回
     */
    int32_t Read(void* buf, uint32_t& len, uint64_t timeout_ns);

    /**
     * @brief      修改pipe设备的根路径，默认根路径为 /tmp/${user}_pipe/
     *
     * @param[in]  rpath  新的文件系统跟路径地址
     */
    static void ChangeRootPath(const std::string& rpath);

private:
    int pfd_;

    Pipe();
};

} // adk

#endif // AMI_PIPE_H_
