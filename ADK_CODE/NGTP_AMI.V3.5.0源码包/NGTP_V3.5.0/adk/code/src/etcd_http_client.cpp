#include <boost/format.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include "iostream"

#include <adk/property.h>
#include <adk/error_code.h>
#include <adk/arch/generic.h>
#include <adk/etcd_http_client.h>
#include <adk/http_request.h>

typedef adk_impl::http::Request HttpClient;

namespace adk_impl
{
const std::int32_t kHttpStatusCodeOK(200);
const std::string kHttpRequestGET("GET");
const std::string kHttpRequestPOST("POST");
const std::string kHttpRequestPUT("PUT");
const std::string kHttpRequestDELETE("DELETE");
constexpr int32_t kHttpTimeOut = -1;   // -1表示无超时，阻塞

const std::string base64_chars =
"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
"abcdefghijklmnopqrstuvwxyz"
"0123456789+/";

EtcdHttpClient::EtcdHttpClient(const std::string& service_location,
                                const std::string& domain)
: current_member_(0),
    domain_(domain)
{
    std::vector<std::string> tmp;
    boost::algorithm::split(tmp,
                            service_location,
                            boost::algorithm::is_any_of(", "));

    std::copy_if(tmp.begin(), tmp.end(), std::back_inserter(member_locations_), [](std::string& addr) {
        return !addr.empty();
    });
}

int EtcdHttpClient::GetValue(const std::string& key,
                            std::string* value,
                            VersionNo* version,
                            int32_t tried_time)
{
    /// 无法更新etcd_version, 直接返回失败.
    if (GetEtcdVersion() != ErrorCode::kSuccess)
    {
        return ErrorCode::kFailure;
    }

    std::string str_key = CompleteKey(key);
    std::string encode_key = Base64Encode(str_key.c_str(), str_key.length());

    std::string key_url = "/kv/range";
    std::string req_json = (boost::format(R"({"key":"%1%"})") % encode_key).str();
    std::string req_type = kHttpRequestPOST;
    adk_impl::Property props;

    /// http请求成功
    if (DoHttpRequest(key_url, req_type, req_json, props) == ErrorCode::kSuccess)
    {
        std::vector<adk_impl::Property> vec_props;
        vec_props = props.GetValue("kvs", std::vector<adk_impl::Property>());

        if (!vec_props.empty())
        {
            // 这里采用[0] 的原因是etcd 返回的json只有一个条目(因为此时为指定key而非前缀, 所以统一用列表返回, 但实则只有一个值),
            if (value)
            {
                *value = vec_props[0].GetValue("value", "");
                *value = Base64Decode(*value);
            }

            if (version)
            {
                *version = vec_props[0].GetValue("version", 0);
            }
        }
        else
        {
            return ErrorCode::kKeyNotExist;
        }

        return ErrorCode::kSuccess;
    }

    return ErrorCode::kFailure;
}

int EtcdHttpClient::PutValue(const std::string& key,
                            const std::string& value,
                            int32_t tried_time)
{
    /// 无法更新etcd_version, 直接返回失败.
    if (GetEtcdVersion() != ErrorCode::kSuccess)
    {
        return ErrorCode::kFailure;
    }

    std::string str_key = CompleteKey(key);
    std::string encode_key = Base64Encode(str_key.c_str(), str_key.length());
    std::string encode_value = Base64Encode(value.c_str(), value.length());

    std::string key_url = "/kv/put";
    std::string req_json = (boost::format(R"({"key":"%1%","value":"%2%"})") % encode_key % encode_value).str();
    std::string req_type = kHttpRequestPOST;
    adk_impl::Property props;

    if (DoHttpRequest(key_url, req_type, req_json, props) == ErrorCode::kSuccess)
    {
        return ErrorCode::kSuccess;
    }

    return ErrorCode::kFailure;
}

int EtcdHttpClient::CheckAndPutValues(const CheckList& check_versions,
                                    const ValueList& put_values,
                                    int32_t tried_time)
{
    /// 无法更新etcd_version, 直接返回失败.
    if (GetEtcdVersion() != ErrorCode::kSuccess)
    {
        return ErrorCode::kFailure;
    }

    std::string cmp_fmt = R"({"key":"%1%","version":"%2%","result":"EQUAL","target":"VERSION"})";
    std::string suc_fmt = R"({"requestPut":{"key":"%1%","value":"%2%"}})";

    /// 组装请求json, 格式如下:
    /// {"compare":[{"version":"7","result":"EQUAL","target":"VERSION","key":"QGRlZmF1bHQvQ29udGV4dC9MQURlZmF1bHQ="}],
    ///  "success":[{"requestPut":{"key":"QGRlZmF1bHQvQ29udGV4dC9MQURlZmF1bHQ=","value":"QGRlZmF1bHQvQ29udGV4dC9MQURlZmF1bHQ="}}]}
    std::string req_json;
    {

        adk_impl::Property req_props;
        std::vector<adk_impl::Property> cmp_props_vec, suc_props_vec;
        for (const auto& iter : check_versions)
        {
            std::string domain_key = CompleteKey(iter.first);
            std::string cmp_str = (boost::format(cmp_fmt) % Base64Encode(domain_key.c_str(), domain_key.length()) % iter.second ).str();
            cmp_props_vec.push_back(adk_impl::Property(cmp_str));
        }

        for (const auto& iter : put_values)
        {
            std::string domain_key = CompleteKey(iter.first);
            std::string value = iter.second;
            std::string suc_str = (boost::format(suc_fmt) % Base64Encode(domain_key.c_str(), domain_key.length()) % Base64Encode(value.c_str(), value.length()) ).str();
            suc_props_vec.push_back(adk_impl::Property(suc_str));
        }

        req_props.SetValue("compare", cmp_props_vec);
        req_props.SetValue("success", suc_props_vec);

        req_json = req_props.Dump();
    }

    std::string key_url = "/kv/txn";
    std::string req_type = kHttpRequestPOST;
    adk_impl::Property props;

    /// http请求成功
    if (DoHttpRequest(key_url, req_type, req_json, props) == ErrorCode::kSuccess)
    {
        bool successed = props.GetValue("succeeded", false);

        if (successed)
        {
            return ErrorCode::kSuccess;
        }
        else
        {
            return ErrorCode::kTryAgain;
        }
    }

    return ErrorCode::kFailure;
}

int32_t EtcdHttpClient::GetEtcdVersion(bool force_refresh)
{
    if (member_locations_.empty())
    {
        return ErrorCode::kFailure;
    }
    /// 非强制获取时, 且上一次已经获取了version, 则此时不再进行获取. (避免对同一个etcd节点重复获取version)
    if (!force_refresh && !current_version_.empty())
    {
        return ErrorCode::kSuccess;
    }

    uint32_t tried_times = 0;
    do
    {
        try
        {
            HttpClient http_client(member_locations_[current_member_] + "/version");

            HttpClient::Response resp = http_client.send(kHttpRequestGET, "", {"Content-Type: json"});

            if (resp.status == kHttpStatusCodeOK)
            {
                adk_impl::Property props = adk_impl::Property(std::string{resp.body.begin(), resp.body.end()});
                current_version_ = props.GetValue("etcdserver", "");

                // 根据version确定使用 v3alpha/v3beta/v3 中的一个
                if (current_version_ < "3.3") url_v3_key_ = "v3alpha";
                else if (current_version_ < "3.5") url_v3_key_ = "v3beta";
                else url_v3_key_ = "v3";

                return ErrorCode::kSuccess;
            }
        }
        catch (...)
        {
        }

        /// 该etcd请求不成功, 需要换下一个节点.
        ++tried_times;
        current_member_ = (current_member_ + 1) % member_locations_.size();

    } while (tried_times < member_locations_.size());

    /// have tried all etcd address
    return ErrorCode::kFailure;
}

int32_t EtcdHttpClient::DoHttpRequest(const std::string& key_url, const std::string& req_type, const std::string& req_json, adk_impl::Property& props)
{
    try
    {
        uint32_t tried_times = 0;

        do
        {
            HttpClient http_client(member_locations_[current_member_] + "/" + url_v3_key_ + key_url);
            HttpClient::Response resp = http_client.send(req_type, req_json, {"Content-Type: json"});

            if (resp.status == kHttpStatusCodeOK)
            {
                props = adk_impl::Property(std::string{resp.body.begin(), resp.body.end()});

                return ErrorCode::kSuccess;
            }
            else
            {
                /// 请求失败则尝试切换下一个节点.
                /// 若所有节点都无法获取version. 则返回失败
                if (GetEtcdVersion(true) != ErrorCode::kSuccess)
                {
                    return ErrorCode::kFailure;
                }

                /// 防止一些意料之外的情况造成的死循环(比如version的http成功, 但其他请求的http不成功)
                if (ADK_UNLIKELY(++tried_times >= member_locations_.size()))
                {
                    return ErrorCode::kFailure;
                }
            }
        } while (1);
    }
    catch (...)
    {
        return ErrorCode::kFailure;
    }

    return ErrorCode::kFailure;
}

std::string EtcdHttpClient::Base64Encode(const char * bytes_to_encode, unsigned int in_len)
{
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (in_len--)
    {
        char_array_3[i++] = *(bytes_to_encode++);
        if(i == 3)
        {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for(i = 0; (i <4) ; i++)
            {
                ret += base64_chars[char_array_4[i]];
            }
            i = 0;
        }
    }
    if(i)
    {
        for(j = i; j < 3; j++)
        {
            char_array_3[j] = '\0';
        }

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for(j = 0; (j < i + 1); j++)
        {
            ret += base64_chars[char_array_4[j]];
        }

        while((i++ < 3))
        {
            ret += '=';
        }

    }
    return ret;
}

std::string EtcdHttpClient::Base64Decode(std::string const & encoded_string)
{
    int in_len = (int) encoded_string.size();
    int i = 0;
    int j = 0;
    int in_ = 0;
    unsigned char char_array_4[4] = {0}, char_array_3[3] = {0};
    std::string ret;

    while (in_len-- && ( encoded_string[in_] != '=') && is_base64(encoded_string[in_]))
    {
        char_array_4[i++] = encoded_string[in_]; in_++;
        if (i == 4)
        {
            for (i = 0; i < 4; i++)
            {
                char_array_4[i] = base64_chars.find(char_array_4[i]);
            }

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; (i < 3); i++)
            {
                ret += char_array_3[i];
            }
            i = 0;
        }
    }

    if (i)
    {
        for (j = i; j <4; j++)
        {
            char_array_4[j] = 0;
        }

        for (j = 0; j < 4; j++)
        {
            char_array_4[j] = base64_chars.find(char_array_4[j]);
        }

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (j = 0; (j < i - 1); j++)
        {
            ret += char_array_3[j];
        }
    }

    return ret;
}

}
