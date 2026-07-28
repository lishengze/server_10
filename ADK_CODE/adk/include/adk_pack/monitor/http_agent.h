/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_MONITOR_HTTP_AGENT_H_
#define ADK_MONITOR_HTTP_AGENT_H_

#include "../error_code.h"

#include <functional>
#include <boost/system/system_error.hpp>

namespace adk
{

namespace monitor
{

class HttpAgent
{
public:
    typedef std::function<int(const std::string &config)> ReceiveUpdateConfig;
    typedef std::function<void(const boost::system::system_error&)> ErrorCallback;

    HttpAgent();
    ~HttpAgent();

    void Start(uint16_t port,
               const std::string& ip = std::string(),
               const ErrorCallback& error_callback = ErrorCallback());

    void Stop();

    void RegisterGetHttpURL(const std::string& url, std::function<std::string(const std::string&)> callback);

    void RegisterPutHttpURL(const std::string& url, std::function<bool(const std::string&)> callback);

    ErrorCode_def RegisterReceiveUpdateConfig(const ReceiveUpdateConfig &receive);

    ErrorCode_def UnRegisterReceiveUpdateConfig();

private:
    void* http_agent_impl_;
};

}

}

#endif