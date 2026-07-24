/**
 * @file config_agent.h
 * @brief 配置代理
 * @author Li Yunchong
 * @version 0.1
 * @date 2016-11-20
 */

#ifndef AMI_CONFIG_AGENT_H_
#define AMI_CONFIG_AGENT_H_

#include <stdint.h>
#include <string>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/format.hpp>
#include <boost/function.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <adk/random.h>
#include <ami/config_key.h>
#include <ami/error_code.h>
#include <ami/property.h>
#include <ami/ami_ds_client.h>

namespace ami
{
// #define LOG_DECLARE \
// static const std::string *_module_name; \
// static const int32_t _log_base; \
// static const int32_t _counter_begin = __COUNTER__;

/**
 * @brief 配置代理类
 */
class ConfigAgent
{
    // LOG_DECLARE
public:
    static constexpr bool kSyncRead = true;
    static constexpr bool kCachedRead = false;
    static constexpr bool kLogError = true;
    static constexpr bool kDisableLog = false;

    typedef std::string ConfigPath;

    typedef boost::function<ErrorCode_def (boost::property_tree::ptree*)> PreProcessFunc;
    using LeaseType = int64_t;
    using LeaseInvalidCB = std::function<void(LeaseType)>;

    /**
     * @brief 构造函数
     */
    ConfigAgent();

    bool Init(const Property& property);

    bool Init(const Property& property, const bool is_use_ds_client);

    ErrorCode_def GetBridgeConfig(const std::string& name, Property* property);

    ErrorCode_def GetTxEndpointConfig(const std::string& endpoint_name,
                                      const std::string& tier_name,
                                      std::vector<int32_t> partitions,
                                      Property* property,
                                      bool sync_read = true);

    ErrorCode_def GetRxEndpointConfig(const std::string& endpoint_name,
                                      std::vector<int32_t> partitions,
                                      Property* property,
                                      bool is_queuing_service = false,
                                      bool sync_read = true);
};
}

#endif /* AMI_CONFIG_AGENT_H_ */
