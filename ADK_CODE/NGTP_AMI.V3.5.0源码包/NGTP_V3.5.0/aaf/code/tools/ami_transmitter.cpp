/**
 * @brief      to transmit ami messages from a specified context and do simple statistics
 * @author     zhaonan, zhaonan@archforce.com.cn
 * @date       2017-11-09
 */
#include <aaf.h>
#include <adk/util.h>
#include <adk/simple_rate_controller.h>

#include <time.h>
#include <map>
#include <vector>
#include <string>
#include <cstdlib>

#include <boost/algorithm/string.hpp>

using namespace aaf;

class AmiReceiver : public GenericAmiApplication
{
    ADK_LOG_DECLARE_AC(300000);

public:

    class EndpointInfo
    {
    public:
        EndpointInfo(EndpointHandler* ep_hdl, std::vector<int32_t>& partitions)
            :   ep_hdl_(ep_hdl), partitions_(partitions), nr_send_msgs_(0)
        {}

        EndpointInfo(const EndpointInfo& other)
        {
            ep_hdl_ = other.ep_hdl_;
            nr_send_msgs_ = other.nr_send_msgs_;
            partitions_ = other.partitions_;
        }

        EndpointHandler* ep_hdl_;
        std::vector<int32_t>    partitions_;
        uint64_t                nr_send_msgs_;
    };

    AmiReceiver()
        : msg_size_min_(0), msg_size_max_(0), msg_size_(128), rate_("1/1"), stats_interval_(1)
    {}

    ~AmiReceiver()
    {}

    virtual void SetAmiAppOption()
    {
        AddOptionWithAcceptor("total-rate",
                              "usage1: set transmit rate, default 1 msg/s"
                              "\nusage2: set transmit rate range, eg: 1/100 ",
                              std::string("1"),
                              rate_);

        AddOptionWithAcceptor("send-time-range",
                              "usage1: set transmit send time range, default 10s"
                              "\nusage2: set transmit send time range, eg: 1/10"
                              "\ndon't set 0, it doesn't make sense!",
                              std::string("10"),
                              send_time_range_);

        AddOptionWithAcceptor("send-interrupt-time-range",
                              "usage1: set transmit send interrupt time range, default 5s"
                              "\nusage2: set transmit send interrupt time range, eg: 1/10"
                              "\ndon't set 0, it doesn't make sense!",
                              std::string("5"),
                              send_interrupt_time_range_);

        AddOptionWithAcceptor("stats-interval",
                              "the statistics interval in seconds, default 1",
                              uint32_t(1),
                              stats_interval_);

        AddOptionWithAcceptor("tx-endpoints",
                              "list the transmit endpoint to use, default using all tx endpoints",
                              std::string(""),
                              tx_endpoint_list_);
        
        AddOptionWithAcceptor("nmsgs-before-suspend",
                              "the number of messages to send before suspend",
                              uint64_t(0),
                              nmsg_before_suspend_);

        AddOptionWithAcceptor("message-size",
                              "the transmit message size bytes",
                              msg_size_,
                              msg_size_);

        AddOptionWithArgument<std::string>("msg-size-range",
                              "the transmit message size bytes range ",
                              "0/0");

        AddOption("random", "the transmit message content is random");

        AddOption("loadblance", "tx_endpoint send message with loadblance");

        AAF_ADDOPT_ACCEPTOR_NARG("bridge",
                                 "simulate an ami bridge instance",
                                 is_bridge_);

        AddOptionWithAcceptor("enable-ha-context",
                  "enable high available context, ha context has same name as the application name",
                  enable_ha_,
                  enable_ha_);

        AddOptionWithAcceptor("enable-sg-context",
                  "enable singleton context, the name of singleton context is "
                  "${ha_context_name}_Singleton",
                  enable_sg_,
                  enable_sg_);
    }

    void OnAmiAppOption(const std::string& option_name)
    {
        if (option_name == "msg-size-range")
        {
            std::string option_val = GetOptionArgument<std::string>(option_name);
            std::vector<std::string> msg_range_vec;
            boost::split(msg_range_vec, option_val, boost::is_any_of("/"), boost::token_compress_on);
            msg_size_min_ = atoi(msg_range_vec[0].c_str());
            msg_size_max_ = atoi(msg_range_vec[1].c_str());
            if (msg_size_min_ >= msg_size_max_)
            {
                msg_size_min_ = 0;
                msg_size_max_ =0;
                return;
            }
            is_random_ = true;
        }
        else if (option_name == "enable-ha-context")
        {
            enable_ha_ = GetOptionArgument<int32_t>("enable-ha-context");
        }
        else if (option_name == "enable-sg-context")
        {
            enable_sg_ = GetOptionArgument<int32_t>("enable-sg-context");
        }
        else if (option_name == "random")
        {
            is_random_content_ = true;
        }
        else if (option_name == "loadblance")
        {
            is_loadblance_ = true;
        }
    }

    virtual void OnConfigureFramework(ami::Property& fw_props)
    {
        fw_props.SetValue(config::kEnableHighAvailableContext, enable_ha_);
        fw_props.SetValue(config::kEnableSingletonContext, enable_sg_);
        fw_props.SetValue(config::kEnableAppNameCheck, false);
    }

    virtual void OnMessageSingleton(ami::Message* msg)
    {
        return;
    }

    virtual void OnMessage(ami::Message* msg)
    {
        return;
    }

    virtual int32_t OnAmiInitBegin()
    {
        try
        {
            std::vector<std::string> splits;
            boost::split(splits, rate_, boost::is_any_of("/"), boost::token_compress_on);
            if (splits.size() > 1)
            {
                min_rate_ = atoi(splits[0].c_str());
                max_rate_ = atoi(splits[1].c_str());
            }
            else
            {
                min_rate_ = atoi(splits[0].c_str());
                max_rate_ = min_rate_;
            }
            splits.clear();
            boost::split(splits, send_time_range_, boost::is_any_of("/"), boost::token_compress_on);
            if (splits.size() > 1)
            {
                send_start_time_ = atoi(splits[0].c_str());
                send_end_time_ = atoi(splits[1].c_str());
            }
            else
            {
                send_start_time_ = atoi(splits[0].c_str());
                send_end_time_ = send_start_time_;
            }
            if (send_start_time_ > send_end_time_ || send_start_time_ == 0)
            {
                ADK_LOG_ERROR_AC_TF("<send-time-range> args is error", "please check the send time range set!");
                return ErrorCode::kFailure;
            }
            splits.clear();
            boost::split(splits, send_interrupt_time_range_, boost::is_any_of("/"), boost::token_compress_on);
            if (splits.size() > 1)
            {
                interrupt_start_time_ = atoi(splits[0].c_str());
                interrupt_end_time_ = atoi(splits[1].c_str());
            }
            else
            {
                interrupt_start_time_ = atoi(splits[0].c_str());
                interrupt_end_time_ = interrupt_start_time_;
            }
            if (interrupt_start_time_ > interrupt_end_time_ || interrupt_start_time_ == 0)
            {
                ADK_LOG_ERROR_AC_TF("<send-interrupt-time-range> args is error", 
                                    "please check the send interrupt time range set!");
                return ErrorCode::kFailure;
            }
        }
        catch(const std::exception& e)
        {
            ADK_LOG_ERROR_AC_TF("start program args", "please check the <--total-rate> | "
                                "<send-time-range> | <send-interrupt-time-range> usage");
            return ErrorCode::kFailure;
        }

        if (tx_endpoint_list_.empty())
            return ErrorCode::kSuccess;

        std::vector<std::string> splits;
        boost::split(splits, tx_endpoint_list_, boost::is_any_of(", \t"),
                     boost::token_compress_on);
        for (auto& ep_name : splits)
        {
            using_tx_endpoints_.insert(ep_name);
        }

        return ErrorCode::kSuccess;
    }

    virtual int32_t OnTxEndpointCreation(EndpointHandler* ep_hdl, const std::string& ep_name)
    {
        if (!using_tx_endpoints_.empty())
        {
            if (using_tx_endpoints_.find(ep_name) == using_tx_endpoints_.end())
                return ErrorCode::kSuccess;
        }

        std::vector<int32_t> partitions;
        GetTxEndpointPartitions(ep_name, partitions);
        endpoint_map_.insert(std::make_pair(ep_name, EndpointInfo(ep_hdl, partitions)));
        return ErrorCode::kSuccess;
    }

    virtual int32_t OnRun()
    {
        if (endpoint_map_.empty())
        {
            StopAmiApp();
            return ErrorCode::kSuccess;    
        }

        std::string stats;
        
        if (min_rate_ > max_rate_)
        {
            ADK_LOG_ERROR_AC_TF("<total-rate> args is error", "please check the rate set!");
            return ErrorCode::kFailure;
        }
        adk::SimpleVariableRateCtrl* rate_change_ctrl = nullptr;
        if (min_rate_ == 0)
        {
            is_has_rate_ = false;
            if (max_rate_ == 0)
            {
                rate_change_ctrl = new adk::SimpleVariableRateCtrl(1, 1);
            }
            else
            {
                rate_change_ctrl = new adk::SimpleVariableRateCtrl(1, max_rate_);
            }
        }
        else
        {
            rate_change_ctrl = new adk::SimpleVariableRateCtrl(min_rate_, max_rate_);
        }
        if (rate_change_ctrl == nullptr)
        {
            ADK_LOG_ERROR_AC_TF("Create SimpleVariableRateCtrl Error", "please check the rate set!");
            return ErrorCode::kFailure;
        }
        
        cur_rate_ = rate_change_ctrl->GetCurrentRate();
        auto show_stats_rates = cur_rate_ * stats_interval_;
         
        auto it_begin = endpoint_map_.begin();
        auto it_end = endpoint_map_.end();
        auto it_cur = it_begin;
        std::string content;
        if (!is_random_)
        {
            content.resize(msg_size_);
            if (is_random_content_)
            {
                srand((uint32_t)time(NULL));
                for (uint32_t i = 0; i < msg_size_; ++i)
                {
                    char c = (rand() % (126 - 32)) + 32 + 1;
                    content[i] = c;
                }
            }
            else
            {
                for (uint32_t i = 0; i < msg_size_; ++i)
                {
                    content[i] = 'A';
                }
            }
        }
        int send_time_diff = send_end_time_ - send_start_time_;
        int interrupt_time_diff = interrupt_end_time_ - interrupt_start_time_;

        uint64_t total_send = 0;
        uint64_t last_print_at = 0;
        int32_t send_interrupt_time = 0;  // 该变量控制 消息发送中断 的时间，为0表示接下来为消息发送连续时间
        int32_t send_continued_time = -1;  // 该变量控制 消息发送连续 的时间，为0表示接下来为消息发送中断时间
        while (is_running())
        {
            //生成随机大小的消息
            if (is_random_)
            {
                srand((uint32_t)time(NULL));
                msg_size_ = (rand() % (msg_size_max_ - msg_size_min_)) + msg_size_min_ + 1;
                content.resize(msg_size_);
                if (is_random_content_)
                {
                    for (uint32_t i = 0; i < msg_size_; ++i)
                    {                        
                        char c = (rand() % (126 - 32)) + 32 + 1;
                        content[i] = c;
                    }
                }
                else
                {
                    for (uint32_t i = 0; i < msg_size_; ++i)
                    {
                        content[i] = 'A'; 
                    }
                }
            }

            if (send_interrupt_time > 0)
            {
                it_cur = it_end;
            }
            else
            {
                it_cur = it_begin;
            }
            
            // 模拟中断
            // 如果消息发送中断时间结束
            if (send_interrupt_time == 0 && !is_has_rate_)
            {
                srand((uint32_t)time(NULL));
                if (send_time_diff == 0)
                {
                    send_continued_time = send_start_time_;
                }
                else
                {
                    send_continued_time = send_start_time_ + rand() % send_time_diff;
                }
                is_has_rate_ = true;
            }
            // 如果消息发送连续时间结束
            if (send_continued_time == 0 && is_has_rate_)
            {
                srand((uint32_t)time(NULL));
                if (interrupt_time_diff == 0)
                {
                    send_interrupt_time = interrupt_start_time_;
                }
                else
                {
                    send_interrupt_time = interrupt_start_time_ + rand() % interrupt_time_diff;   
                }
                is_has_rate_ = false;
                it_cur = it_end;
                cur_rate_ = 0;
            }
            
            while (it_cur != it_end)
            {
                auto& ep_info = it_cur->second;
                if (is_loadblance_)
                {
					ep_info.ep_hdl_->SendMsg(content);
                    rate_change_ctrl->Wait();
	                cur_rate_ = rate_change_ctrl->GetCurrentRate();
                    ++ep_info.nr_send_msgs_;
                }
                else
                {
                	for (auto partition : ep_info.partitions_)
                	{
	                    ep_info.ep_hdl_->SendMsg(content, partition);
	                    rate_change_ctrl->Wait();
	                    cur_rate_ = rate_change_ctrl->GetCurrentRate();    
	                    ++ep_info.nr_send_msgs_;
                    }
                }
                ++it_cur;
                ++total_send;
                total_size_ += content.size();

                if (nmsg_before_suspend_ != 0
                    && total_send == nmsg_before_suspend_)
                {
                    while (is_running())
                    {
                        sleep(1);
                        stats = (boost::format("\n=====================================================\n"
                                       "total messages : %1%, message rate : 0\n") % total_send).str();
                        it_cur = it_begin;
                        while (it_cur != it_end)
                        {
                            auto& ep_info = it_cur->second;
                            stats.append((boost::format("\nendpoint: <%1%>, tx messages: %2%") % it_cur->first % ep_info.nr_send_msgs_).str());
                            ++it_cur;
                        }
                        stats.append("\n=====================================================\n");
                        ADK_LOG_INFO_AC_TF("stats", stats);
                    }
                    return ErrorCode::kSuccess;
                }
            }
            show_stats_rates = cur_rate_ * stats_interval_;
            
            if (ADK_UNLIKELY(total_send >= last_print_at + show_stats_rates))
            {
                if (send_interrupt_time > 0)
                {
                    sleep(1);
                    send_interrupt_time -= 1;
                }
                if (send_continued_time > 0)
                {
                    send_continued_time -= stats_interval_;
                    if (send_continued_time < 0)
                    {
                        send_continued_time = 0;
                    }
                }
                
                uint32_t message_size = 0;
                // 如果当前速率不为0
                if (cur_rate_ != 0)
                {
                    message_size = total_size_ / cur_rate_;
                }
                
                stats = (boost::format("\n=====================================================\n"
                                       "total messages : %1%, message rate : %2%, message size : %3%\n") 
                                        % total_send % cur_rate_ % message_size).str();
                total_size_ = 0; 
                it_cur = it_begin;
                while (it_cur != it_end)
                {
                    auto& ep_info = it_cur->second;
                    stats.append((boost::format("\nendpoint: <%1%>, tx messages: %2%") % it_cur->first % ep_info.nr_send_msgs_).str());
                    ++it_cur;
                }
                stats.append("\n=====================================================\n");
                ADK_LOG_INFO_AC_TF("stats", stats);
                last_print_at = total_send;
            }
        }
        return ErrorCode::kSuccess;
    }

    void OnConfigureContextProperty(const std::string& context_name, 
                                    bool is_ha_ctx,
                                    ami::Property& props)
    {
        if (is_bridge_)
        {
            props.SetValue(ami::config::context::kIsDisableLoop, true);
            props.SetValue(ami::config::endpoint::kLoadBalance, false);
	        props.SetValue(ami::config::context::kIsDisableTxReplay, true);
        }
    }

private:
    std::map<std::string, EndpointInfo>   endpoint_map_;
    uint32_t                 msg_size_min_;
    uint32_t                 msg_size_max_;    
    uint32_t                 msg_size_;
    uint32_t                 total_size_ = 0;
    std::string              rate_;
    std::string              send_time_range_;
    int32_t                  send_start_time_;
    int32_t                  send_end_time_;
    std::string              send_interrupt_time_range_;
    int32_t                  interrupt_start_time_;
    int32_t                  interrupt_end_time_;
    uint32_t                 cur_rate_;
    uint32_t                 min_rate_;
    uint32_t                 max_rate_;
    uint32_t                 stats_interval_;
    std::string              tx_endpoint_list_;
    std::set<std::string>    using_tx_endpoints_;
    uint64_t                 nmsg_before_suspend_;
    bool                     is_bridge_ = false;
    bool                     is_random_ = false;
    bool                     is_random_content_ = false;
    bool                     is_loadblance_ = false;
    int32_t                  enable_ha_ = 1;
    int32_t                  enable_sg_ = 0;
    bool                     is_has_rate_ = true;
}g_ami_receiver;

ADK_LOG_DEFINE(AmiReceiver);