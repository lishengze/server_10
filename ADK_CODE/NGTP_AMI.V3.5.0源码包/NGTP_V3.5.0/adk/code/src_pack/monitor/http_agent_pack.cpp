#include <adk/monitor/http_agent.h>
#include <adk_pack/monitor/http_agent.h>

namespace adk
{

namespace monitor
{

using HttpAgentImpl = adk_impl::monitor::HttpAgent;

HttpAgent::HttpAgent()
{
    http_agent_impl_ = new HttpAgentImpl;
}

HttpAgent::~HttpAgent()
{
    delete reinterpret_cast<HttpAgentImpl*>(http_agent_impl_);
}

void HttpAgent::Start(uint16_t port, const std::string& ip, const ErrorCallback& error_callback)
{
    reinterpret_cast<HttpAgentImpl*>(http_agent_impl_)->Start(port, ip, error_callback);
}

void HttpAgent::Stop()
{
    reinterpret_cast<HttpAgentImpl*>(http_agent_impl_)->Stop();
}

void HttpAgent::RegisterGetHttpURL(const std::string& url, std::function<std::string(const std::string&)> callback)
{
    reinterpret_cast<HttpAgentImpl*>(http_agent_impl_)->RegisterGetHttpURL(url, callback);
}

void HttpAgent::RegisterPutHttpURL(const std::string& url, std::function<bool(const std::string&)> callback)
{
    reinterpret_cast<HttpAgentImpl*>(http_agent_impl_)->RegisterPutHttpURL(url, callback);
}

ErrorCode_def HttpAgent::RegisterReceiveUpdateConfig(const ReceiveUpdateConfig &receive)
{
    return reinterpret_cast<HttpAgentImpl*>(http_agent_impl_)->RegisterReceiveUpdateConfig(receive);
}

ErrorCode_def HttpAgent::UnRegisterReceiveUpdateConfig()
{
    return reinterpret_cast<HttpAgentImpl*>(http_agent_impl_)->UnRegisterReceiveUpdateConfig();
}

}

}