#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

#include <atomic>
#include <chrono>

#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>

#include <adk/http_server.h>
#include <adk/http_client.h>
#include <adk/entry_wrapper.h>
#include <adk/web/adk_http.h>

namespace adk_impl
{
namespace web
{

using ADK_IMPL_HttpRequest = http::Server<http::HTTP>::Request;
using ADK_IMPL_HttpResponse = http::Server<http::HTTP>::Response;
using ADK_IMPL_HttpRequestPtr = std::shared_ptr<http::Server<http::HTTP>::Request>;
using ADK_IMPL_HttpResponsePtr = std::shared_ptr<http::Server<http::HTTP>::Response>;

HttpServer::HttpServer()
{    
    http_server_ptr_.reset(new http::Server<http::HTTP>);
}

HttpServer::~HttpServer()
{
    Stop();
}

void HttpServer::Start()
{
    // http server 配置
    http_server_ptr_->config.address = config.address;
    http_server_ptr_->config.port = config.port;
    http_server_ptr_->config.thread_pool_size = config.thread_pool_size;
    http_server_ptr_->config.reuse_address = config.reuse_address;
    http_server_ptr_->config.timeout_content = config.timeout_content;
    http_server_ptr_->config.timeout_request = config.timeout_request;

    // 转发 http 请求处理 handler 
    auto http_handler = [this](ADK_IMPL_HttpResponsePtr resp, ADK_IMPL_HttpRequestPtr req)
    {
        this->OnHttp(req.get(), resp.get());
    };

    // 注册 http 请求处理的 handler
    http_server_ptr_->default_resource["GET"] = http_handler;
    http_server_ptr_->default_resource["POST"] = http_handler;
    http_server_ptr_->default_resource["PUT"] = http_handler;
    http_server_ptr_->default_resource["DELETE"] = http_handler;
    http_server_ptr_->default_resource["PATCH"] = http_handler;

    // 如果提供 web 静态文件目录
    if (!config.web_root.empty())
    {
        if (!boost::filesystem::exists(config.web_root))
        {
            throw std::invalid_argument("the web root directory don't exist");
        }
        web_root_ = boost::filesystem::canonical(config.web_root);

        // 注册处理静态文件的 handler 
        resource_["^/$"]["GET"] = std::bind(&HttpServer::GetStaticFile,
                                            this,
                                            true,
                                            std::placeholders::_1, 
                                            std::placeholders::_2);
    }

    // 使用单独线程启动 http server
    // 因为 http server 启动失败时会抛出异常
    // 因此需要在外部检查使用启动失败，如果失败则将异常抛出
    auto is_ready_ptr = std::make_shared<std::atomic_bool>(true);
    auto exception_ptr = std::make_shared<std::exception>();
    http_server_thr_ = std_thread("", "", [is_ready_ptr, exception_ptr, this]()
    {
        try
        {
            http_server_ptr_->start();
        }
        catch (const std::exception &err)
        {
            *exception_ptr = err;
            is_ready_ptr->store(false);
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (is_ready_ptr->load() != true)
    {
        throw *exception_ptr;
    }
}

// 停止 http server
// join 后台线程
void HttpServer::Stop()
{
    if (http_server_ptr_ != nullptr)
    {
        http_server_ptr_->stop();
    }

    if (http_server_thr_.joinable())
    {
        http_server_thr_.join();
    }

    http_server_ptr_ = nullptr;
}

// 预处理 http 请求
void HttpServer::OnHttp(void *req_ptr, void *resp_ptr)
{
    auto adk_impl_req = static_cast<ADK_IMPL_HttpRequest*>(req_ptr);
    auto adk_impl_resp = static_cast<ADK_IMPL_HttpResponse*>(resp_ptr);

    // 构建 http 请求描述
    RequestPtr request_ptr(new Request);
    request_ptr->remote_endpoint_port = adk_impl_req->remote_endpoint_port;
    request_ptr->remote_endpoint_address = std::move(adk_impl_req->remote_endpoint_address);
    request_ptr->method = std::move(adk_impl_req->method);
    request_ptr->path = std::move(adk_impl_req->path);
    request_ptr->http_version = std::move(adk_impl_req->http_version);
    request_ptr->http_body = std::string((std::istream_iterator<char>(adk_impl_req->content)), std::istream_iterator<char>());
    // 拷贝头部字段
    for (auto &item : adk_impl_req->header)
    {
        request_ptr->header.emplace(std::move(item.first), std::move(item.second));
    }
    
    // 以默认的响应头部字段构建响应
    ResponsePtr response_ptr(new Response(default_resp_header_));
    // 根据应用注册的 url 查找对应的应用 hander
    // 并且调用查找到的hander
    FindResource(request_ptr, response_ptr);

    // write respone
    // http 响应报文构建
    adk_impl_resp->close_connection_after_response = response_ptr->close_connection_after_response;
    *adk_impl_resp << "HTTP/1.1 " 
                   << response_ptr->status_code
                   << " "
                   << http_status_code::get_string(response_ptr->status_code)
                   << "\r\n";

    for (const auto field : response_ptr->header)
    {
        if ("Content-Length" != field.first)
        {
            *adk_impl_resp << std::move(field.first) 
                           << ": " 
                           << std::move(field.second)
                           << "\r\n";
        }
    }

    *adk_impl_resp << "Content-Length: "
                   << response_ptr->http_body.size()
                   << "\r\n\r\n"
                   << std::move(response_ptr->http_body);
}

void HttpServer::RegisterHandler(const std::string &url,
                                 const std::string &method,
                                 Handler http_hd)
{
    WriteLock lck(resource_mtx_);
    resource_[url][method] = std::move(http_hd);
}

void HttpServer::RegisterHandlerStaticFileUrl(const std::string &static_file_url)
{
    resource_[static_file_url]["GET"] = std::bind(&HttpServer::GetStaticFile,
                                                  this,
                                                  false,
                                                  std::placeholders::_1, 
                                                  std::placeholders::_2);
}

void HttpServer::SetDefaultResponeHeader(const std::string &key, const std::string &value)
{
    if (key != "Content-Length")
    {
        default_resp_header_[key] = value;
    }
}

void HttpServer::WriteResponseFile(ResponsePtr respone,
                                   const std::string &file_data,
                                   const std::string &resp_file_name)
{
    std::string header_value = "attachment; filename=";
    header_value.append(resp_file_name);
    respone->header["Content-Disposition"] = std::move(header_value);
    respone->http_body = file_data;
}

void HttpServer::WriteResponseFile(ResponsePtr respone,
                                   std::string &&file_data,
                                   const std::string &resp_file_name)
{
    std::string header_value = "attachment; filename=";
    header_value.append(resp_file_name);
    respone->header["Content-Disposition"] = std::move(header_value);
    respone->http_body = std::move(file_data);
}

void HttpServer::FindResource(RequestPtr request, ResponsePtr response)
{
    ReadLock lck(resource_mtx_);
    // 遍历注册的 url hander
    // 使用正则表达式匹配 url
    for(auto &regex_method : resource_) 
    {
        auto it = regex_method.second.find(request->method);
        if(it != regex_method.second.end()) 
        {
            boost::smatch sm_res;
            if(boost::regex_match(request->path, sm_res, regex_method.first))
            {
                request->path_match = std::move(sm_res);
                try
                {
                    it->second(request, response);
                }
                catch(const std::exception &err)
                {
                    response->status_code = http_status_code::internal_server_error;
                    response->http_body= (boost::format(R"({"error": "catch internal exception:%1%"})") % err.what()).str();
                }
                catch (...)
                {
                    response->status_code = http_status_code::internal_server_error;
                }

                return;
            }
        }
    }

    lck.unlock();

    // 如果没有查找到对应 hander
    // 尝试读取 web 目录的静态文件
    if (request->method == "GET") // try get static file
    {
        GetStaticFile(false, request, response);
    }
    else 
    {
        response->status_code = http_status_code::not_found;
    }
}

// 获取静态文件
void HttpServer::GetStaticFile(const bool is_index, RequestPtr request, ResponsePtr response)
{
    try
    {
        if (web_root_.empty())
        {
            throw std::invalid_argument("not web root directory empty");
        }

        // 根据参数决定是否获取 index.html
        std::string path_str = is_index ? "/index.html" : request->path;
        auto path = boost::filesystem::canonical(web_root_ / path_str);

        //Check if path is within web_root_
        if (std::distance(web_root_.begin(), web_root_.end()) > std::distance(path.begin(), path.end())
            || !std::equal(web_root_.begin(), web_root_.end(), path.begin()))
        {
            throw std::invalid_argument("path must be within root path");
        }

        if (!(boost::filesystem::exists(path)
              && boost::filesystem::is_regular_file(path)))
        {
            throw std::invalid_argument("file does not exist");
        }

        // 以二进制方式打开文件流
        auto file_name = path.string();
        auto file = fopen(file_name.c_str(), "rb");
        if (file != nullptr)
        {
            // 获取文件大小信息
            // 读取整个文件到内存，如果静态文件巨大无比比如几十个G，则造成内存占用过多
            // 后续可考虑使用 sendfile 进行优化
            struct stat64 st;
            if (fstat64(fileno(file), &st) != 0)
            {
                fclose(file);
                throw std::invalid_argument("could not read file");
            }

            response->http_body.resize(st.st_size);
            long read_len = 0;
            while (true)
            {
                size_t ret = fread(&response->http_body[read_len],
                                   sizeof(char),
                                   st.st_size - read_len,
                                   file);
                if (ret == 0)
                {
                    break;
                }

                read_len += ret;
            }

            fclose(file);
            if (read_len != st.st_size)
            {
                throw std::invalid_argument("read file failed");
            }
        }
        else 
        {
            throw std::invalid_argument("could not read file");
        }
    }
    catch(const std::exception &err)
    {
        response->status_code = http_status_code::not_found;
        response->http_body = (boost::format(R"({"error": "catch exception:%1%"})") % err.what()).str();
    }
}

// 将 16 进制进行转换成真正的 char
static unsigned char FromHex(unsigned char x)
{
    unsigned char y;
    if (x >= 'A' && x <= 'Z') y = x - 'A' + 10;
    else if (x >= 'a' && x <= 'z') y = x - 'a' + 10;
    else if (x >= '0' && x <= '9') y = x - '0';
    else { y = '0'; assert(0); }
    return y;
}

// 解析 url 中带 % 的参数
static std::string UrlDecode(const std::string& str)
{
    std::string strTemp = "";
    size_t length = str.length();
    for (size_t i = 0; i < length; i++)
    {
        if (str[i] == '+') strTemp += ' ';
        else if (str[i] == '%')
        {
            assert(i + 2 < length);
            unsigned char high = FromHex((unsigned char)str[++i]);
            unsigned char low = FromHex((unsigned char)str[++i]);
            strTemp += high*16 + low;
        }
        else strTemp += str[i];
    }
    return strTemp;
}

// 解析 key1=value1&key2=value2 到 map 中
std::map<std::string, std::string> HttpServer::ParseURLData(const std::string& data)
{
    std::vector<std::string> items;
    boost::algorithm::split(items, data, boost::algorithm::is_any_of("&"));
    std::map<std::string, std::string> result;
    for (auto& item : items)
    {
        auto pos = item.find('=');
        result[UrlDecode(item.substr(0, pos))] = UrlDecode(item.substr(pos+1));
    }

    return result;
}

HttpClient::HttpClient(const std::string &server_addr)
{
    http_client_ptr_.reset(new http::Client<boost::asio::ip::tcp::socket>(server_addr));
}

// 超时返回 nullptr
HttpClient::ResponsePtr HttpClient::Request(const std::string &request_type,
                                            const std::string &path, 
                                            const std::string &request_body, 
                                            const std::map<std::string, std::string> &header)
{
    // 超时时间与代理服务器设置
    http_client_ptr_->config.timeout = config.timeout;
    http_client_ptr_->config.timeout_connect = config.timeout_connect;
    http_client_ptr_->config.proxy_server = config.proxy_server;

    try
    {
        auto resp = http_client_ptr_->request(request_type, path, request_body, header);
        ResponsePtr resp_ptr(new Response);

        // 拷贝 http version 以及状态码
        resp_ptr->http_version = std::move(resp->http_version);
        resp_ptr->status_code = std::move(resp->status_code);

        // 拷贝头部字段
        for (auto &item : resp->header)
        {
            resp_ptr->header.emplace(std::move(item.first), std::move(item.second));
        }
        
        // 拷贝 body
        resp_ptr->http_body = std::string((std::istream_iterator<char>(resp->content)), std::istream_iterator<char>());
        return resp_ptr;
    }
    catch(const std::exception &err)
    {
        fprintf(stderr, "catch exception:%s\n", err.what());
        return nullptr;
    }
}

// 基于 iostream 的重载函数
HttpClient::ResponsePtr HttpClient::Request(const std::string &request_type,
                                            const std::string &path, 
                                            std::iostream &request_body, 
                                            const std::map<std::string, std::string> &header)
{
    http_client_ptr_->config.timeout = config.timeout;
    http_client_ptr_->config.timeout_connect = config.timeout_connect;
    http_client_ptr_->config.proxy_server = config.proxy_server;

    try
    {
        auto resp = http_client_ptr_->request(request_type, path, request_body, header);
        ResponsePtr resp_ptr(new Response);
        resp_ptr->http_version = std::move(resp->http_version);
        resp_ptr->status_code = std::move(resp->status_code);

        for (auto &item : resp->header)
        {
            resp_ptr->header.emplace(std::move(item.first), std::move(item.second));
        }
        
        resp_ptr->http_body = std::string((std::istream_iterator<char>(resp->content)), std::istream_iterator<char>());
        return resp_ptr;
    }
    catch(...)
    {
        return nullptr;
    }
}

void HttpClient::Close()
{
    http_client_ptr_->close();
}

}
}