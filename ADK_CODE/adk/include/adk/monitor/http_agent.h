/**
 * @file http_agent.h
 * @brief HTTP监控代理
 * @author Li Yunchong
 * @version 0.1
 * @date 2017-05-17
 */
#ifndef ADK_IMPL_MONITOR_HTTP_AGENT_H_
#define ADK_IMPL_MONITOR_HTTP_AGENT_H_

#include "../error_code.h"
#include "../http_server.h"
#include "../monitor/monitor.h"
#include "../response_builder.h"

#include <map>
#include <deque>
#include <mutex>
#include <string>
#include <functional>

#include <boost/system/system_error.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace adk_impl
{

namespace monitor
{

class HttpAgent : public IMonitorSinker
{
    typedef http::Server<http::HTTP> HttpServer;
    typedef std::shared_ptr<HttpServer::Request> RequestPtr;
    typedef std::shared_ptr<HttpServer::Response> ResponsePtr;
    typedef http::ResponseBuilder<http::HTTP> ResponseBuilder;
    typedef IMonitorSinker IMonitorSinker;
    typedef boost::property_tree::ptree ptree;

public:
    typedef boost::function<void (const boost::system::system_error&)> ErrorCallback;
    typedef std::function<int (const std::string &config)> ReceiveUpdateConfig;

    HttpAgent();

    ~HttpAgent();

    void Start(uint16_t port,
               const std::string& ip = std::string(),
               const ErrorCallback& error_callback = ErrorCallback());

    void Stop();

    void RegisterGetHttpURL(const std::string& url, std::function<std::string(const std::string&)> callback);

    void RegisterPutHttpURL(const std::string& url, std::function<bool(const std::string&)> callback);

    ErrorCode_def RegisterReceiveUpdateConfig(const ReceiveUpdateConfig &receive)
    {
        std::lock_guard<std::mutex> lck(update_mtx_);
        receive_update_config_callback_ = receive;
        return ErrorCode::kSuccess;
    }

    ErrorCode_def UnRegisterReceiveUpdateConfig()
    {
        std::lock_guard<std::mutex> lck(update_mtx_);
        receive_update_config_callback_ = nullptr;
        return ErrorCode::kSuccess;
    }

private:
    void Run(const ErrorCallback& error_callback);

    virtual void Receive(IMonitorSinker::Type type,
                         uint64_t query_key,
                         const ptree& content);

    void GetAll(ResponsePtr response, RequestPtr request);

    void GetIndicators(ResponsePtr response, RequestPtr request);

    void GetEvents(ResponsePtr response, RequestPtr request);

    void GetEventsPaged(ResponsePtr response, RequestPtr request);

    void OutputIndicators(ResponseBuilder *resp_builder);

    void OutputEvents(ResponseBuilder *resp_builder);

    void OutputEventsPaged(ResponseBuilder *resp_builder, RequestPtr request);

    void ProcessGetRequest(std::function<std::string(const std::string&)> callback, ResponsePtr response, RequestPtr request);

    void ProcessPutRequest(std::function<bool(const std::string&)> callback, ResponsePtr response, RequestPtr request);

    void UpdateConfig(ResponsePtr response, RequestPtr request);

    std::map<std::string, ptree>    indicators_;
    std::deque<std::string>         events_;
    std::mutex                      indicators_mtx_;
    std::mutex                      events_mtx_;
    std::mutex                      update_mtx_;

    HttpServer                      server_;
    std::thread                     thread_;
    ErrorCallback                   error_callback_;
    bool                            is_running_;

    ReceiveUpdateConfig             receive_update_config_callback_;
};

} // namespace monitor
} // namespace adk_impl

#endif /* ADK_MONITOR_HTTP_AGENT_H_ */
