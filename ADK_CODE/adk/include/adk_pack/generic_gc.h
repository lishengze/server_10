/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_GENERIC_GC_H_
#define ADK_GENERIC_GC_H_

#include "generic_arg.h"

#include <map>
#include <string>
#include <sstream>

#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace adk
{

#ifndef ADK_GGC_CHANNEL_DEPTH
#define ADK_GGC_CHANNEL_DEPTH (8192)
#endif // !ADK_GGC_CHANNEL_DEPTH

namespace gc
{

static GenericArg MiniGCPeriodMilli("MiniGCPeriodMilli");

static GenericArg place_holder("place_holder");

constexpr bool kDedicatedChannel = true;
constexpr bool kSharedChannel = false;

constexpr bool kImmediateStart = true;
constexpr bool kDelayStart = false;
} // gc

class GCRequest
{
public:
    virtual void DoGC() = 0;
};

class GCAgent
{
public:
    void PushGCRequest(GCRequest* req);

private:
    GCAgent() = default;
};

class GenericGC
{
public:
    static GCAgent* CreateGCAgent(const std::string& gc_name,
        bool using_dedicated_channel = false,
        bool immediate_start = gc::kImmediateStart,
        uint32_t channel_depth = ADK_GGC_CHANNEL_DEPTH);

    static GCAgent* CreateGCAgent(const char* gc_name,
        bool using_dedicated_channel = false,
        bool immediate_start = gc::kImmediateStart,
        uint32_t channel_depth = ADK_GGC_CHANNEL_DEPTH);

    static void Start(const std::string& gc_name);

    static void Finish(const std::string& gc_name);

    static void Dump(const std::string& gc_name, boost::property_tree::ptree& ptree);

    static std::string Dump(const std::string& gc_name, bool is_pretty = false);

    static void ChangeParams(const std::string& gc_name,
        GenericArg& arg1 = gc::place_holder);

private:
    GenericGC() = default;

    ~GenericGC() = default;
};
} // adk

#endif // ADK_GENERIC_GC_H_
