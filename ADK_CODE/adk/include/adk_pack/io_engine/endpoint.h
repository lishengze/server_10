/** 
 *  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
 *  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
 *  For more information about Archforce, welcome to archforce.cn.
 *  @file endpoint.h
 **/

#ifndef ADK_IO_ENGINE_ENDPOINT_H_
#define ADK_IO_ENGINE_ENDPOINT_H_

#include "message.h"

#include <assert.h>
#include <string.h>

#include <vector>
#include <functional>

#include "../error_code.h"

namespace adk
{

class Property;

namespace io_engine
{

class Endpoint
{
public:

    /**
     * @brief          创建IOEngine Message 用于零拷贝消息发送（接口线程安全）
     *
     * @param[in]      len 创建消息长度
     *
     * @return         成功返回Message对象指针 / 失败返回nullptr
     */
    Message* NewMessage(uint32_t len);

    /**
     * @brief          删除IOEngine Message
     * 
     * @note           消息发送失败需要应用主动删除消息
     *
     * @param[in]      message 待删除的消息
     *
     */
    void DeleteMessage(Message* message);

    /**
     * @brief          发送消息非零拷贝接口（异步调用 线程安全）
     *
     * @param[in]      data 发送消息体
     * @param[in]      len  发送消息长度
     *
     * @return         成功返回ErrorCode::kSuccess / 失败返回相应错误码
     */
    template<bool kBlock = true, bool kThreadSafe = true>
    inline int32_t SendMsg(const void* data, uint32_t len)
    {
        Message* const message = NewMessage(len);
        memcpy(message->data(), data, len);
        message->set_data_len(len);

        const auto ec = SendMsg<kBlock, kThreadSafe>(message);
        if (ErrorCode::kSuccess == ec)
        {
            return ErrorCode::kSuccess;
        }

        DeleteMessage(message);
        return ec;
    }

    /**
     * @brief          发送消息零拷贝接口（异步调用 线程安全）
     * @note           is_block==true: 当Endpoint的发送缓冲队列满时发送接口将会被阻塞，以下3种情形阻塞会被解除：
     *                 1、底层通信层正常发送消息，队列中有新的空间，SendMsg成功阻塞解除；
     *                 2、Endpoint不再使用调用Close后，Engine回收Endpoint时返回kFailure
     *                 3、主动调用Shutdown，所有阻塞以及后续SendMsg操作都会返回kFailure
     *                 is_block==false: 非阻塞发送，发送失败返回kWouldBlock/如果Endpoint异常返回kFailure
     *
     * @param[in]      msg 发送消息
     *
     * @return         成功返回ErrorCode::kSuccess / 失败返回相应错误码
     */
    template<bool kBlock = true, bool kThreadSafe = true>
    inline int32_t SendMsg(Message* const msg)
    {
        if (kBlock)
        {
            return kThreadSafe ? SendMsgBlock(msg) : SendMsgBlockUnsafe(msg);
        }
        else
        {
            return kThreadSafe ? SendMsgUnblock(msg) : SendMsgUnblockUnsafe(msg);
        }
    }

    /**
     * @brief          先多条连接发送消息 零拷贝接口（异步调用 线程安全）
     *
     * @param[in]      endpoint_vec 消息发往的连接
     * @param[in]      msg          发送的消息内容
     *
     * @return         成功返回ErrorCode::kSuccess / 失败返回相应错误码
     */
    static int32_t SendMsg(const std::vector<Endpoint*>& endpoint_vec, Message* const msg);

    /**
     * @brief          遍历未完成发送的消息
     *                 非线程安全，主要用于连接异常时提供故障处理辅助信息
     *
     * @param[in]      msg_handler 遍历消息回调函数
     *
     * @return         成功返回 指向消息的指针 / 失败返回 nullptr
     *                 失败即:所有异步待发送消息都已完成发送调用
     */
    int32_t GetPendingMsg(const std::function<int32_t(Message*)>& msg_handler);

    /**
     * @brief          同步调用接受网络数据
     *
     * @note           如果Endpoint创建时指定了MessageHandler，不支持调用接口
     *                 不支持多线程调用
     * 
     * @param[in/out]  buf 指向接受消息填充内存
     * @param[in]      len buf合法可用长度
     *
     * @return         大于0:     表示实际接受到的消息长度
     *                 小于等于0: 读取消息失败，具体可能性: 不支持同步调用；没有可读的消息；底层网络错误
     *                            底层网络错误会有EventHandler的事件回调
     */
    ssize_t Recv(char *buf, size_t len);

    /**
     * @brief          更新Endpoint属性
     *
     * @param[in]      props 更新属性信息
     * 支持修改的属性:
     *                 kEventHandler
     *                 kMessageHandler
     *                 kHeartbeatHandler
     *                 kHeartbeatTimeoutMilli
     *                 kDecodeTemplate
     *                 kTxMinResidentMicro
     *                 kRxMinResidentMicro
     */                
    void UpdateProperty(const Property& props);

    /**
     * @brief          异步关闭Endpoint
     *
     * @param          mode 0: 异步调用关闭当前连接, 函数返回后将不再有消息进行递交，
     *                         最终回收Endpoint资源时会有EndpointClosed的事件通知
     *                 mode 1: 异步关闭当前连接，函数返回后不会再用任何该Endpoint的回调，
     *                         Endpoint资源在函数返回后将不能进行访问
     */
    void Close(int32_t mode = 0);

    /**
     * @brief          重新发起连接
     *                 当TCP连接断开时，应用可以通过调用该接口重新发起连接
     *
     * @return         成功返回 ErrorCode::kSuccess / 失败返回其他
     */
    virtual int32_t Reconnect() = 0;

    /**
     * @brief          重新发起连接（支持重新指定目标地址）
     *                 当TCP连接断开时，应用可以通过调用该接口重新发起连接
     *
     * @param          remote_ip   目标IP地址
     * @param          remote_port 目标端口
     * 
     * @return         成功返回 ErrorCode::kSuccess / 失败返回其他
     */
    virtual int32_t Reconnect(const std::string& remote_ip, uint16_t remote_port) = 0;

    /**
     * @brief          判断当前Endpoint是否可以正常工作
     */
    bool IsReady() const;

    /**
     * @brief          终止Endpoint后台工作
     * @note           函数返回后该Endpoint的MessageHandler和HeartbeatHandler的回调函数不会再被调用，
     *                 且会释放发送阻塞的线程，所有阻塞以及后续SendMsg操作都会返回kWouldblock
     */
    void Shutdown();

    /**
     * @brief          获取Endpoint Id
     */
    int32_t endpoint_id() const;

    /**
     * @brief          获取虚拟连接的Index
     */
    uint32_t sub_index() const;

    /**
     * @brief          获该Endpoint是否为单例模式
     */
    bool is_singleton() const;

    /**
     * @brief          设置每个Endpoint私有的上下文
     * 
     * @param          private_ctx应用指定上下文
     */
    void set_private_ctx(void* const private_ctx);

    /**
     * @brief          获取每个Endpoint私有的上下文
     */
    void* private_ctx() const;

    /**
     * @brief          设置公共上下文，共同目标地址的singleton Endpoint共享该上下文
     *
     * @param          share_ctx应用指定上下文
     */
    void set_share_ctx(void* const share_ctx);

    /**
     * @brief          获取公共上下文
     */
    void* share_ctx() const;

    /**
     * @brief          获取对端的IP地址
     */
    const std::string& remote_ip() const;

    /**
     * @brief          获取对端的端口
     */
    const uint16_t remote_port() const;

    /**
     * @brief          获取本地的IP地址
     */
    const std::string& local_ip() const;

    /**
     * @brief          获取本地的端口
     */
    const uint16_t local_port() const;

    /**
     * @brief          通知TcpEngine不要再通过当前endpoint递交消息。
     */
    bool SetRxCork() ;

    /**
     * @brief          通知TcpEngine恢复对当前endpoint的消息递交
     */
    bool SetRxUncork();
protected:
    Endpoint() = default;

    int32_t SendMsgBlock(Message* const msg);

    int32_t SendMsgUnblock(Message* const msg);

    int32_t SendMsgBlockUnsafe(Message* const msg);

    int32_t SendMsgUnblockUnsafe(Message* const msg);
};

}

}

#endif