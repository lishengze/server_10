/**
 * @file etcd_http_client.h
 * @brief ETCD http客户端(通过http访问, 仅提供 getvalue, deletevalue, putvalue, checkandput 等基本操作)
 */
#ifndef ADK_ETCD_HTTP_CLIENT_H_
#define ADK_ETCD_HTTP_CLIENT_H_

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <set>

namespace adk_impl
{

class Property;

class EtcdHttpClient
{
public:
    typedef int64_t VersionNo;
    typedef std::map<std::string, VersionNo> CheckList;
    typedef std::map<std::string, std::string> ValueList;
    typedef std::map<std::string, int64_t> LeaseList;

    struct KeyValue
    {
        std::string key;
        std::string value;
        VersionNo version;
    };

    /**
     * @brief 构造函数
     *
     * @param[in] service_location etcd服务地址，格式：192.168.0.101:2379,192.168.0.102:2379
     * @param[in] domain 命名空间，默认值为空
     */
    explicit EtcdHttpClient(const std::string& service_location,
                            const std::string& domain = std::string());

    /**
     * @brief 析构函数
     */
    ~EtcdHttpClient() = default;

    /**
     * @brief 读取值
     *
     * @param[in]   key         关键字
     * @param[out]  value       值
     * @param[out]  version     值版本号
     * @param[in]   tried_time  已重试次数，内部使用
     *
     * @return 错误代码
     */
    int GetValue(const std::string& key,
                 std::string* value,
                 VersionNo* version = NULL,
                 int32_t tried_time = 0);

    /**
     * @brief 写入值
     *
     * @param[in] key           关键字
     * @param[in] value         值
     * @param[in] tried_time    已重试次数，内部使用
     *
     * @return 错误代码
     */
    int PutValue(const std::string& key,
                 const std::string& value,
                 int32_t tried_time = 0);

    // /**
    //  * @note 暂不提供, 未找到对应的url
    //  * @brief 删除值
    //  *
    //  * @param key           关键字
    //  * @param tried_time    已重试次数，内部使用
    //  *
    //  * @return 错误代码
    //  */
    // int DeleteValue(const std::string& key,
    //                 int32_t tried_time = 0);

    /**
     * @brief 检查版本并写入值
     *
     * @param[in] check_versions    需检查的版本号序列
     * @param[in] put_values        需写入的值序列
     * @param[in] tried_time        已重试次数，内部使用
     *
     * @return 错误代码
     */
    int CheckAndPutValues(const CheckList& check_versions,
                          const ValueList& put_values,
                          int32_t tried_time = 0);

    // 获取 domain
    std::string domain() const
    {
        return domain_;
    }

    // 设置 domain
    void set_domain(const std::string& domain)
    {
        domain_ = domain;
    }

private:
    /**
     * @brief 获取当前etcdserver的版本号, 不同版本号对应的url有所不同
     *        该函数会遍历etcd列表, 以找到一个可用的节点获取版本号. 否则返回失败
     * @param[in] force_refresh: 强制重新获取version信息. (更换etcd节点时将其置为true)
     * @return kSuccess: 成功; kFailure: 失败
     */
    int32_t GetEtcdVersion(bool force_refresh = false);

    /**
     * @brief 进行实际的http请求, 并返回请求结果.
     * @param[in] key_url: 不包含addr的url. 如访问 http://127.0.0.1:4200/v3/kv/txn, 则 key_url = "/kv/txn"
     * @param[in] req_type: http请求类型. GET POST
     * @param[in] req_json: http请求json报文.
     * @param[out] props: 返回的请求结果, 当返回kSuccess时props有效
     * @return kSuccess成功, 其余失败
     */
    int32_t DoHttpRequest(const std::string& key_url, const std::string& req_type, const std::string& req_json, adk_impl::Property& props);

    inline bool is_base64(const char c)
    {
        return (isalnum(c) || (c == '+') || (c == '/'));
    }

    /**
     * @brief base64加密
     *
     * @param[in] bytes_to_encode: 需要加密的字符串
     * @param[in] in_len: 待加密字符串长度
     *
     * @return 返回加密后的字符串
     */
    std::string Base64Encode(const char * bytes_to_encode, unsigned int in_len);

    /**
     * @brief base64解密
     *
     * @param[in] encoded_string: 加密的字符串
     *
     * @return 返回解密后的字符串
     */
    std::string Base64Decode(std::string const & encoded_string);

    std::string CompleteKey(const std::string& key)
    {
        return "@" + domain_ + "/" + key;
    }

private:
    std::vector<std::string> member_locations_;
    int32_t current_member_;
    std::string current_version_;
    std::string url_v3_key_;

    std::string domain_;
};

}
#endif /* ADK_ETCD_HTTP_CLIENT_H_ */
