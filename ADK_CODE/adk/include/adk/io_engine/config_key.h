/** 
 *  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
 *  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
 *  For more information about Archforce, welcome to archforce.cn.
 *  @file config_key.h
 **/

#ifndef ADK_IMPL_IO_ENGINE_CONFIG_KEY_H_
#define ADK_IMPL_IO_ENGINE_CONFIG_KEY_H_

#include <string>

namespace adk_impl
{

namespace io_engine
{

namespace config
{

const std::string kName("Name");                                      ///> 指定引擎的名字[string]
const std::string kTxThreadNum("TxThreadNum");                        ///> 处理数据发送的线程数[uint32_t]
const std::string kRxThreadNum("RxThreadNum");                        ///> 处理数据接受的线程数[uint32_t]
const std::string kEventHandler("EventHandler");                      ///> 事件通知回调函数[EventHandler]
const std::string kAcceptHandler("AcceptHandler");                    ///> 服务器新连接回调[AcceptHandler]
const std::string kConnectHandler("ConnectHandler");                  ///> 连接完成回调函数[ConnectHandler]

const std::string kMessageIp("MessageIp");                            ///> 设置网卡IP用选择协议栈[string]
const std::string kIsTxLowLatency("IsTxLowLatency");                  ///> 是否发送方向开启低时延模式[bool]
const std::string kIsRxLowLatency("IsRxLowLatency");                  ///> 是否接受方向开启低时延模式[bool]
const std::string kMaxConnections("MaxConnections");                  ///> Engine管理最大连接数限制[uint32_t]
const std::string kPreSendHandler("PreSendHandler");                  ///> 消息预发送回调句柄[PreSendHandler]
const std::string kPreRecvHandler("PreRecvHandler");                  ///> 消息预接收回调句柄[PreRecvHandler]
const std::string kUseDuplexIOActor("UseDuplexIOActor");              ///> 使用收发双工的IO线程[bool]
const std::string kRxMemoryPoolSize("RxMemoryPoolSize");              ///> 接受方向内存池的大小[uint32_t]
const std::string kRxMemoryBlockSize("RxMemoryBlockSize");            ///> 接受方向内存块的大小[uint32_t]
const std::string kLocalPortRangeLow("LocalPortRangeLow");            ///> 本地端口随机分配范围下限闭区间[uint16_t]
const std::string kLocalPortRangeHigh("LocalPortRangeHigh");          ///> 本地端口随机分配范围上限闭区间[uint16_t]
const std::string kTxActorCpuAffinity("TxActorCpuAffinity");          ///> 发送线程绑定到CPU核列表
const std::string kRxActorCpuAffinity("RxActorCpuAffinity");          ///> 接收线程绑定到CPU核列表

const std::string kTxActorName("TxActorName");                      ///> TCPEngine发送线程名称[string]
const std::string kRxActorName("RxActorName");                      ///> TCPEngine接收线程名称[string]
const std::string kDupActorName("DupActorName");                    ///> TCPEngine duplex收发线程名称[string]
namespace acceptor
{

const std::string kListenIp("ListenIp");                              ///>*配置服务器监听地址[string]
const std::string kListenPort("ListenPort");                          ///>*配置服务器监听端口[uint16_t]
const std::string kEventHandler("EventHandler");                      ///> 事件通知回调函数(配置属性在没有更新的情况下将继承给Endpoint)[EventHandler]
const std::string kAcceptHandler("AcceptHandler");                    ///>*服务器新连接回调函数[AcceptHandler]
const std::string kMessageHandler("MessageHandler");                  ///> 消息回调函数(配置属性在没有更新的情况下将继承给Endpoint)[MessageHandler]
const std::string kDecodeTemplate("DecodeTemplate");                  ///> Stream消息解析模板(配置属性在没有更新的情况下将继承给Endpoint)[DecodeTemplate]
const std::string kHeartbeatHandler("HeartbeatHandler");              ///> 定时发送心跳回调函数(配置属性在没有更新的情况下将继承给Endpoint)[HeartbeatHandler]
const std::string kRateControlKBytes("RateControlKBytes");            ///> 流量控制(配置属性在没有更新的情况下将继承给Endpoint)[uint32_t]
const std::string kTxMessageQueueSize("TxMessageQueueSize");          ///> 发送队列大小(配置属性在没有更新的情况下将继承给Endpoint)[uint32_t]
const std::string kRxMessageQueueSize("RxMessageQueueSize");          ///> 接受队列大小(配置属性在没有更新的情况下将继承给Endpoint)[uint32_t]
const std::string kHeartbeatTimeoutMilli("HeartbeatTimeoutMilli");    ///> 心跳超时时间配置(配置属性在没有更新的情况下将继承给Endpoint)[uint32_t]

/// <socket option>
const std::string kReuseAddr("ReuseAddr");                            ///> sockopt[bool]
const std::string kReusePort("ReusePort");                            ///> sockopt[bool]
const std::string kTcpNoDelay("TcpNoDelay");                          ///> sockopt(配置属性在没有更新的情况下将继承给Endpoint)[bool]
const std::string kSocketSendBufferKBytes("SocketSendBufferKBytes");  ///> sockopt发送缓冲区大小(配置属性在没有更新的情况下将继承给Endpoint)[uint32_t]
const std::string kSocketRecvBufferKBytes("SocketRecvBufferKBytes");  ///> sockopt接受缓冲区大小(配置属性在没有更新的情况下将继承给Endpoint)[uint32_t]

/// <to do>
const std::string kDirectSend("DirectSend");                          ///> 服务器接受的新连接是否使用直接发送，DirectSend线程不安全[bool]

}

namespace endpoint
{

const std::string kRemoteIp("RemoteIp");                              ///>*配置客户端连接的服务器地址[string]
const std::string kRemotePort("RemotePort");                          ///>*配置客户端连接的服务器端口[uint16_t]
const std::string kLocalPort("LocalPort");                            ///> 绑定本本地端口(默认为Engine LocalPortRange范围内随机分配)[uint16_t]
const std::string kIsSingleton("IsSingleton");                        ///> 相同目标地址多次Connect返回Endpoint对象指向同一个网络资源[bool]
const std::string kShareContext("ShareContext");                      ///> 在创建时绑定ShareContext, 功能与Endpoint::set_share_ctx一致[void*]
const std::string kEventHandler("EventHandler");                      ///>*事件通知回调函数[EventHandler]
const std::string kConnectHandler("ConnectHandler");                  ///>*连接完成回调函数[ConnectHandler]
const std::string kMessageHandler("MessageHandler");                  ///> 消息回调函数[MessageHandler]
const std::string kDecodeTemplate("DecodeTemplate");                  ///> 消息流解析模板[DecodeTemplate]
const std::string kPrivateContext("PrivateContext");                  ///> 在创建时绑定PrivateContext, 功能与Endpoint::set_private_ctx一致[void*]
const std::string kHeartbeatHandler("HeartbeatHandler");              ///> 定时发送心跳回调函数[HeartbeatHandler]
const std::string kRateControlKBytes("RateControlKBytes");            ///> 流量控制[uint32_t]
const std::string kRetryConnectTimes("RetryConnectTimes");            ///> Connect失败时重试次数[uint32_t]
const std::string kTxMinResidentMicro("TxMinResidentMicro");          ///> 发送IO线程单次最短驻留时间[uint32_t]
const std::string kRxMinResidentMicro("RxMinResidentMicro");          ///> 接受IO线程单次最短驻留时间[uint32_t]
const std::string kTxMessageQueueSize("TxMessageQueueSize");          ///> 发送队列大小[uint32_t]
const std::string kConnectIntervalMilli("ConnectIntervalMilli");      ///> 失败重试间隔时间[uint32_t]
const std::string kHeartbeatTimeoutMilli("HeartbeatTimeoutMilli");    ///> 心跳超时时间配置[uint32_t]

/// <socket option>
const std::string kReuseAddr("ReuseAddr");                            ///> sockopt[bool]
const std::string kReusePort("ReusePort");                            ///> sockopt[bool]
const std::string kTcpNoDelay("TcpNoDelay");                          ///> sockopt[bool]
const std::string kSocketSendBufferKBytes("SocketSendBufferKBytes");  ///> sockopt发送缓冲区大小[uint32_t]
const std::string kSocketRecvBufferKBytes("SocketRecvBufferKBytes");  ///> sockopt接受缓冲区大小[uint32_t]

/// <moved to namespace acceptor - to delete>
const std::string kListenIp("ListenIp");                              ///>*配置服务器监听地址[string]
const std::string kListenPort("ListenPort");                          ///>*配置服务器监听端口[uint16_t]
const std::string kAcceptHandler("AcceptHandler");                    ///> 服务器新连接回调函数[AcceptHandler]

/// <to delete>
const std::string kLocalIp("LocalIp");                                ///> 绑定本地地址(默认为INADDR_ANY)[string]
const std::string kRxMessageQueueSize("RxMessageQueueSize");          ///> 接受队列大小[uint32_t]
const std::string kRxMessageBufferKBytes("RxMessageBufferKBytes");    ///> 连接接受缓冲区的大小[uint32_t]

/// <to do>
const std::string kDirectSend("DirectSend");                          ///> 是否使用费直接发送，DirectSend线程不安全[bool]

}

}

namespace default_value
{

constexpr bool     kReuseAddr = false;
constexpr bool     kReusePort = false;
constexpr bool     kTcpNoDelay = false;
constexpr bool     kDirectSend = false;
constexpr bool     kIsSingleton = false;
constexpr bool     kIsTxLowLatency = false;
constexpr bool     kIsRxLowLatency = false;
constexpr bool     kUseDuplexIOActor = false;

constexpr uint16_t kInvalidPort = 0;
constexpr uint32_t kTxThreadNum = 1;
constexpr uint32_t kRxThreadNum = 1;
constexpr uint16_t kMonitorPort = 0;
constexpr uint32_t kMaxConnections = 8192;
constexpr uint16_t kLocalPortRangeLow = 5000;
constexpr uint16_t kLocalPortRangeHigh = 65535;

constexpr uint32_t kRxMemoryPoolSize = 1024;
constexpr uint32_t kRxMemoryBlockSize = 8192;

constexpr uint32_t kRxMinResidentMicro = 1000;
constexpr uint32_t kTxMinResidentMicro = 1000;
constexpr uint32_t kRxMinResidentMicroLowLatency = 1000000;
constexpr uint32_t kTxMinResidentMicroLowLatency = 1000000;
constexpr uint32_t kRxMinResidentMicroNoLowLatency = 0;
constexpr uint32_t kTxMinResidentMicroNoLowLatency = 0;

constexpr uint32_t kRetryConnectTimes = 32;
constexpr uint32_t kTxMessageQueueSize = 8192;
constexpr uint32_t kConnectIntervalMilli = 1000;
constexpr uint32_t kRxMessageBufferKBytes = 8192;
constexpr uint32_t kRateControlKBytes = 0xFFFFFFFFu;
constexpr uint32_t kSocketSendBufferKBytes = 8192;
constexpr uint32_t kSocketRecvBufferKBytes = 8192;

/// <to delete>
constexpr uint32_t kRxMessageQueueSize = 8192;

///> TCP引擎tx线程默认名
const std::string kTxActorName("ioe-txactor");

///> TCP引擎rx线程默认名
const std::string kRxActorName("ioe-rxactor");

///> TCP引擎dup线程默认名
const std::string kDupActorName("ioe-dupactor");
}

}

}
#endif