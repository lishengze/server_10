/** 
 *  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
 *  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
 *  For more information about Archforce, welcome to archforce.cn.
 *  @file event.h
 **/

#ifndef ADK_IMPL_IO_ENGINE_EVENT_H_
#define ADK_IMPL_IO_ENGINE_EVENT_H_

#include <string>

namespace adk_impl
{

namespace io_engine
{

/**
 * @brief      事件类型
 */
enum EventType
{
	///> 底层网络出错        级别:error    
	///> 建议措施:关闭该连接 事件回调后该连接上将不再有消息递交
    kSocketError = 0,
	///> 异步连接失败        级别:error
	///> 建议措施:关闭该连接 事件回调后该连接上将不再有消息递交
    kConnectFailed,
	///> 网络资源清理完成    级别:info     
	///> 事件回调后，应用持有的Endpoint将不能继续访问
    kEndpointClosed,
	///> 心跳超时            级别:warn
	///> 建议措施:关闭该连接
    kHeartbeatTimeout,
	///> 处理连接数已达上限  级别:warn
	///> Endpoint资源泄露，建议排查代码逻辑
    kNoResources,
};

/**
 * @brief      事件级别
 */
enum EventLevel
{
    kInfo = 0,
    kWarn,
    kError,
    kFatal,
};

class Event
{
public:
    virtual ~Event() {}

    /**
    * @brief      获取事件类型
    *
    * @return     事件类型
    */
    virtual EventType type() = 0;

    /**
    * @brief      获取事件级别
    *
    * @return     事件级别
    */
    virtual EventLevel level() = 0;

    /**
    * @brief      获取事件描述
    *
    * @return     事件描述信息
    */
    virtual std::string what() = 0;
};

}

}

#endif