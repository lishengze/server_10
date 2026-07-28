
#include <adk/http_util.h>
#include <adk_pack/http_util.h>

namespace adk
{

namespace http
{

std::string HtmlEncode(const std::string& str)
{
    return adk_impl::http::HtmlEncode(str);
}

std::string JsEncode(const std::string& str, char quot)
{
    return adk_impl::http::JsEncode(str, quot);
}

std::string UrlEncode(const std::string& str)
{
    return adk_impl::http::UrlEncode(str);
}

std::string UrlDecode(const std::string& str)
{
    return adk_impl::http::UrlDecode(str);
}


std::map<std::string, std::string> ParsePostData(const std::string& data)
{
    return adk_impl::http::ParsePostData(data);
}

} // namespace http
} // namespace adk_impl

