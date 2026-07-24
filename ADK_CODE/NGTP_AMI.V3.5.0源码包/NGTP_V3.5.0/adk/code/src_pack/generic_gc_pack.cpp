#include <adk/generic_gc.h>
#include <adk_pack/generic_gc.h>

namespace adk
{

using GCAgentImpl = adk_impl::GCAgent;
using GCRequestImpl = adk_impl::GCRequest;
using GenericGCImpl = adk_impl::GenericGC;

void GCAgent::PushGCRequest(GCRequest* req)
{
    reinterpret_cast<GCAgentImpl*>(this)->PushGCRequest(reinterpret_cast<GCRequestImpl*>(req));
}

GCAgent* GenericGC::CreateGCAgent(const std::string& gc_name, bool using_dedicated_channel, bool immediate_start, uint32_t channel_depth)
{
    return (GCAgent*)GenericGCImpl::CreateGCAgent(gc_name, using_dedicated_channel, immediate_start, channel_depth);
}

GCAgent* GenericGC::CreateGCAgent(const char* gc_name, bool using_dedicated_channel, bool immediate_start, uint32_t channel_depth)
{
    return (GCAgent*)GenericGCImpl::CreateGCAgent(gc_name, using_dedicated_channel, immediate_start, channel_depth);
}

void GenericGC::Start(const std::string& gc_name)
{
    GenericGCImpl::Start(gc_name);
}

void GenericGC::Finish(const std::string& gc_name)
{
    GenericGCImpl::Finish(gc_name);
}

void GenericGC::Dump(const std::string& gc_name, boost::property_tree::ptree& ptree)
{
    GenericGCImpl::Dump(gc_name, ptree);
}

std::string GenericGC::Dump(const std::string& gc_name, bool is_pretty)
{
    return GenericGCImpl::Dump(gc_name, is_pretty);
}

void GenericGC::ChangeParams(const std::string& gc_name, GenericArg& arg1)
{
    GenericGCImpl::ChangeParams(gc_name, (adk_impl::GenericArg&)arg1);
}

}