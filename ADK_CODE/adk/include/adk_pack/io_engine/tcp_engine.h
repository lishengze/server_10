/** 
 *  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
 *  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
 *  For more information about Archforce, welcome to archforce.cn.
 *  @file tcp_engine.h
 **/

#ifndef ADK_TCP_ENGINE_H_ 
#define ADK_TCP_ENGINE_H_

#include <string>
#include <cstdint>

namespace adk
{

class Property;

namespace io_engine
{

class Message;
class Acceptor;
class Endpoint;

class TcpEngine
{
public:
    enum StackType : int32_t
    {
        kTcpIp = 0,
        kTcpDirect,
        kUnknown
    };

    /**
     * @brief          创建TcpEngine对象
     *
     * @param[in]      engine_props engine配置参数
     * 重要配置属性：
     *                 "TxThreadNum"        处理数据发送的线程数 默认值: uint32_t 1
     *                 "RxThreadNum"        处理数据接受的线程数 默认值: uint32_t 1
     *                 "MaxConnections"     Engine管理最大连接数限制 默认值: uint32_t 8192
                       "LocalPortRangeLow"  本地端口随机分配范围下限(闭区间) 默认值: uint16_t 5000
                       "LocalPortRangeHigh" 本地端口随机分配范围上限(闭区间) 默认值: uint16_t 65535
     * 其他配置：     
     *                 "EventHandler"   全局默认事件通知回调函数 默认值: EventHandler* nullptr
     *                 "AcceptHander"   全局默认新连接回调函数 默认值: AcceptHandler* nullptr
     *                 "ConnectHandler" 全局默认连接成功回调函数 默认值: ConnectHandler* nullptr
     *
     * @return         创建成功返回对象，创建失败返回nullptr
     */
    static TcpEngine* Create(const Property& engine_props);

    /**
     * @brief          销毁已创建的TcpEngine对象
     *
     * @param[in]      tcp_engine 已创建的tcp engine对象
     *     
     */
    static void Destroy(TcpEngine* const tcp_engine);

    /**
     * @brief           根据传入的网卡IP地址判断网卡支持的加速协议类型
     * 
     * @param[in]       指定网卡地址
     *                  如果传入的网卡地址为空则选择第一个网卡进行判断
     * 
     * @return          StackType::kTcpIp       TcpIp协议栈
     *                  StackType::kTcpDirect   sfc-zocket
     */
    static StackType GetStackType(const std::string& message_ip);

    /**
    * @brief          获取错误信息
    *                 主要用来获取Accept/Connect失败原因
    *
    * @return         具体错误信息描述
    */
    static const char* GetLastError();

    /**
     * @brief          暂停消息递交
     * 
     * @return         成功返回ErrorCode::kSuccess / 失败返回其他
     */
    static int32_t Pause();

    /**
     * @brief          恢复消息递交
     */
    static void Resume();

    /**
     * @brief          发起异步Accept
     *
     * @param[in]      accept_props 创建Endpoint配置属性
     * 重要配置属性：
     *                 "ListenIp"       配置监听地址 必填 string
     *                 "ListenPort"     配置监听端口 必填 uint16_t
     *                 "EventHandler"   事件通知回调函数 默认值: EventHandler* nullptr
     *                 "AcceptHander"   新连接回调函数   默认值: AcceptHandler* nullptr
     * Note: 如果EventHandler或AcceptHander为nullptr则以全局默认为准，最终为nullptr则函数调用失败
     * 其他配置：      "MessageHandler"         配置消息回调函数 默认值: MessageHandler* nullptr
     *                 "HeartbeatHandler"       配置定时任务回调 默认值: HeartbeatHandler* nullptr
     *                 "RateControlKBytes"      配置发送每秒流控 默认值: uint32_t kuint32Max
     *                 "HeartbeatTimeoutMilli"  配置心跳超时时间 默认值: uint32_t kuint32Max
     * TCP属性配置     "ReuseAddr"              配置Tcp属性 默认值: bool false
     *                 "TcpNoDelay"             配置Tcp属性 默认值: bool false
     *                 "SocketSendBufferKBytes" 配置Tcp属性 默认值: uint32_t 8192
     *                 "SocketRecvBufferKBytes" 配置Tcp属性 默认值: uint32_t 8192
     * Note: 如下配置可以在AcceptHander回调接口中重新指定
     *                 "MessageHandler"         如果MessageHandler最终为nullptr
     *                 "HeartbeatHandler"       如果HeartbeatHandler最终为nullptr则不执行心跳任务
     *                 "RateControlKBytes"
     *                 "HeartbeatTimeoutMilli"  超时时间未接收到任何数据则会上报心跳超时事件
     *       
     *
     * @return         成功返回Acceptor对象指针 / 失败返回 nullptr
     */
    Acceptor* Accept(const Property& accept_props);

    /**
     * @brief          发起异步Connect
     *
     * @param[in]      connect_props 创建Endpoint配置属性
     * 重要配置属性：                       
     *                 "RemoteIp"       配置连接地址 必填 string
     *                 "RemotePort"     配置连接端口 必填 uint16_t
     *                 "EventHandler"   事件通知回调函数 默认值: EventHandler* nullptr
     *                 "ConnectHandler" 连接成功回调函数 默认值: ConnectHandler* nullptr
     *                 "LocalIp"        本地地址配置 string 如果未配置则由系统分配INADDR_ANY
     *                 "LocalPort"      本地端口配置 uint16_t 默认为系统分配
     *                 "IsSingleton"    指定Endpoint是否为单例模式 默认值: bool false
     *                                  若配置为单例模式，创建Endpoint底层共享连接(RemoteIp,RemotePort)
     *                 "RetryConnectTimes"    配置连接重试次数 默认值: uint32_t 32
     *                 "ConnectIntervalMilli" 配置重试间隔时间 默认值: uint32_t 1000
     * Note: 如果EventHandler或ConnectHandler为nullptr则以全局默认为准，最终为nullptr则函数调用失败
     * 其他配置：
     *                 "MessageHandler"         配置消息回调函数 默认值: MessageHandler* nullptr
     *                 "HeartbeatHandler"       配置定时任务回调 默认值: HeartbeatHandler* nullptr
     *                 "RateControlKBytes"      配置发送每秒流控 默认值: uint32_t kuint32Max
     *                 "HeartbeatTimeoutMilli"  配置心跳超时时间 默认值: uint32_t kuint32Max
     *                                          超时未接受到任何消息则认为对端异常主动断开连接
     * TCP属性配置     "TcpNoDelay"             配置Tcp属性 默认值: bool false
     *                 "SocketSendBufferKBytes" 配置Tcp属性 默认值: uint32_t 8192
     *                 "SocketRecvBufferKBytes" 配置Tcp属性 默认值: uint32_t 8192
     * Note: 如下配置可以在ConnectHandler回调接口中重新指定
     *                 "MessageHandler"         如果MessageHandler最终为nullptr，
	 *                                          消息不进行异步回调，可以主动调用Endpoint::Recv
     *                 "HeartbeatHandler"       如果HeartbeatHandler最终为nullptr则不执行心跳任务
	 *                 "RateControlKBytes"
     *                 "HeartbeatTimeoutMilli"  超时时间未接收到任何数据则会上报心跳超时事件
     *
     * @return         成功返回Endpoint对象指针 / 失败返回 nullptr
     */
    Endpoint* Connect(const Property& connect_props);

    /**
     * @brief          创建IOEngine Message 用于零拷贝消息发送（接口线程安全）
     *
     * @param[in]      len 创建消息长度
     *
     * @return         成功返回Message对象指针 / 失败返回nullptr
     */
    Message* NewMessage(uint32_t len);

    /**
     * @brief          删除消息对象
     *
     * @param[in]      message 待删除消息
     */
    void DeleteMessage(Message* message);

    /**
     * @brief          收集TcpEngine的内部指标信息
     * 
     * @param[out]     具体的指标内容，格式为Json
     *
     * @return         成功返回ErrorCode::kSuccess / 失败返回其他
     */
    int32_t CollectIndicator(std::string& indicator);

protected:
    TcpEngine() = default;
};

}

}

#endif