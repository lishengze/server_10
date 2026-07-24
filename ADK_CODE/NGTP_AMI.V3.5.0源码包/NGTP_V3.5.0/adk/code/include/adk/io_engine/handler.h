/** 
 *  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
 *  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
 *  For more information about Archforce, welcome to archforce.cn.
 *  @file handler.h
 **/

#ifndef ADK_IMPL_IO_ENGINE_HANDLER_H_
#define ADK_IMPL_IO_ENGINE_HANDLER_H_

#include <stdint.h>

namespace adk_impl
{

class Property;

namespace io_engine
{

class Event;
class Message;
class Endpoint;

#ifndef BASE_TYPE_DEFINE
#define BASE_TYPE_DEFINE(HandlerType)  typedef HandlerType base_class
#endif

#ifndef BASE_TYPE_CAST
#define BASE_TYPE_CAST(HandlerType) typename HandlerType::base_class*
#endif

class EventHandler
{
public:
    BASE_TYPE_DEFINE(EventHandler);

    virtual ~EventHandler() = default;

    /**
     * @brief      当出现异常时调用应用注册的回调函数
     *
     * @param      endpoint 出现异常的endpoint
     * @param      event    异常的详细信息
     */
    virtual void OnEvent(Endpoint* endpoint, Event* event) = 0;
};

class AcceptHandler
{
public:
    BASE_TYPE_DEFINE(AcceptHandler);

    virtual ~AcceptHandler() = default;

    /**
     * @brief      accept新连接时调用应用注册的回调函数
     *
     * @param      endpoint accept连接对应的endpoint
     * @param      ep_props 应用可以通过ep_props来修改endpoint的配置
     *
     * @note       如果需要弃用该endpoint可以回调函数直接调用Endpoint::Close
     *             弃用后该Endpoint指针将不能再持有使用
     */
    virtual void OnAccept(Endpoint* endpoint, Property& ep_props) = 0;
};

class ConnectHandler
{
public:
    BASE_TYPE_DEFINE(ConnectHandler);

    virtual ~ConnectHandler() = default;

    /**
     * @brief      连接成功调用应用注册的回调函数
     *
     * @param      endpoint 连接成功的endpoint
     * @param      ep_props 应用可以通过ep_props来修改endpoint的配置
     *
     * @note       如果需要弃用该endpoint可以回调函数直接调用Endpoint::Close
     *             弃用后该Endpoint指针将不能再持有使用
     */
    virtual void OnConnect(Endpoint* endpoint, Property& ep_props) = 0;
};

class MessageHandler
{
public:
    BASE_TYPE_DEFINE(MessageHandler);

    enum Result : int32_t
    {
        kSuccess = 0,
        kFollowUp
    };

    virtual ~MessageHandler() = default;

    /**
     * @brief      收到消息调用应用注册的回调函数
     *
     * @return     返回值: 
     *             1. Result::kSuccess 表示当前消息全部被应用接受；
     *             函数返回后消息将被回收，除非消息的管理权被应该接管，详细参考Message::forward_acquire函数说明
     *             2. Result::kFollowUp 表示该消息需要和后续消息结合处理，详细参考Message::set_follow_up函数说明
     * @note       如果Endpoint注册了MessageTemplate，Result::kFollowUp功能会被关闭
     */
    virtual int32_t OnMessage(Message* message) = 0;
};

class DecodeTemplate
{
public:
    BASE_TYPE_DEFINE(DecodeTemplate);

    virtual ~DecodeTemplate() = default;

    /**
     * @brief      消息解析模板
     * 
     * @return     返回值：
     *             -1: 表示从已知消息体中无法确定当前消息的长度
     *             >0: 解析出的当前消息的长度
     *             0 : 无意义消息将被丢弃
     */
    virtual int32_t MessageLength(const void* msg_data, uint32_t len) = 0;
};

class HeartbeatHandler
{
public:
    BASE_TYPE_DEFINE(HeartbeatHandler);

    virtual ~HeartbeatHandler() = default;

    /**
     * @brief     Endpoint超过间隔时间(GetPeriodMilli)没有数据传输
     *            将触发SendHBMsg函数调用
     *
     * @param     Endpoint* endpoint 相应的endpoint
     */
    virtual void SendHBMsg(Endpoint* endpoint) = 0;

    /**
     * @brief     获取心跳间隔时间单位为毫秒
     */
    virtual uint32_t GetPeriodMilli() = 0;
};

class PreSendHandler
{
public:
    BASE_TYPE_DEFINE(PreSendHandler);

    enum PreTxResult : int32_t
    {
        kSuccess = 0,
        kCallOnce,
        kDrop
    };

    virtual ~PreSendHandler() = default;

    /**
     * @brief     发送前消息回调应用
     *
     * @param     ep_share_ctx IO线程当前处理的Endpoint的share context
     * @param     message  具体消息的内容
     *
     * @return    返回值 PreTxResult::kSucess:       消息继续发送
     *                   PreTxResult::kCallOnce:     消息继续发送，发送成功与否只回调一次
     *                   PreTxResult::kDrop或其他值: 消息将被丢弃
     */
    virtual int32_t OnTxMessageBefore(void* ep_share_ctx, Message* message) = 0;

    /**
     * @brief     消息发送完成后的回调
     * 
     * @param     endpoint ep_share_ctx IO线程当前处理的Endpoint的share context
     * @param     result   底层IO的执行结果即实际IO成功的字节数
     *                     小于0，表示IO执行失败
     *
     */
    virtual void OnTxMessageAfter(void* ep_share_ctx, int32_t result) = 0;
};

class PreRecvHandler
{
public:
    BASE_TYPE_DEFINE(PreRecvHandler);

    virtual ~PreRecvHandler() = default;

    /**
     * @brief     io线程每次调用网络IO接口前进行回调
     *
     * @param     ep_share_ctx IO线程当前处理的Endpoint的share context
     */
    virtual void OnRxMessageBefore(void* ep_share_ctx) = 0;

    /**
     * @brief     io线程每次调用网络IO接口后进行回调
     *
     * @param     ep_share_ctx IO线程当前处理的Endpoint的share context
     * @param     result       底层IO的执行结果即实际IO成功的字节数
     *                         小于等于0，表示IO没有执行成功
     * 
     */
    virtual void OnRxMessageAfter(void* ep_share_ctx, int32_t result) = 0;
};

template<class HandlerType>
BASE_TYPE_CAST(HandlerType) MakeHandler(HandlerType* handler)
{
    return static_cast<BASE_TYPE_CAST(HandlerType)>(handler);
}

template<class HandlerType>
BASE_TYPE_CAST(HandlerType) MakeHandler(HandlerType& handler)
{
    return static_cast<BASE_TYPE_CAST(HandlerType)>(&handler);
}

}

}

#endif