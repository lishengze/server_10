/** 
 *  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
 *  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
 *  For more information about Archforce, welcome to archforce.cn.
 *  @file acceptor.h
 **/

#ifndef ADK_IMPL_IO_ENGINE_ACCEPTOR_H_
#define ADK_IMPL_IO_ENGINE_ACCEPTOR_H_

#include <string>

namespace adk_impl
{

namespace io_engine
{

class Acceptor
{
public:

    /**
     * @brief            停止acceptor
     */
    virtual void Close() = 0;

    /**
     * @brief            获取监听地址
     */
    virtual const std::string& listen_ip() const = 0;

    /**
     * @brief            获取监听端口
     */
    virtual uint16_t listen_port() const = 0;
};

}

}

#endif