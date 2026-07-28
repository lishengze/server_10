/**
 * @file http_util.cpp
 * @brief Http工具函数
 * @author Li Yunchong
 * @version 0.1
 * @date 2017-03-21
 */
#include <ctype.h>

#include <vector>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <adk/http_util.h>

namespace adk_impl
{

namespace http
{

std::string HtmlEncode(const std::string& str)
{
    std::string result(str);
    boost::algorithm::replace_all(result, "&", "&amp;");
    boost::algorithm::replace_all(result, "<", "&lt;");
    boost::algorithm::replace_all(result, ">", "&gt;");
    return result;
}

std::string JsEncode(const std::string& str, char quot)
{
    std::string result(str);
    boost::algorithm::replace_all(result, "\\", "\\\\");
    boost::algorithm::replace_all(result, "\n", "\\n");
    boost::algorithm::replace_all(result, "\"", ('\"' == quot) ? "&quot;" : "\\\"");
    boost::algorithm::replace_all(result, "\'", ('\'' == quot) ? "&apos;" : "\\\'");
    return result;
}

unsigned char ToHex(unsigned char x)
{
    return  x > 9 ? x + 55 : x + 48;
}

unsigned char FromHex(unsigned char x)
{
    unsigned char y;
    if (x >= 'A' && x <= 'Z') y = x - 'A' + 10;
    else if (x >= 'a' && x <= 'z') y = x - 'a' + 10;
    else if (x >= '0' && x <= '9') y = x - '0';
    else { y = '0'; assert(0); }
    return y;
}

std::string UrlEncode(const std::string& str)
{
    std::string strTemp = "";
    size_t length = str.length();
    for (size_t i = 0; i < length; i++)
    {
        if (isalnum((unsigned char)str[i]) ||
            (str[i] == '-') ||
            (str[i] == '_') ||
            (str[i] == '.') ||
            (str[i] == '~'))
            strTemp += str[i];
        else if (str[i] == ' ')
            strTemp += "+";
        else
        {
            strTemp += '%';
            strTemp += ToHex((unsigned char)str[i] >> 4);
            strTemp += ToHex((unsigned char)str[i] % 16);
        }
    }
    return strTemp;
}

std::string UrlDecode(const std::string& str)
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

std::map<std::string, std::string> ParsePostData(const std::string& data)
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

} // namespace http
} // namespace adk_impl
