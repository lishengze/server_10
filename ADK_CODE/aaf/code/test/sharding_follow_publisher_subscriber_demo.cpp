#include <assert.h>

#include <iostream>
#include <string>
#include <stdexcept>

#include <boost/property_tree/json_parser.hpp>

#include <aaf.h>

#include <adk/util.h>
#include <adk/pipeline.h>
#include <adk/random.h>

#include "sharding_follow_demo.h"

using namespace aaf;

ADK_LOG_DECLARE_AC(300000);

// 消息转发的主题关系映射
struct StreamEP
{
    std::string from_ep;
    std::string to_ep;
};

class AmiApp : public GenericAmiApplication 
{
public:
    AmiApp()
    {}

    ~AmiApp()
    {}

    // configure program options
    virtual void SetAmiAppOption()
    {
        AAF_ADDOPT_ACCEPTOR_NARG("enable-ha-context", "enable ha-context", enable_ha_);
        AAF_ADDOPT_ACCEPTOR_NARG("enable-sg-context", "enable singleton-context", enable_sg_);
        AAF_ADDOPT_ACCEPTOR_NARG("is-need-send", "", is_need_send_);        // 需要发送消息

        // 消息驱动程序的启动参数
        AAF_ADDOPT_ACCEPTOR("msg-send-total", "send msg total", msg_send_total_, msg_send_total_);    // 消息发送总数
        AAF_ADDOPT_ACCEPTOR("msg-send-rate", "send msg rate", msg_send_rate_, msg_send_rate_);        // 发送速率
        AAF_ADDOPT_ACCEPTOR("forward-msg", "forward msg route; examples: \"A>B,B>C\"", forward_msg_str_, forward_msg_str_);  //消息转发逻辑，如A>B，即从A接收消息发送至B上
        AAF_ADDOPT_ACCEPTOR("core-frequency", "the frequency of core", core_frequency_, core_frequency_);  // core频率

        // 广播消息控制参数
        AAF_ADDOPT_ACCEPTOR("broadcast-endpoints", "the endpoint need to send broadcast messages, example: a;B;c", broadcast_endpoint_str_, broadcast_endpoint_str_);  // 需要发送广播消息的主题名称
        AAF_ADDOPT_ACCEPTOR("is-all-broadcast", "all send msg is broadcast", is_all_broadcast_, is_all_broadcast_);  
    }
    // =======================================================================================

    void OnAmiAppOption(const std::string& option_name)
    {
        if (option_name == "sharding-num")
        {
            sharding_num_ = GetOptionArgument<int32_t>("sharding-num");
            if (sharding_num_ > 0)
            {
                // AGW、ORS进程目前禁止开分片
                ADK_LOG_ERROR_AC_TF("", "publisher and subscriber is forbid to shart sharding");
                StopAmiApp();
                return;
            }
        }
    }

    // ================================configure aaf=======================================

    void OnConfigureFramework(ami::Property& fw_props)
    {
        fw_props.SetValue(config::kEnableHighAvailableContext, enable_ha_);
        fw_props.SetValue(config::kEnableSingletonContext, enable_sg_);
        fw_props.SetValue(config::kEnableAppNameCheck, false);
    }

    void OnRoleChangeToLeader()
    {
    }

    // ================================AAF Init=======================================
    int32_t OnFrameworkInitBegin()
    {
        app_name_ = GetApplicationName();
        return aaf::ErrorCode::kSuccess;
    }

    int32_t OnAmiInitBegin() override
    {
        // 主题消息转发逻辑，用户传入"A>B"，表示A主题转发给B主题
        if (!forward_msg_str_.empty())
        {
            std::vector<std::string> channels;
            boost::split(channels, forward_msg_str_, boost::is_any_of(","), boost::token_compress_on);
            for (const auto& channel : channels)
            {
                std::vector<std::string> from_to;
                boost::split(from_to, channel, boost::is_any_of(">"), boost::token_compress_on);
                if (from_to.size() != 2)
                {
                    ADK_LOG_ERROR_AC_TF("", "forward-msg is error, content: {1}", channel);
                }

                forward_msg_vec_.push_back({from_to[0], from_to[1]});
                ADK_LOG_INFO_AC_TF("paser forward-msg", "from: {1}, to: {2}", from_to[0], from_to[1]);
            }
        }

        if (!broadcast_endpoint_str_.empty())
        {
            boost::split(broadcast_endpoint_name_, 
                         broadcast_endpoint_str_, 
                         boost::is_any_of(",; "), 
                         boost::token_compress_on);
            for (const auto& item : broadcast_endpoint_name_)
            {
                ADK_LOG_INFO_AC_TF("parser broadcast endpoint", "endpoint: {1}", item);
            }
        }
        return aaf::ErrorCode::kSuccess;
    }

    // =================================AAF Run==============================================

    int32_t OnRun() override
    {
        // 目前仅仅AGW组件主动发送消息，其他组件都是被动做转发
        if (is_need_send_)
        {
            adk::SimpleRateController<> rate_ctl(msg_send_rate_);

            uint64_t msg_send_count = 0;
            ADK_LOG_INFO_AC_TF("", "msg_send_total: {1}, msg_send_rate: {2}, msg_send_count: {3}", 
                                    msg_send_total_, msg_send_rate_, msg_send_count);

            for (const auto& ep : endpoint_map_)
            {
                ADK_LOG_INFO_AC_TF("", "ep: {1}", ep.first);
            }

            uint64_t nr_broadcast_msg = 0;
            while (is_running())
            {
                while (msg_send_count < msg_send_total_)
                {
                    // 保证每个主题发送的消息内容sqn从1开始递增
                    ++msg_send_count;
                    for (const auto& endpoint_item : endpoint_map_)
                    {
                        rate_ctl.Wait();

                        ami::Message* ami_msg  = NewMessage(endpoint_item.second, sizeof(AppMessage));
                        AppMessage& msg        = *(AppMessage*)ami_msg->data();
                        msg.sqn                = msg_send_count;
                        msg.is_broadcast_msg   = false;

                        if (broadcast_endpoint_name_.find(endpoint_item.first) != 
                            broadcast_endpoint_name_.end())
                        {
                            if (is_all_broadcast_ || (((msg_send_count - 1) / 4) % 2 == 0))  // 4 为cte的分片数
                            {
                                msg.is_broadcast_msg = true;
                                ++nr_broadcast_msg;
                            }
                        }

                        if (core_frequency_ != 0 && msg_send_count % core_frequency_ == 0)
                        {
                            msg.is_core = true;
                        }
                        else
                        {
                            msg.is_core = false;
                        }

                        std::string content = "Hello sqn: " + std::to_string(msg_send_count);
                        msg.msg_len         = content.length();
                        memcpy(msg.msg, content.c_str(), content.length());

                        memcpy(msg.from_ep_name, endpoint_item.first.c_str(), endpoint_item.first.length());
                        msg.from_ep_name[endpoint_item.first.length()] = '\0';

                        ami_msg->set_size(sizeof(AppMessage));

                        endpoint_item.second->SendMsg(ami_msg);

                        if (msg_send_count % msg_send_rate_ == 0)
                        {
                            ADK_LOG_INFO_AC_TF("################", 
                                               "{1} msg send: {2}", 
                                               endpoint_item.first, 
                                               msg_send_count);
                        }
                    }
                }
            }
        }

        // 等待高可用Context收齐消息
        while (is_running())
        {
            ADK_LOG_INFO_AC_TF("################", "ha recv msg count: {1}", ha_msg_recv_count_);
            sleep(1);
        }

        return aaf::ErrorCode::kPassed;
    }

    void OnMessage(ami::Message* msg) override
    {
        AppMessage* app_msg = (AppMessage*)msg->data();
        std::string endpoint_name = app_msg->from_ep_name;

        auto nr_ep_iter = nr_from_ep_msg_map_.find(endpoint_name);
        if (nr_ep_iter != nr_from_ep_msg_map_.end())
        {
            ++nr_ep_iter->second;
        }
        else
        {
            auto ret = nr_from_ep_msg_map_.insert(std::make_pair(endpoint_name, 1));
            nr_ep_iter = ret.first;
            assert(ret.second == true); // 插入成功
        }

        // 校验消息内容
        uint64_t check_sqn = app_msg->sqn;
        std::string current_msg_content(app_msg->msg, app_msg->msg_len);
        std::string check_msg_content = "Hello sqn: " + std::to_string(check_sqn);
        if (current_msg_content != check_msg_content)
        {
            ADK_LOG_ERROR_AC_TF("message content error",
                                "current msg <{1}>, expect msg <{2}>",
                                current_msg_content,
                                check_msg_content);
            StopAmiApp();
            return;
        }

        ++ha_msg_recv_count_;

        if (!forward_msg_vec_.empty())
        {
            // ADK_LOG_INFO_AC_TF("recv msg", "total: {1}", ha_msg_recv_count_);

            for (const auto from_to_name : forward_msg_vec_)
            {
                if (endpoint_name != from_to_name.from_ep)
                {
                    continue;
                }

                // 根据启动参照，选择Tx进行发送，如AB->BC，即从AB接收消息发送至BC上
                std::string to_name = from_to_name.to_ep;
                auto to_iter = endpoint_map_.find(to_name);
                if (to_iter != endpoint_map_.end())
                {
                    // 更新消息内容：来源哪个主题
                    memcpy(app_msg->from_ep_name, to_name.c_str(), to_name.length());
                    app_msg->from_ep_name[to_name.length()] = '\0';

                    auto to_ep_count = nr_to_ep_msg_map_.find(to_name);
                    if (to_ep_count != nr_to_ep_msg_map_.end())
                    {
                        ++to_ep_count->second;
                    }
                    else
                    {
                        nr_to_ep_msg_map_[to_name] = 1;
                    }

                    to_iter->second->SendMsg(app_msg, sizeof(AppMessage));
                }
            }
        }
    }

    int32_t OnTxEndpointCreation(EndpointHandler* ep_hdl, const std::string& ep_name) override
    {
        ADK_LOG_INFO_AC_TF("", "ep_name: {1}", ep_name);
        std::vector<int32_t> partitions;
        GetTxEndpointPartitions(ep_name, partitions);
        if (partitions.empty())
        {
            abort();
        }
        endpoint_map_.insert(std::make_pair(ep_name, ep_hdl));
        return ErrorCode::kSuccess;
    }

private:
    std::map<std::string, EndpointHandler*> endpoint_map_;
    std::set<std::string> broadcast_endpoint_name_;
    std::vector<StreamEP> forward_msg_vec_;

    std::string broadcast_endpoint_str_;
    std::string forward_msg_str_;

    bool is_all_broadcast_ = false;
    bool enable_ha_ = true;    
    bool enable_sg_ = false;
    bool is_need_send_ = false;

    uint64_t msg_send_total_ = 60000;
    uint64_t msg_send_rate_  = 1000;
    uint64_t ha_msg_recv_count_ = 0;     // 高可用Context收到的消息总数
    uint64_t core_frequency_ = 0;

    int32_t  sharding_num_ = 0;   // 分片进程数量

    // 消息检验相关数据结构
    std::map<std::string, uint64_t> nr_from_ep_msg_map_;  // 主题名<->该主题收到的消息数量映射
    std::map<std::string, uint64_t> nr_to_ep_msg_map_;    // 转发的目标主题名<->转发的消息数量

    std::string app_name_;
} g_ami_app;