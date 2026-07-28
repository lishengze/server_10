#include <boost/lexical_cast.hpp>

#include <adk/monitor/monitor.h>
#include <adk_pack/monitor/monitor.h>

namespace adk
{

int32_t EventChannel::PushEvent(const boost::property_tree::ptree& ptree)
{
    return reinterpret_cast<adk_impl::EventChannel*>(this)->PushEvent(ptree);
}

int32_t EventChannel::PushEvent(const EventFunctorType& func)
{
    return reinterpret_cast<adk_impl::EventChannel*>(this)->PushEvent(func);
}

int32_t EventChannel::PushEvent(EventFunctionType func, void* user)
{
    return reinterpret_cast<adk_impl::EventChannel*>(this)->PushEvent(func, user);
}

MonitorOps::MonitorOps()
{
    is_collection_indicator = false;
    static const char* s_env_mon_interval = std::getenv("ADK_MONITOR_INTERVAL_MILLI");
    if (s_env_mon_interval != NULL)
    {
        try
        {
            collection_interval_milli = boost::lexical_cast<int32_t>(s_env_mon_interval);
            return;
        }
        catch (...)
        {
        }
    }
    collection_interval_milli = ADK_DEFAULT_COLLECTION_INTERVAL_MILLI;
}


EventChannel* Monitor::RegisterObject(const std::string& class_name, const std::string& object_name,
    MonitorOps* monitor_ops, const std::string& parent_class, const std::string& parent_name)
{
    return (EventChannel*)adk_impl::Monitor::RegisterObject(class_name, object_name,
        reinterpret_cast<adk_impl::MonitorOps*>(monitor_ops), parent_class, parent_name);
}

int32_t Monitor::UnregisterObject(const std::string& class_name, const std::string& object_name, 
    const std::string& parent_class, const std::string& parent_name)
{
    return adk_impl::Monitor::UnregisterObject(class_name, object_name, parent_class, parent_name);
}

int32_t Monitor::PluginSinker(IMonitorSinker* sinker)
{
    return adk_impl::Monitor::PluginSinker(reinterpret_cast<adk_impl::IMonitorSinker*>(sinker));
}

int32_t Monitor::PlugoutSinker(IMonitorSinker* sinker)
{
    return adk_impl::Monitor::PlugoutSinker(reinterpret_cast<adk_impl::IMonitorSinker*>(sinker));
}

int32_t Monitor::Start()
{
    return adk_impl::Monitor::Start();
}

int32_t Monitor::Stop()
{
    return adk_impl::Monitor::Stop();
}

void Monitor::SetShardingIndex(int32_t sharding_index, int32_t sharding_number)
{
    adk_impl::Monitor::SetShardingIndex(sharding_index, sharding_number);
}

int32_t Monitor::SubmitRequest(const uint64_t query_key, const std::string& url, 
    const boost::property_tree::ptree& query_condition, const int32_t query_type)
{
    return adk_impl::Monitor::SubmitRequest(query_key, url, query_condition, query_type);
}

int32_t Monitor::SubmitRequest(const QueryFunctorType& func)
{
    return adk_impl::Monitor::SubmitRequest(func);
}

int32_t Monitor::SubmitRequest(QueryFunctionType func, void* user)
{
    return adk_impl::Monitor::SubmitRequest(func, user);
}

int32_t Monitor::SubmitRequest(IMonitorSinker::Type from, const uint64_t query_key, 
    const std::string& url, const boost::property_tree::ptree& query_condition, const int32_t query_type)
{
    return adk_impl::Monitor::SubmitRequest(adk_impl::IMonitorSinker::Type(from), 
        query_key, url, query_condition, query_type);
}

int32_t Monitor::SubmitRequest(IMonitorSinker::Type from, const QueryFunctorType& func)
{
    return adk_impl::Monitor::SubmitRequest(adk_impl::IMonitorSinker::Type(from), func);
}

int32_t Monitor::SubmitRequest(IMonitorSinker::Type from, QueryFunctionType func, void* user)
{
    return adk_impl::Monitor::SubmitRequest(adk_impl::IMonitorSinker::Type(from), func, user);
}

int32_t Monitor::ChangeCollectionInterval(const std::string& class_name, uint32_t milli)
{
    return adk_impl::Monitor::ChangeCollectionInterval(class_name, milli);
}

int32_t Monitor::EnableCollection(const std::string& class_name, const std::string& object_name, uint32_t milli)
{
    return adk_impl::Monitor::EnableCollection(class_name, object_name, milli);
}

} // adk
