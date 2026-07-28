/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/
#ifndef ADK_RESPONSE_BUILDER_H_
#define ADK_RESPONSE_BUILDER_H_

#include "http_server.h"

#include <stdint.h>

#include <map>
#include <stack>
#include <string>
#include <sstream>
#include <boost/format.hpp>
#include <boost/assign/list_of.hpp>

namespace adk
{

namespace http
{

/**
 * @brief HTTP状态码
 */
namespace status_code
{

const uint16_t kContinue = 100;
const uint16_t kSwitchingProtocols = 101;
const uint16_t kOK = 200;                   ///< 请求成功
const uint16_t kCreated = 201;
const uint16_t kAccepted = 202;
const uint16_t kNonAuthoritativeInfomation = 203;
const uint16_t kNoContent = 204;
const uint16_t kResetContent = 205;
const uint16_t kPartialContent = 206;
const uint16_t kMultipleChoice = 300;
const uint16_t kMovedPermanently = 301;     ///< 资源被永久转移到其他URL
const uint16_t kFound = 302;
const uint16_t kSeeOther = 303;
const uint16_t kNotModified = 304;
const uint16_t kTemporaryRedirect = 307;
const uint16_t kPermanentRedirect = 308;
const uint16_t kBadRequest = 400;           ///< 错误的客户端请求
const uint16_t kUnauthorized = 401;
const uint16_t kForbidden = 403;
const uint16_t kNotFound = 404;             ///< 请求的资源不存在
const uint16_t kMethodNotAllowed = 405;
const uint16_t kNotAcceptable = 406;
const uint16_t kProxyAuthenticationRequired = 407;
const uint16_t kRequestTimeout = 408;
const uint16_t kConflict = 409;
const uint16_t kGone = 410;
const uint16_t kLengthRequired = 411;
const uint16_t kPrecondictionFailed = 412;
const uint16_t kPayloadTooLarge = 413;
const uint16_t kURITooLong = 414;
const uint16_t kUnsupportedMediaType = 415;
const uint16_t kRequestedRangeNotSatisfiable = 416;
const uint16_t kExpectationFailed = 417;
const uint16_t kUpgradeRequired = 426;
const uint16_t kPreconditionRequired = 428;
const uint16_t kTooManyRequests = 429;
const uint16_t kRequestHeaderFieldsTooLarge = 431;
const uint16_t kUnavailableForLegalReasons = 451;
const uint16_t kInternalServerError = 500;  ///< 内部服务器错误
const uint16_t kNotImplemented = 501;
const uint16_t kBadGateway = 502;
const uint16_t kServiceUnavailable = 503;
const uint16_t kGatewayTimeout = 504;
const uint16_t kHTTPVersionNotSupported = 505;
const uint16_t kNetworkAuthenticationRequired = 511;

/// 常用HTTP状态码到描述信息的映射表
const std::map<uint16_t, std::string> kMessages = boost::assign::map_list_of
    (kContinue, "Continue")
    (kSwitchingProtocols, "Switching Protocols")
    (kOK, "OK")
    (kCreated, "Created")
    (kAccepted, "Accepted")
    (kNonAuthoritativeInfomation, "Non-Authoritative Infomation")
    (kNoContent, "No Content")
    (kResetContent, "Reset Content")
    (kPartialContent, "Partial Content")
    (kMultipleChoice, "Multiple Choice")
    (kMovedPermanently, "Moved Permanently")
    (kFound, "Found")
    (kSeeOther, "See Other")
    (kNotModified, "Not Modified")
    (kTemporaryRedirect, "Temporary Redirect")
    (kPermanentRedirect, "Permanent Redirect")
    (kBadRequest, "Bad Request")
    (kUnauthorized, "Unauthorized")
    (kForbidden, "Forbidden")
    (kNotFound, "Not Found")
    (kMethodNotAllowed, "Method Not Allowed")
    (kNotAcceptable, "Not Acceptable")
    (kProxyAuthenticationRequired, "Proxy Authentication Required")
    (kRequestTimeout, "Request Timeout")
    (kConflict, "Conflict")
    (kGone, "Gone")
    (kLengthRequired, "Length Required")
    (kPrecondictionFailed, "Precondiction Failed")
    (kPayloadTooLarge, "Payload Too Large")
    (kURITooLong, "URI Too Long")
    (kUnsupportedMediaType, "Unsupported Media Type")
    (kRequestedRangeNotSatisfiable, "Requested Range Not Satisfiable")
    (kExpectationFailed, "Expectation Failed")
    (kUpgradeRequired, "Upgrade Required")
    (kPreconditionRequired, "Precondition Required")
    (kTooManyRequests, "Too Many Requests")
    (kRequestHeaderFieldsTooLarge, "Request Header Fields Too Large")
    (kUnavailableForLegalReasons, "Unavailable For Legal Reasons")
    (kInternalServerError, "Internal Server Error")
    (kNotImplemented, "Not Implemented")
    (kBadGateway, "Bad Gateway")
    (kServiceUnavailable, "Service Unavailable")
    (kGatewayTimeout, "Gateway Timeout")
    (kHTTPVersionNotSupported, "HTTP Version Not Supported")
    (kNetworkAuthenticationRequired, "Network Authentication Required");

/**
 * @brief 获取HTTP状态码描述信息
 *
 * @param status HTTP状态码
 *
 * @return 状态码对应的描述信息，非常用状态码将返回空字符串
 */
inline std::string Desc(uint16_t status)
{
    auto it = status_code::kMessages.find(status);
    return (it != status_code::kMessages.end()) ? it->second : std::string();
}

} // namespace status_code

/**
 * @brief HTTP响应构造器
 *
 * @tparam socket_type Socket类型，可以为HTTP或HTTPS
 */
template <typename socket_type>
class ResponseBuilder
{
public:
    /**
     * @brief 构造函数
     *
     * @param response  需要构造的HTTP响应对象指针
     * @param stat_code HTTP响应中的状态码，默认值为200（请求成功）
     */
    explicit ResponseBuilder(typename Server<socket_type>::Response* response,
                             uint16_t stat_code = status_code::kOK);

    /**
     * @brief 析构函数
     */
    ~ResponseBuilder();

    /**
     * @brief 获取HTTP响应中的状态码
     *
     * @return HTTP状态码
     */
    inline uint16_t status_code()
    {
        return status_code_;
    }

    /**
     * @brief 设置HTTP响应中的状态码
     *
     * @param status_code HTTP状态码
     */
    inline void set_status_code(uint16_t status_code)
    {
        status_code_ = status_code;
    }

    /**
     * @brief 设置HTTP响应头信息
     *
     * HTTP响应头中的Content-Length等必要字段已自动设置，此方法用于设置个性化字段
     *
     * @param name 字段名
     * @param value 字段值
     */
    inline void SetHeaderField(const std::string& name, const std::string& value);

    /**
     * @brief 向响应内容中增加包围结构的Context内容
     *
     * Begin和end为一对分别出现在内容前后的Context内容，如“<html>...<body>”和“</body></html>”、
     * “&lt;table ...&gt;”和“&lt;/table&gt;”，此方法会将begin立即追加到响应内容中，
     * 在包围内部的内容输出后，ResponseBuilder会自动将end内容输出。
     *
     * @tparam T1   Begin段内容类型，要求支持<<运算符
     * @tparam T2   End段内容类型，要求支持<<运算符
     * @param begin Begin段内容
     * @param end   End段内容
     */
    template <typename T1, typename T2>
    inline void WrapContext(const T1& begin, const T2& end);

    /**
     * @brief WrapContext方法end为STL字符串类型的特例化实现
     *
     * @tparam T1   Begin段内容类型，要求支持<<运算符
     * @param begin Begin段内容
     * @param end   End段内容
     */
    template <typename T1>
    inline void WrapContext(const T1& begin, const std::string& end);

    /**
     * @brief WrapContext方法end为c语言字符串类型的特例化实现
     *
     * @tparam T1   Begin段内容类型，要求可以作为<<运算符的右操作数
     * @param begin Begin段内容
     * @param end   End段内容
     */
    template <typename T1>
    inline void WrapContext(const T1& begin, const char* end);

    /**
     * @brief 输出一层包围结构的end内容
     *
     * 例如：之前已经使用WrapContext增加了一对“&lt;table ...&gt;”和“&lt;/table&gt;”，
     * 此时表格内容已结束，希望增加一段新的包围式内容，即可使用该方法输出之前压入的“&lt;/table&gt;”，
     * 然后再调用WrapContext增加新内容。
     */
    inline void PopContext();

    /**
     * @brief 追加一段Context内容
     *
     * @tparam T 追加内容类型，要求可以作为<<运算符的右操作数
     * @param t 追加内容
     *
     * @return ResponseBuilder自身
     */
    template <typename T>
    inline ResponseBuilder& operator<<(const T& t);

private:
    typename Server<socket_type>::Response* response_;
    uint16_t                                status_code_;
    std::map<std::string, std::string>      header_fields_;
    std::stringstream                       content_stream_;
    std::stack<std::string>                 ends_;
};

template <typename socket_type>
ResponseBuilder<socket_type>::ResponseBuilder(
        typename Server<socket_type>::Response* response,
        uint16_t stat_code)
    : response_(response),
      status_code_(stat_code)
{
}

template <typename socket_type>
ResponseBuilder<socket_type>::~ResponseBuilder()
{
    if (response_ != NULL)
    {
        *response_ << boost::format("HTTP/1.1 %1% %2%\r\n")
                         % status_code_
                         % status_code::Desc(status_code_);
        for (const auto field : header_fields_)
        {
            *response_ << boost::format("%1%: %2%\r\n") % field.first % field.second;
        }
        while (!ends_.empty())
        {
            content_stream_ << ends_.top();
            ends_.pop();
        }
        *response_ << boost::format("Content-Length: %1%\r\n\r\n%2%")
                         % content_stream_.tellp()
                         % content_stream_.rdbuf();
    }
}

template <typename socket_type>
void ResponseBuilder<socket_type>::SetHeaderField(const std::string& name,
                                                  const std::string& value)
{
    if ("Content-Length" != name)
    {
        header_fields_[name] = value;
    }
}

template <typename socket_type>
template <typename T1, typename T2>
void ResponseBuilder<socket_type>::WrapContext(const T1& begin, const T2& end)
{
    content_stream_ << begin;
    std::stringstream sstream;
    sstream << end;
    ends_.push(sstream.str());
}

template <typename socket_type>
template <typename T1>
void ResponseBuilder<socket_type>::WrapContext(const T1& begin, const std::string& end)
{
    content_stream_ << begin;
    ends_.push(end);
}

template <typename socket_type>
template <typename T1>
void ResponseBuilder<socket_type>::WrapContext(const T1& begin, const char* end)
{
    content_stream_ << begin;
    ends_.push(end);
}

template <typename socket_type>
void ResponseBuilder<socket_type>::PopContext()
{
    if (!ends_.empty())
    {
        content_stream_ << ends_.top();
        ends_.pop();
    }
}

template <typename socket_type>
template <typename T>
ResponseBuilder<socket_type>& ResponseBuilder<socket_type>::operator<<(const T& t)
{
    content_stream_ << t;
    return *this;
}

} // namespace http
} // namespace adk

#endif /* ADK_RESPONSE_BUILDER_H_ */
