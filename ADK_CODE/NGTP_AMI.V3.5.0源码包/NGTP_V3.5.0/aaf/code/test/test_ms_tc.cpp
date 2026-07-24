/**
 * @brief      to transmit ami messages from a specified context and do simple statistics
 * @author     zhaonan, zhaonan@archforce.com.cn
 * @date       2017-11-09
 */
#include <aaf.h>
#include <adk/util.h>

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
        : msg_size_min_(0), msg_size_max_(0), msg_size_(128), rate_(1), stats_interval_(1)
    {}

    ~AmiReceiver()
    {}

    virtual void SetAmiAppOption()
    {
        AddOptionWithAcceptor("total-rate",
                              "the total transmit rate, messages per second, default 1",
                              uint32_t(1),
                              rate_);

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

        AAF_ADDOPT_ACCEPTOR_NARG("bridge",
                                 "simulate an ami bridge instance",
                                 is_bridge_);
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
    }

    virtual void OnConfigureFramework(ami::Property& fw_props)
    {
        fw_props.SetValue(config::kEnableHighAvailableContext, true);
        fw_props.SetValue(config::kEnableAppNameCheck, false);

        memset(stream_id_, 0x00, sizeof(stream_id_));
        memset(rx_sqn_, 0x00, sizeof(rx_sqn_));
    }

    virtual void OnMessageSingleton(ami::Message* msg)
    {
        return;
    }

    void VerifyMessage(char* buf, uint32_t len, bool reset, int32_t tp_id)
    {
        ++rx_sqn_[tp_id];
        if (*((uint64_t*)buf) != rx_sqn_[tp_id])
        {
            if (!reset)
            {
                abort();
            }
            rx_sqn_[tp_id] = *((uint64_t*)buf);
        }

        if (*((uint64_t*)(buf + sizeof(uint64_t))) != len)
            abort();

        for (uint32_t i = sizeof(uint64_t) + sizeof(uint64_t); i != len; )
        {
            if (*((uint32_t*)(buf + i)) != i)
            {
                abort();
            }

            i += 4;
        }
    }

    virtual void OnMessage(ami::Message* msg)
    {
        bool reset = false;
        if (msg->get_stream_id() != stream_id_[msg->get_transport_id()])
        {
            std::cout << "stream_id_ = " << stream_id_[msg->get_transport_id()]
                      << ", msg->get_stream_id() = " << msg->get_stream_id()
                      << std::endl;
            stream_id_[msg->get_transport_id()] = msg->get_stream_id();
            reset = true;
        }

        VerifyMessage(msg->data(), msg->size(), reset, msg->get_transport_id());
        return;
    }

    virtual int32_t OnAmiInitBegin()
    {
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

    void EncodeMessage(char* buf, uint32_t len)
    {
        *((uint64_t*)buf) = (++sqn_);
        *((uint64_t*)(buf + sizeof(uint64_t))) = len;
        for (uint32_t i = sizeof(uint64_t) + sizeof(uint64_t); i != len; )
        {
            *((uint32_t*)(buf + i)) = i;
            i += 4;
        }
    }

    virtual int32_t OnRun()
    {
        if (endpoint_map_.empty())
        {
            StopAmiApp();
            return ErrorCode::kSuccess;    
        }

        std::string stats;
        auto show_stats_rates = rate_ * stats_interval_;
        adk::SimpleRateController<> rate_ctrl(rate_);
        auto it_begin = endpoint_map_.begin();
        auto it_end = endpoint_map_.end();
        auto it_cur = it_begin;
        std::string content;
        if (!is_random_)
        {
            content.reserve(msg_size_);
            for (uint32_t i = 0; i < msg_size_; ++i)
            {
                content.append("A");
            }
        }
        uint64_t total_send = 0;
        uint64_t last_print_at = 0;
        srand((uint32_t)time(NULL));
        char* buffer = new char[8192];
        while (is_running())
        {
            uint32_t msg_size = (rand() % 4096) + 32;
            msg_size = (msg_size / 4) * 4;
            EncodeMessage(buffer, msg_size);

            it_cur = it_begin;
            while (it_cur != it_end)
            {
                auto& ep_info = it_cur->second;
                for (auto partition : ep_info.partitions_)
                {
                    rate_ctrl.Wait();    
                    ep_info.ep_hdl_->SendMsg(buffer, msg_size, partition);
                }
                ++it_cur;
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
        }
    }

private:
    uint64_t sqn_ = 0;
    uint64_t rx_sqn_[65535];
    uint32_t stream_id_[65535];

    std::map<std::string, EndpointInfo>   endpoint_map_;
    uint32_t                 msg_size_min_;
    uint32_t                 msg_size_max_;    
    uint32_t                 msg_size_;
    uint32_t                 total_size_ = 0;
    uint32_t                 rate_;
    uint32_t                 stats_interval_;
    std::string              tx_endpoint_list_;
    std::set<std::string>    using_tx_endpoints_;
    uint64_t                 nmsg_before_suspend_;
    bool                     is_bridge_ = false;
    bool                     is_random_ = false;
}g_ami_receiver;

ADK_LOG_DEFINE(AmiReceiver);
