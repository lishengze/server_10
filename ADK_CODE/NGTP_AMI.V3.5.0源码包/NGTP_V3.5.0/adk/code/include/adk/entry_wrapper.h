/**
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved.
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.
*  For more information about Archforce, welcome to archforce.cn.
**/

#pragma once

#include <thread>
#include <functional>
#include <boost/thread/thread.hpp>
#include <adk/libadk.h>
namespace adk_impl
{

ADK_API std::thread std_thread(const char* short_name, const char* full_name, std::function<void()> f);

ADK_API boost::thread boost_thread(const char* short_name, const char* full_name, std::function<void()> f);

}