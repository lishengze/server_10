/**
 * @file http_agent.cpp
 * @brief HTTP监控代理
 * @author Li Yunchong
 * @version 0.1
 * @date 2017-05-17
 */

#include <thread>
#include <adk/entry_wrapper.h>
#include <adk/monitor/http_agent.h>
#include <adk/http_util.h>
#include <adk/arch/generic.h>
#include <adk/json/json.hpp>

namespace ph = std::placeholders;

namespace adk_impl
{

namespace monitor
{

HttpAgent::HttpAgent()
{
    std::lock_guard<std::mutex> lock(server_.mutex_);
    server_.resource["^/$"]["GET"]
        = std::bind(&HttpAgent::GetAll, this, ph::_1, ph::_2);
    server_.resource["^/indicators/$"]["GET"]
        = std::bind(&HttpAgent::GetIndicators, this, ph::_1, ph::_2);
    server_.resource["^/events/$"]["GET"]
        = std::bind(&HttpAgent::GetEvents, this, ph::_1, ph::_2);
    server_.resource["^/event_paged/\\?(.*)$"]["GET"]
        = std::bind(&HttpAgent::GetEventsPaged, this, ph::_1, ph::_2);
    server_.resource["^/update_config/$"]["PUT"]
        = std::bind(&HttpAgent::UpdateConfig, this, ph::_1, ph::_2);
    is_running_ = false;
}

HttpAgent::~HttpAgent()
{
    if (is_running_)
    {
        is_running_ = false;
        Monitor::PlugoutSinker(this);
        server_.stop();
    }

    // thread_ must be joined
    if(thread_.joinable())
    {
        thread_.join();
    }
}

void HttpAgent::Start(uint16_t port,
                      const std::string& ip,
                      const ErrorCallback& error_callback)
{
    if (is_running_)
        return;

    is_running_ = true;
    Monitor::PluginSinker(this);
    server_.config.port = port;
    server_.config.address = ip;
    thread_ = std_thread("adk-httpagent", "agent thread", std::bind(&HttpAgent::Run, this, error_callback));
}

void HttpAgent::Stop()
{
    if (!is_running_)
        return;

    is_running_ = false;
    Monitor::PlugoutSinker(this);
    server_.stop();
}

void HttpAgent::RegisterGetHttpURL(const std::string& url, std::function<std::string(const std::string&)> callback)
{
    std::lock_guard<std::mutex> lock(server_.mutex_);
    server_.resource["^/app/" + url + "$"]["GET"] = std::bind(&HttpAgent::ProcessGetRequest, this, callback, ph::_1, ph::_2);
}

void HttpAgent::RegisterPutHttpURL(const std::string& url, std::function<bool(const std::string&)> callback)
{
    std::lock_guard<std::mutex> lock(server_.mutex_);
    server_.resource["^/app/" + url + "$"]["PUT"] = std::bind(&HttpAgent::ProcessPutRequest, this, callback, ph::_1, ph::_2);
}

void HttpAgent::ProcessGetRequest(std::function<std::string(const std::string&)> callback, ResponsePtr response, RequestPtr request)
{
    // default http response status code: 200 OK and http response body must be json format
    ResponseBuilder resp_builder(response.get());
    resp_builder.SetHeaderField("Content-Type", "application/json");

    // just pass parameter string to callback
    std::string parameter = http::UrlDecode(request->path_match[1]);

    // Not the content, but parameters from URI
    resp_builder << callback(parameter);
}

void HttpAgent::ProcessPutRequest(std::function<bool(const std::string&)> callback, ResponsePtr response, RequestPtr request)
{
    uint16_t sc = http::status_code::kOK;
    if (!callback(request->content.string()))
    {
        sc = http::status_code::kBadRequest;
    }
    ResponseBuilder resp_builder(response.get(), sc);
}

void HttpAgent::Run(const ErrorCallback& error_callback)
{
    try
    {
        server_.start();
    }
    catch (boost::system::system_error &ec)
    {
        if (error_callback)
        {
            error_callback(ec);
        }
    }
    catch (...)
    {
        std::cout << "Unknown error. " << std::endl;
    }
}

void HttpAgent::GetAll(ResponsePtr response, RequestPtr request)
{
    ResponseBuilder resp_builder(response.get());
//    resp_builder.SetHeaderField("Access-Control-Allow-Origin", "*");
    resp_builder << "{\n\"indicators\":\n";
    OutputIndicators(&resp_builder);

    resp_builder << ",\n\"events\":\n";
    OutputEvents(&resp_builder);
    resp_builder << "\n}";
}

void HttpAgent::GetIndicators(ResponsePtr response, RequestPtr request)
{
    ResponseBuilder resp_builder(response.get());
    OutputIndicators(&resp_builder);
}

void HttpAgent::GetEvents(ResponsePtr response, RequestPtr request)
{
    ResponseBuilder resp_builder(response.get());
    OutputEvents(&resp_builder);
}

void HttpAgent::GetEventsPaged(ResponsePtr response, RequestPtr request)
{
    ResponseBuilder resp_builder(response.get());
    OutputEventsPaged(&resp_builder, request);
}

void HttpAgent::OutputIndicators(ResponseBuilder *resp_builder)
{
    std::lock_guard<std::mutex> lock(indicators_mtx_);
    if (indicators_.empty())
    {
        (*resp_builder) << "[]";
        return;
    }
    std::string new_line = "[\n";
    for (auto indi : indicators_)
    {
        std::ostringstream oss;
        boost::property_tree::json_parser::write_json_helper(
                oss, indi.second, 0, true);
        (*resp_builder) << new_line << oss.str();
        new_line = ",\n";
    }
    (*resp_builder) << "\n]";
}

void HttpAgent::OutputEvents(ResponseBuilder *resp_builder)
{
    std::lock_guard<std::mutex> lock(events_mtx_);
    if (events_.empty())
    {
        (*resp_builder) << "[]";
        return;
    }
    std::string new_line = "[\n";
    for (auto evt : events_)
    {
        (*resp_builder) << new_line << evt;
        new_line = ",\n";
    }
    (*resp_builder) << "\n]";
}

void HttpAgent::OutputEventsPaged(ResponseBuilder *resp_builder, RequestPtr request)
{
    static int sorted = 0;
    std::string url = request->path_match[1];
    std::map<std::string, std::string> parm = adk_impl::http::ParsePostData(url);

    // 如果参数不存在，会返回0
    int64_t page_no = atoi(parm["page_no"].c_str());
    int64_t page_size = atoi(parm["page_size"].c_str());

    // 判断参数
    if (page_size <= 0 || page_no <= 0)
    {
        (*resp_builder) << "[]";
        return;
    }

    // 无event时返回空
    if (events_.empty())
    {
        (*resp_builder) << "[]";
        return;
    }

    // 加锁
    std::lock_guard<std::mutex> lock(events_mtx_);

    // 从小到大排序，最近event会push_back到尾部
    auto compare = [&](const std::string& v1, const std::string& v2) {
        nlohmann::json v1_js = nlohmann::json::parse(v1);
        nlohmann::json v2_js = nlohmann::json::parse(v2);

        // 通过TimeStamp排序
        return (std::strtoull(std::string(v1_js["property"]["TimeStamp"]).c_str(), nullptr, 10)
                    < std::strtoull(std::string(v2_js["property"]["TimeStamp"]).c_str(), nullptr, 10));
    };

    // 判断分页左边界是否大于event大小
    if (((page_no - 1) * page_size) >= (int64_t)events_.size())
    {
        (*resp_builder) << "[]";
        return;
    }

    // 对event按照时间排序
    std::sort(events_.begin() + sorted, events_.end(), compare);
    sorted = events_.size();

    // 返回结果
    (*resp_builder) << "[";
    std::string new_line = "\n";
    for (int i = events_.size() - ((page_no - 1) * page_size) - 1; page_size > 0 && i >= 0; i--, page_size--)
    {
        (*resp_builder) << new_line << events_[i];
        new_line = ",\n";
    }
    (*resp_builder) << "\n]";
}

void HttpAgent::Receive(IMonitorSinker::Type type,
                        uint64_t query_key,
                        const ptree& content)
{
    switch (type)
    {
    case kIndicator:
        {
            std::string class_name = content.get<std::string>("class_name");
            std::lock_guard<std::mutex> lock(indicators_mtx_);
            indicators_[class_name] = content;
        }
        break;
    case kEvent:
        {
            std::ostringstream oss;
            boost::property_tree::json_parser::write_json_helper(oss, content, 0, false);
            std::lock_guard<std::mutex> lock(events_mtx_);
            events_.push_back(oss.str());
        }
        break;
    default:
        ;
    }
}

void HttpAgent::UpdateConfig(ResponsePtr response, RequestPtr request)
{
    ResponseBuilder resp_builder(response.get());
    std::lock_guard<std::mutex> lck(update_mtx_);
    if (receive_update_config_callback_)
    {
        if (receive_update_config_callback_(request->content.string()) != 0)
        {
            resp_builder.set_status_code(http::status_code::kInternalServerError);
        }
    }
}

} // namespace monitor
} // namespace adk_impl
