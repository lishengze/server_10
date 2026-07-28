/**
 * @brief      to receive ami messages from a specified context and do simple statistics
 * @author     zhaonan, zhaonan@archforce.com.cn
 * @date       2017-06-25
 */
#include <aaf.h>
#include <adk/util.h>

#include <time.h>

using namespace aaf;

#define AAF_RECV_MAX_TRANSPORT_ID 65535

class AmiReceiver : public GenericAmiApplication
{
    ADK_LOG_DECLARE_AC(300000);

public:
    class TransportInfo
    {
    public:
        TransportInfo(const std::string& ep_name,
                     int32_t partition_no,
                     uint32_t transport_id)
            :   ep_name_(ep_name),
                partition_no_(partition_no),
                transport_id_(transport_id),
                nr_recv_msgs_(0)
        {}

        std::string     ep_name_;
        int32_t         partition_no_;
        uint32_t        transport_id_;
        uint64_t        nr_recv_msgs_;
    };


    AmiReceiver()
        :   nr_message_received_(0),
            saved_nr_message_received_(0),
            rate_(0), 
            stats_interval_(1),
            proc_delay_us_(0),
            nr_busy_threads_(0)
    {
        nr_transports_[0] = 0;
        nr_transports_[1] = 0;
        memset(&last_time_, 0x00, sizeof(last_time_));
    }

    ~AmiReceiver()
    {}

    virtual void SetAmiAppOption()
    {
        AddOptionWithAcceptor("stats-interval",
                              "the statistics interval in seconds, default 1",
                              uint32_t(1),
                              stats_interval_);
        AAF_ADDOPT_ACCEPTOR_NARG("bridge",
                                 "simulate an ami bridge instance",
                                 is_bridge_);
        AddOptionWithAcceptor("process-delay",
                              "the message process delay",
                              proc_delay_us_,
                              proc_delay_us_);
        AddOptionWithAcceptor("num-busy-threads",
                              "the number of busy loop threads",
                              nr_busy_threads_,
                              nr_busy_threads_);
        AddOption("check-context-config",
                  "check the context config and show result on the console");

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

    virtual void OnAmiAppOption(const std::string& option_name)
    {
        if (option_name == "check-context-config")
        {
            is_check_context_config_ = true;
        }
        else if (option_name == "enable-ha-context")
        {
            enable_ha_ = GetOptionArgument<int32_t>("enable-ha-context");
        }
        else if (option_name == "enable-sg-context")
        {
            enable_sg_ = GetOptionArgument<int32_t>("enable-sg-context");
        }
    }

    virtual void OnConfigureFramework(ami::Property& fw_props)
    {
        fw_props.SetValue(config::kEnableHighAvailableContext, enable_ha_);
        fw_props.SetValue(config::kEnableSingletonContext, enable_sg_);
        fw_props.SetValue(config::kEnableAppNameCheck, false);
    }

    void __attribute__((optimize("O0"))) ThreadMain(uint32_t n)
    {
        while (is_running())
        {
            for (uint32_t i = 0; i != n; ++i);
        }
    }

    virtual int32_t OnAmiInitBegin()
    {
        if (nr_busy_threads_ != 0)
        {
            for (uint32_t i = 0; i != nr_busy_threads_; ++i)
            {
                threads_ = boost::thread(&AmiReceiver::ThreadMain, this, 1000);
            }
        }

        return ErrorCode::kSuccess;
    }

    virtual int32_t OnAmiInitEnd()
    {
        if (is_check_context_config_)
        {
            std::cout << "Success" << std::endl;
            is_check_passed_ = true;
            StopAmiApp();
        }
        return ErrorCode::kSuccess;
    }


    virtual void OnAmiExitBegin()
    {
        if (is_check_context_config_)
        {
            if (!is_check_passed_)
            {
                std::cout << "Failure" << std::endl;
            }
        }
    }

    virtual void OnMessage(ami::Message* msg)
    {
        if (proc_delay_us_ != 0)
            usleep(proc_delay_us_);

        ++nr_message_received_;
        InnerOnMessage(msg, 0);
    }

    virtual void OnMessageSingleton(ami::Message* msg)
    {
        if (proc_delay_us_ != 0)
            usleep(proc_delay_us_);
        
        ++nr_message_received_;
        InnerOnMessage(msg, 1);
    }

    void InnerOnMessage(ami::Message* msg, int32_t index)
    {
        auto tp_id = msg->get_transport_id();
        if (transport_array_[index][tp_id] == NULL)
        {
            transport_array_[index][tp_id] = new TransportInfo(msg->get_endpoint_name(),
                                                        msg->get_partition_no(),
                                                        tp_id);
            transport_ring_[index][nr_transports_[index]] = transport_array_[index][tp_id];
            ADK_BARRIER();
            ++nr_transports_[index];
        }
        auto* tp_info = transport_array_[index][tp_id];
        ++(tp_info->nr_recv_msgs_);
    }

    virtual int32_t OnRun()
    {
        std::string stats;
        while (is_running())
        {
            sleep(stats_interval_);
            ADK_CALC_RATE(last_time_, saved_nr_message_received_, nr_message_received_, rate_);
            stats = (boost::format("\n=====================================================\n"
                                   "total messages : %1%, message rate : %2%\n") % nr_message_received_ % rate_).str();

            for (int32_t index = 0; index <=1; ++index)
            {
                auto nr_transports_saved = nr_transports_[index];
                for (uint32_t tp_idx = 0; tp_idx < nr_transports_saved; ++tp_idx)
                {
                    auto* tp_info = transport_ring_[index][tp_idx];
                    stats.append((boost::format("\nEndpoint: <%1%>, Partition <%2%>, Transport <%3%>, rx messages: %4%")
                                         % tp_info->ep_name_ % tp_info->partition_no_ % tp_info->transport_id_
                                         % tp_info->nr_recv_msgs_).str());
                }
            }
            
            stats.append("\n=====================================================\n");
            ADK_LOG_INFO_AC_TF("stats", stats);
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
    uint64_t            nr_message_received_;
    uint64_t            saved_nr_message_received_;
    struct timespec     last_time_;
    int64_t             rate_;
    uint32_t            nr_transports_[2];
    TransportInfo*      transport_ring_[2][AAF_RECV_MAX_TRANSPORT_ID];
    TransportInfo*      transport_array_[2][AAF_RECV_MAX_TRANSPORT_ID];
    uint32_t            stats_interval_;
    bool                is_bridge_ = false;
    uint32_t            proc_delay_us_;
    uint32_t            nr_busy_threads_;
    boost::thread       threads_;    
    bool                is_check_context_config_ = false;
    bool                is_check_passed_ = false;
    int32_t             enable_ha_ = 1;
    int32_t             enable_sg_ = 0;
}g_ami_receiver;

ADK_LOG_DEFINE(AmiReceiver);
