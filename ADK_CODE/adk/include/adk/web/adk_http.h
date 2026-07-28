#ifndef ADK_IMPL_HTTP_H_
#define ADK_IMPL_HTTP_H_

#include <string>
#include <map>
#include <unordered_map>
#include <memory>
#include <thread>
#include <functional>
#include <fstream>

#include <boost/regex.hpp>
#include <boost/functional/hash.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/core/noncopyable.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/filesystem.hpp>

namespace adk_impl
{

namespace http
{
template <class socket_type> class Server;
template <class socket_type> class Client;
}

namespace web
{
 
// http 状态码
namespace http_status_code 
{
enum value
{
    uninitialized = 0,

    continue_code = 100,
    switching_protocols = 101,

    ok = 200,
    created = 201,
    accepted = 202,
    non_authoritative_information = 203,
    no_content = 204,
    reset_content = 205,
    partial_content = 206,

    multiple_choices = 300,
    moved_permanently = 301,
    found = 302,
    see_other = 303,
    not_modified = 304,
    use_proxy = 305,
    temporary_redirect = 307,

    bad_request = 400,
    unauthorized = 401,
    payment_required = 402,
    forbidden = 403,
    not_found = 404,
    method_not_allowed = 405,
    not_acceptable = 406,
    proxy_authentication_required = 407,
    request_timeout = 408,
    conflict = 409,
    gone = 410,
    length_required = 411,
    precondition_failed = 412,
    request_entity_too_large = 413,
    request_uri_too_long = 414,
    unsupported_media_type = 415,
    request_range_not_satisfiable = 416,
    expectation_failed = 417,
    im_a_teapot = 418,
    upgrade_required = 426,
    precondition_required = 428,
    too_many_requests = 429,
    request_header_fields_too_large = 431,

    internal_server_error = 500,
    not_implemented = 501,
    bad_gateway = 502,
    service_unavailable = 503,
    gateway_timeout = 504,
    http_version_not_supported = 505,
    not_extended = 510,
    network_authentication_required = 511
}; // enum value

inline std::string get_string(value c)
{
    switch (c)
    {
    case uninitialized:
        return "Uninitialized";
    case continue_code:
        return "Continue";
    case switching_protocols:
        return "Switching Protocols";
    case ok:
        return "OK";
    case created:
        return "Created";
    case accepted:
        return "Accepted";
    case non_authoritative_information:
        return "Non Authoritative Information";
    case no_content:
        return "No Content";
    case reset_content:
        return "Reset Content";
    case partial_content:
        return "Partial Content";
    case multiple_choices:
        return "Multiple Choices";
    case moved_permanently:
        return "Moved Permanently";
    case found:
        return "Found";
    case see_other:
        return "See Other";
    case not_modified:
        return "Not Modified";
    case use_proxy:
        return "Use Proxy";
    case temporary_redirect:
        return "Temporary Redirect";
    case bad_request:
        return "Bad Request";
    case unauthorized:
        return "Unauthorized";
    case payment_required:
        return "Payment Required";
    case forbidden:
        return "Forbidden";
    case not_found:
        return "Not Found";
    case method_not_allowed:
        return "Method Not Allowed";
    case not_acceptable:
        return "Not Acceptable";
    case proxy_authentication_required:
        return "Proxy Authentication Required";
    case request_timeout:
        return "Request Timeout";
    case conflict:
        return "Conflict";
    case gone:
        return "Gone";
    case length_required:
        return "Length Required";
    case precondition_failed:
        return "Precondition Failed";
    case request_entity_too_large:
        return "Request Entity Too Large";
    case request_uri_too_long:
        return "Request-URI Too Long";
    case unsupported_media_type:
        return "Unsupported Media Type";
    case request_range_not_satisfiable:
        return "Requested Range Not Satisfiable";
    case expectation_failed:
        return "Expectation Failed";
    case im_a_teapot:
        return "I'm a teapot";
    case upgrade_required:
        return "Upgrade Required";
    case precondition_required:
        return "Precondition Required";
    case too_many_requests:
        return "Too Many Requests";
    case request_header_fields_too_large:
        return "Request Header Fields Too Large";
    case internal_server_error:
        return "Internal Server Error";
    case not_implemented:
        return "Not Implemented";
    case bad_gateway:
        return "Bad Gateway";
    case service_unavailable:
        return "Service Unavailable";
    case gateway_timeout:
        return "Gateway Timeout";
    case http_version_not_supported:
        return "HTTP Version Not Supported";
    case not_extended:
        return "Not Extended";
    case network_authentication_required:
        return "Network Authentication Required";
    default:
        return "Unknown";
    } // end switch
}

} // namespace status_code

class CaseInsensitiveEquals 
{
public:
    bool operator()(const std::string &key1, const std::string &key2) const 
    {
        return boost::algorithm::iequals(key1, key2);
    }
};

class CaseInsensitiveHash 
{
public:
    size_t operator()(const std::string &key) const 
    {
        std::size_t seed=  0;
        for(auto &c: key)
        {
            boost::hash_combine(seed, std::tolower(c));
        }

        return seed;
    }
};

using HttpHeader = std::unordered_multimap<std::string, std::string, CaseInsensitiveHash, CaseInsensitiveEquals>;

class HttpServer : private boost::noncopyable
{
public:
    // http 请求
    struct Request
    {
        ~Request() = default;

        unsigned short remote_endpoint_port; // client port

        std::string remote_endpoint_address; // client addr

        std::string method; // http request method

        std::string path; // http request path

        std::string http_version; // http verion

        std::string http_body; // http request body

        boost::smatch path_match; // http request path match

        HttpHeader header; // Request http header 

    private:
        Request() = default;

        friend class HttpServer;
    };

    // http 响应
    struct Response
    {
        ~Response() = default;

        // If true, force server to close the connection after the response have been sent.
        // This is useful when implementing a HTTP/1.0-server sending content
        // without specifying the content length.
        bool close_connection_after_response = false;

        http_status_code::value status_code = http_status_code::ok; // http  状态码

        std::string http_body; // http response body

        std::map<std::string, std::string> header; // http response 头部字段

    private:
        Response(const std::map<std::string, std::string> &h) : header(h){}

        friend class HttpServer;
    };

    using RequestPtr = std::shared_ptr<Request>;
    using ResponsePtr = std::shared_ptr<Response>;
    using Handler = std::function<void(RequestPtr, ResponsePtr)>; // 处理 http 请求 hander

    // http server 配置相关
    struct Config
    {
        // http 服务端口
        unsigned short port = 80;

        // http 服务 线程数量
        std::size_t thread_pool_size = 1;

        /// Timeout on request handling. Defaults to 5 seconds.
        std::size_t timeout_request = 5;

        /// Timeout on content handling. Defaults to 300 seconds.
        std::size_t timeout_content = 300;

        // ipv4 或者 ipv6 地址
        std::string address;

        /// web 服务静态文件根目录，为空则不使用页面
        std::string web_root;

        /// 端口复用
        bool reuse_address = true;
    };

    // http server 配置
    // 应用可用过此对象设置参数
    Config config;

    HttpServer();

    ~HttpServer();

    /**
     * @breaf       开始 http 服务，
     * @param[in]   
     *
     * @note       此函数会返回，如果失败则抛出异常
    */
    void Start();

    /**
     * @breaf       停止 http 服务，该函数会阻塞等待所有后台线程退出
     * @param[in]   
     *
     * @note
    */
    void Stop();

    /**
     * @breaf               注册处理 http 请求回调 
     * @param[in] url       处理的 http 请求 url
     * @param[in] method    处理的 http 请求方法
     * @param[in] http_hd   回调函数
     *
     * @note
    */
    void RegisterHandler(const std::string &url,
                         const std::string &method,
                         Handler http_hd);

    /**
     * @breaf                           注册处理获取静态文件的 url，由 HttpServer 负责文件传输
     * @param[in] static_file_url       前端获取静态文件的 url
     *
     * @note                            如果对外提供 http 页面，需要设置 config 中 web_root字段
    */
    void RegisterHandlerStaticFileUrl(const std::string &static_file_url);

    /**
     * @breaf              设置默认响应的 http 响应头部字段，比如 "Content-Type", "application/json;charset=utf-8"
     * @param[in] key      http 头部字段 key
     * @param[in] value    对应的字段 value
     *
     * @note               可以多次调用，应在启动 http server 之前调用，启动 http server 之后不应该调用该函数
    */
    void SetDefaultResponeHeader(const std::string &key, const std::string &value);

    /**
     * @breaf                       某些场景下需要给 http 客户端发送文件
     * @param[in] respone           http 响应
     * @param[in] file_data         文件内容
     * @param[in] resp_file_name    返回给 http 客户端的文件名称
     *
     * @note                        注意发送的文件内容不是 http 相关的静态资源，只是将返回给 客户端内容包装为文件，
     *                              浏览器客户端可下载该文件
    */
    static void WriteResponseFile(ResponsePtr respone,
                                  const std::string &file_data,
                                  const std::string &resp_file_name = "file");

    // 同上
    static void WriteResponseFile(ResponsePtr respone,
                                  std::string &&file_data,
                                  const std::string &resp_file_name = "file");

    /**
     * @brief       将 "key1=value1&key2=value2&key3=value3"字符串转换为key/value映射表
     *
     * @param data  输入数据
     *
     * @return 数据的key/value map
     */
    static std::map<std::string, std::string> ParseURLData(const std::string& data);

private:
    class regex_orderable : public boost::regex
    {
        std::string str;
    public:
        regex_orderable(const char *regex_cstr) : boost::regex(regex_cstr), str(regex_cstr) {}
        regex_orderable(const std::string &regex_str) : boost::regex(regex_str), str(regex_str) {}
        bool operator<(const regex_orderable &rhs) const 
        {
            return str < rhs.str;
        }
    };

    void OnHttp(void *req_ptr, void *resp_ptr);

    void FindResource(RequestPtr request, ResponsePtr response);

    void GetStaticFile(const bool is_index, RequestPtr request, ResponsePtr response);

    void SendFileContent(const ResponsePtr &response, const std::shared_ptr<std::ifstream> &ifs);

    using RWMutex = boost::shared_mutex;
    using ReadLock = boost::shared_lock<RWMutex>;
    using WriteLock = boost::unique_lock<RWMutex>;

    RWMutex resource_mtx_;
    std::map<regex_orderable, std::map<std::string, Handler>> resource_;
    std::unique_ptr<http::Server<boost::asio::ip::tcp::socket>> http_server_ptr_;
    std::thread http_server_thr_;
    std::map<std::string, std::string> default_resp_header_;
    boost::filesystem::path web_root_;
};

class HttpClient : private boost::noncopyable
{
public:
    struct Response
    {            
        std::string http_version; // 服务器使用的 http 版本

        std::string status_code; // http 响应状态码

        HttpHeader header; // http 响应 头部

        std::string http_body; // http response body
    };

    using ResponsePtr = std::shared_ptr<Response>; // shared_ptr

    struct Config 
    {
        // 请求超时时间，0 表示不会超时
        size_t timeout = 0;

        // 连接超时时间，如果为 0，则使用 timeout 设置的值
        size_t timeout_connect = 0;

        // 代理服务器
        std::string proxy_server;
    };
    
    // 配置
    Config config;

     /**
     * @brief               构造函数
     *
     * @param server_addr   http server 地址，比如 "127.0.0.1:80"
     *
     */
    HttpClient(const std::string &server_addr);

    /**
     * @brief                发送 http 请求
     *
     * @param request_type      http 请求类型，比如 "GET"、"POST"、"PUT" 等
     * @param path              http url，不需要填充地址，比如 "/"、"/test/your_path"
     * @param request_body      http 请求 body，可为空
     * @param header            http 请求 头部字段
     *
     */
    ResponsePtr Request(const std::string &request_type,
                        const std::string &path = "/",
                        const std::string &request_body = "",
                        const std::map<std::string, std::string> &header = std::map<std::string, std::string>());

    /**
     * @brief                发送 http 请求
     *
     * @param request_type      http 请求类型，比如 "GET"、"POST"、"PUT" 等
     * @param path              http url，不需要填充地址，比如 "/"、"/test/your_path"
     * @param request_body      http 请求 body，可为空
     * @param header            http 请求 头部字段
     *
     */
    ResponsePtr Request(const std::string &request_type,
                        const std::string &path,
                        std::iostream &request_body,
                        const std::map<std::string, std::string> &header = std::map<std::string, std::string>());

    /**
     * @brief   主动调用 close 接口，关闭底层tcp链接，下次 Request 请求时会重新建立到 http server的链接
     *
     *@note     注意某些情况下，如果连续调用 Request 接口而没设置 keepalive 头部字段
     *          或者服务一个连接只接受一次请求，则客户端的第二次调用 Request 则会失败，因为服务器已关闭该链接
     */
    void Close();
    
private:
    std::shared_ptr<http::Client<boost::asio::ip::tcp::socket>> http_client_ptr_;
};

} // namespace web
} // namespace adk_impl

#endif