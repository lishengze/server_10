#include <aaf/generic_ami_application.h>

#include <sys/types.h>
#include <pwd.h>

#include <map>
#include <set>
#include <atomic>

#include <boost/thread/mutex.hpp>
#include <boost/regex.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/filesystem.hpp>

#include <adk/log.h>
#include <adk/monitor/monitor.h>
#include <adk/monitor/indicator_writer.h>
#include <adk/arch/generic.h>
#include <adk/constant.h>
#include <adk/encrypt_config.h>
#include <adk/fork_run.h>
#include <adk/pipeline_sharding.h>
#include <adk/monitor/indicator_writer.h>

#include <ami.h>
#include <ami/sampling_engine.h>

#include <aaf/config_key.h>

#include "aaf_env.h"
#include "aaf_func_template.h"
#include "api_internal.h"

#include "async_ami_executor.h"

#include "../sharding/sharding_proxy.h"
#include "../sharding/sharding_agent.h"

namespace ami 
{
class AppLogger : public Logger
{
public:
    AppLogger()
    {
        // SetLogger(this);
    }

    ~AppLogger()
    {}

private:
    void Log(LogLevel_def level,
             LogCode code,
             const std::string& module_name,
             const std::string& function_name,
             int32_t src_line,
             const std::string& message)
    {
        static const char* titles[] = {"AMI trace log", "AMI debug log", "AMI informtion",
                                       "AMI warning", "AMI error", "AMI fatal error"};
        ADK_LOG_RAW_TF(level, code, module_name, function_name, src_line,
                       titles[level], message);
    }
};
ami::AppLogger* g_application_logger = nullptr;
// struct DoAppLoggerInit
// {
//     DoAppLoggerInit()
//     {
//         g_application_logger = new ami::AppLogger();
//     }
// } g_do_app_logger_init;
}  // namespace ami

namespace aaf
{
namespace evt
{
const std::string kBootstrapTooLate = "the replica bootstrap too late";
}

ADK_LOG_LOCAL_AC("aaf::GenericAmiApplication", LogCodeBase::kGenericAmiApplication)

using boost::format;

const std::string kDefaultMonitorEndpointName("MonitorRequest");

extern adk::log::LogLevel g_aaf_log_level;

class AppHeartbeat
{
public:
    AppHeartbeat()
        :    hb_counter_(0)
    {}
    ~AppHeartbeat() {}

    bool OnCollection(boost::property_tree::ptree& indicator)
    {
        ++hb_counter_;
        indicator.put("heartbeat", hb_counter_);
        return true;
    }

    bool OnQuery(const int32_t query_type,
                 const boost::property_tree::ptree& query_condition,
                 boost::property_tree::ptree& reply)
    {
        reply.put("heartbeat", hb_counter_);
        return true;
    }

private:
    uint64_t hb_counter_;
};

class MessageHandlerBase : public ami::MessageHandler
{
public:
    MessageHandlerBase() : is_rx_stop_(false)
    {}

    ~MessageHandlerBase() = default;

    void StopRxMessageDeliver()
    {
        is_rx_stop_ = true;
    }

    GenericAmiApplication* application_instance_;
protected:
    volatile bool          is_rx_stop_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    friend class GenericAmiApplicationImpl;
};

class MessageHandlerHighAvailable : public MessageHandlerBase
{
public:
    void OnMessage(ami::Message* msg)
    {
        application_instance_->OnMessage(msg);
    }
};

class MessageHandlerSingleton : public MessageHandlerBase
{
public:
    void OnMessage(ami::Message* msg)
    {
        application_instance_->OnMessageSingleton(msg);
    }
};

class AppEventHandler : public ami::EventHandler
{
public:
    void OnEvent(ami::Event* event)
    {
        event_hdl_(event);
    }

    boost::function<void (ami::Event*)> event_hdl_;
};

class GenericAmiApplicationImpl : public adk::IMonitorSinker, public ami::MessageHandler
{
private:
    ADK_LOG_DECLARE_AC(LogCodeBase::kGenericAmiApplication)

public:
    GenericAmiApplicationImpl()
    {
        ha_context_ = NULL;
        singleton_context_ = NULL;
        application_instance_ = NULL;
        monitor_context_ = NULL;
        monitor_request_ep_ = NULL;
        is_monitor_rx_stop_ = false;
        ha_init_status_ = "Bootstrap";
        sig_init_status_ = "Bootstrap";
    }

    ~GenericAmiApplicationImpl()
    {}

    void set_application_instance(GenericAmiApplication* application_instance)
    {
        application_instance_ = application_instance;
        message_handler_.application_instance_ = application_instance;
        message_handler_singleton_.application_instance_ = application_instance;
    }

    // int32_t InitShmChannel();

    int32_t MonitorInit();

    int32_t AmiInit();                    // CreateMonitorEndpoint

    template<bool is_sg>
    int32_t GetIsRecord(const std::string& ctx_name, const std::string& domain_server, bool& is_recourd);

    int32_t InitContextProperty();

    int32_t GetContextProperty(ami::Property& req_props, ami::Property& resp_props);

    void MonitorExit()
    {
        is_monitor_rx_stop_ = true;
        adk::Monitor::Stop();
    }

    void AmiRxExit()
    {
        async_ami_executor_.Stop();
        message_handler_.StopRxMessageDeliver();
        message_handler_singleton_.StopRxMessageDeliver();

        if (ha_context_ != nullptr)
            ha_context_->StopDeliverMessage(-1u);
        if (singleton_context_ != nullptr)
            singleton_context_->StopDeliverMessage(-1u);
    }

    void AmiTxExit();

    virtual void Receive(adk::IMonitorSinker::Type type, uint64_t query_key, const boost::property_tree::ptree& msg_tree)
    {
        // indicator_key_ = msg_tree.get<std::string>("Key");            FIXME: write the information in msg_tree
        // indicator_desc_ = msg_tree.get<std::string>("Description");
        // if (indicator_key_.empty())
        // {
        //     indicator_key_ = "000000";
        // }
        // if (indicator_desc_.empty())
        // {
        //     indicator_desc_ = IMonitorSinker::GetTypeDesc(type);
        // }
        indicator_writer_.Write(boost::lexical_cast<std::string>(query_key), adk::IMonitorSinker::GetTypeDesc(type), msg_tree);
    }

    void OnMessage(ami::Message* msg);            // to receive monitor request

    void OnEvent(ami::Event* event);             // note: should be thread safe!

    void OnSingletonEvent(ami::Event* event);   // note: should be thread safe!

    void ResetForDeleteAllContext();

    bool is_disaster_backup(bool is_ha)
    {
        if ((is_ha) && is_ha_ctx_props_valid_)
        {
            if (ha_ctx_props_.GetValue(ami::config::context::kIsDisasterBackup, false))
                return true;
        }

        if ((!is_ha) && is_sg_ctx_props_valid_)
        {
            if (sg_ctx_props_.GetValue(ami::config::context::kIsDisasterBackup, false))
                return true;
        }

        return false;
    }

    bool is_disaster_context(bool is_ha)
    {
        if ((is_ha) && is_ha_ctx_props_valid_)
        {
            if (ha_ctx_props_.GetValue(ami::config::context::kIsDisasterContext, false))
                return true;
        }

        if ((!is_ha) && is_sg_ctx_props_valid_)
        {
            if (sg_ctx_props_.GetValue(ami::config::context::kIsDisasterContext, false))
                return true;
        }

        return false;
    }

    void SaveSelfMemoryMap();

    int32_t CheckAmiBypassValid();

    void AmiRxLeaveStreams();

private:
    AppHeartbeat                            app_hb_;
    std::map<std::string, EndpointHandler*> name_to_ep_hdl_;
    std::map<std::string, ami::RxEndpoint*> name_to_rxep_;
    std::set<std::string>                   ha_context_binding_;
    std::set<std::string>                   singleton_context_binding_;
    ami::Property                           aaf_property_;
    std::string                             domain_server_;
    std::string                             config_file_;
    ami::Context*                           ha_context_;
    ami::Context*                           singleton_context_;
    ami::Context*                           monitor_context_;
    ami::RxEndpoint*                        monitor_request_ep_;
    std::string                             name_string_;
    uint32_t                                partition_no_;
    uint32_t                                site_id_;
    uint32_t                                replica_id_;
    adk::IndicatorWriter                    indicator_writer_;
    GenericAmiApplication*                  application_instance_;
    MessageHandlerHighAvailable             message_handler_;                 // FIXME : cache alignment
    MessageHandlerSingleton                 message_handler_singleton_;
    ami::MessageHandler*                    ami_mh_ha_ = nullptr;
    ami::MessageHandler*                    ami_mh_sg_ = nullptr;
    volatile bool                           is_monitor_rx_stop_;
    boost::mutex                            fw_mutex_;
    std::set<std::string>                   master_rx_hactx_endpoint_set_;
    std::set<std::string>                   master_rx_sgctx_endpoint_set_;
    std::set<std::string>                   total_rx_endpoint_set_;
    std::set<std::string>                   rx_endpoint_set_;
    std::set<std::string>                   rx_hactx_endpoint_set_;
    std::set<std::string>                   rx_sgctx_endpoint_set_;
    std::set<std::string>                   tx_endpoint_set_;
    std::set<std::string>                   master_tx_hactx_endpoint_set_;
    std::set<std::string>                   master_tx_sgctx_endpoint_set_;
    std::set<std::string>                   total_tx_endpoint_set_;
    std::set<std::string>                   tx_hactx_endpoint_set_;
    std::set<std::string>                   tx_sgctx_endpoint_set_;
    std::set<std::string>                   reserved_rxep_set_;
    std::set<int32_t>                       rx_stream_set_;
    std::set<int32_t>                       rx_hactx_stream_set_;
    std::set<int32_t>                       rx_sgctx_stream_set_;
    std::set<int32_t>                       tx_stream_set_;
    std::set<int32_t>                       tx_hactx_stream_set_;
    std::set<int32_t>                       tx_sgctx_stream_set_;
    std::vector<TransportInfo*>             transport_infos_vec_;
    std::set<std::string>                   master_rx_endpoints_;
    std::set<std::string>                   master_tx_endpoints_;
    std::set<std::string>                   ha_master_slave_endpoints_;
    std::set<std::string>                   sg_master_slave_endpoints_;

    // std::string                          indicator_key_;
    // std::string                          indicator_desc_;
    std::string                             ha_init_status_;
    std::string                             sig_init_status_;
    std::string                             recorder_data_path_;
    bool                                    is_ha_follower_context_ = false;
    bool                                    is_sig_follower_context_ = false;
    bool                                    is_advance_follower_ = false;
    int32_t                                 sharding_num_ = 0;
    sharding::ShardingAgent*                sharding_agent_ = nullptr;


    ami::Property                           ha_ctx_props_;
    bool                                    is_ha_ctx_props_valid_ = false;
    bool                                    is_sg_ctx_props_valid_ = false;
    ami::Property                           sg_ctx_props_;
    bool                                    is_enable_sampling_ = false;
    std::string                             sampling_name_;
    bool                                    is_disable_context_ = false;
    bool                                    is_separate_log_ = false;

    boost::mutex                            count_mutex_;
    std::map<std::string, uint16_t>         transmitter_count_;
    std::map<std::string, uint16_t>         receiver_count_;

    bool                                    bootstrap_late_flag_ = false;
    bool                                    enable_late_rejoin_ = false;
    bool                                    is_leave_stream_onexit_ = false;
    bool                                    is_sig_late_join_mcast_ = false;
    bool                                    is_sig_enable_join_mcast_ = false;
    boost::mutex                            sig_join_mcast_mutex_;

    AsyncAmiExecutor                        async_ami_executor_;      // bypass功能下回环主题Tx方向的hook对象，用于定序并回环发送消息

    std::string                             te_loopback_topic_name_;  // bypass功能下回环主题名称

    void CheckJoinStream(ami::EventType event_type)
    {
        if (!is_sig_late_join_mcast_)
        {
            return;
        }

        if ((ha_init_status_ == "Bootstrap" && event_type == ami::EventType::kRoleChanged)
            || (ha_init_status_ == "Recovery" && event_type == ami::EventType::kRecoverySuccess)
            || (ha_init_status_ == "Rejoin" && event_type == ami::EventType::kRejoinSuccess))
        {
            // 等待单例节点NewContext完成
            sig_join_mcast_mutex_.lock();
            if (!singleton_context_)
            {
                is_sig_enable_join_mcast_ = true;
            }
            else
            {
                singleton_context_->JoinAllRxEndpoints();
                is_sig_late_join_mcast_ = false;
            }
            sig_join_mcast_mutex_.unlock();
        }
    }

    template<bool kIsUp>
    bool OnReceiverEvent(const std::string& transport_name);

    template<bool kIsUp>
    bool OnTransmitterEvent(const std::string& transport_name);

    int32_t IsHighAvailableBinding(const std::string& name, bool& is_ha);

    ami::Context* GetBindingContext(const std::string& name, ami::MessageHandler** msg_handler = NULL);

    // FIXME: implement
    int32_t AutoEndpointCreation()
    {
        return ErrorCode::kSuccess;
    }

    // 回环主题不为空表示启用了AmiBypass功能
    bool IsEnableAmiBypass()
    {
        return (!te_loopback_topic_name_.empty());
    }

    // FIXME: implement
    static bool MonitorMessageParser(void* user, uint64_t& query_key, std::string& url, boost::property_tree::ptree& query_condition, int32_t& query_type)
    {
        // ami::Message* msg = reinterpret_cast<ami::Message*>(user);    // FIXME: if from shm channel? filtering by user_flag!
        return true;
    }

    int32_t BuildContextBinding(ami::Context* context, std::set<std::string>& context_binding, const std::string& context_name);

    ami::RxEndpoint* GetRxEndpoint(const std::string& rx_ep_name);
    void AddRxEndpoint(const std::string& rx_ep_name, ami::RxEndpoint*);

    int32_t CreateAllTxEndpoints(const std::string& ha_context_name, const std::string& sg_context_name);

    int32_t CreateAllRxEndpoints(const std::string& ha_context_name, const std::string& sg_context_name);

    int32_t InitEndpointInfo(const std::string& ha_context_name, const std::string& sg_context_name)
    {
        if (InitTxEndpointInfo(ha_context_name, sg_context_name) != ErrorCode::kSuccess)
            return ErrorCode::kFailure;

        if (InitRxEndpointInfo(ha_context_name, sg_context_name) != ErrorCode::kSuccess)
            return ErrorCode::kFailure;

        InitForwardEndpointInfo();
        return ErrorCode::kSuccess;
    }

    int32_t InitRxEndpointInfo(const std::string& ha_context_name, const std::string& sg_context_name);
    int32_t InitTxEndpointInfo(const std::string& ha_context_name, const std::string& sg_context_name);

    int32_t InitStreamIDs(const std::string& ha_context_name, const std::string& sg_context_name)
    {
        if (InitTxStreamIDs(ha_context_name, sg_context_name) != ErrorCode::kSuccess)
            return ErrorCode::kFailure;

        if (InitRxStreamIDs(ha_context_name, sg_context_name) != ErrorCode::kSuccess)
            return ErrorCode::kFailure;
        return ErrorCode::kSuccess;
    }
    int32_t InitRxStreamIDs(const std::string& ha_context_name, const std::string& sg_context_name);
    int32_t InitTxStreamIDs(const std::string& ha_context_name, const std::string& sg_context_name);

    int32_t InitTransportInfo();
    int32_t DoInitTransportInfo(ami::Context* context);

    void InitMasterEndpointInfo();
    void DoInitMasterEndpointInfo(ami::Context* context);
    void InitForwardEndpointInfo();

    friend class GenericAmiApplication;
    template<typename T>
    friend void AssignProperty(const std::string& prop_name, const T& value);
};

ADK_LOG_DEFINE(aaf::GenericAmiApplicationImpl)

static GenericAmiApplicationImpl g_generic_ami_application_impl;

static bool CheckIsTeLeader()
{
    const char* env_str = std::getenv("AAF_IS_TE_LEADER");
    return (env_str != nullptr);
}

bool IsTeLeader()
{
    static bool is_te_leader = CheckIsTeLeader();
    return is_te_leader;
}

static bool CheckIsTeHookMessageHandler()
{
    const char* env_str = std::getenv("AAF_IS_TE_HOOK_MESSAGE_HANDLER");
    return (env_str != nullptr);
}

bool IsTeHookMessageHandler()
{
    static bool is_te_hook = CheckIsTeHookMessageHandler();
    return is_te_hook;
}

static bool CheckIsQeHookMessageHandler()
{
    const char* env_str = std::getenv("AAF_IS_QE_HOOK_MESSAGE_HANDLER");
    return (env_str != nullptr);
}

bool IsQeHookMessageHandler()
{
    static bool is_qe_hook = CheckIsQeHookMessageHandler();
    return is_qe_hook;
}

void GenericAmiApplicationImpl::OnMessage(ami::Message* msg)
{
    if (is_monitor_rx_stop_)
        return;
    // forward message to monitor framework
    msg->acquire();
    if (adk::Monitor::SubmitRequest(&GenericAmiApplicationImpl::MonitorMessageParser, msg) != adk::ErrorCode::kSuccess)
        msg->release();
}

void GenericAmiApplication::OnRoleChangeToMasterInternal()
{
    // ami bypass场景下，热备te角色切换为主集群时，工作状态也切换为主te的工作状态
    g_generic_ami_application_impl.async_ami_executor_.OnDisasterFailover();
    OnRoleChangeToMaster();
}

void GenericAmiApplication::OnStopMasterSyncInternal()
{
    OnStopMasterSync();
}

void GenericAmiApplication::OnMasterOfflineInternal()
{
    OnMasterOffline();
}

template<bool kIsUp>
bool GenericAmiApplicationImpl::OnReceiverEvent(const std::string& transport_name)
{
    boost::mutex::scoped_lock lock_guard(count_mutex_);
    auto transport_iter = receiver_count_.find(transport_name);
    if (kIsUp)  // 计数从 0 -> 1，触发调用函数
    {
        if (transport_iter != receiver_count_.end())
        {
            // 计数加一
            ++transport_iter->second;
            if (transport_iter->second != 1)
            {
                return false; // 任意 -> 非1，不触发调用
            }
            // 计数从 0 -> 1
        }
        else
        {
            receiver_count_[transport_name] = 1;  // 初始化，计数从 0 -> 1
        }
    }
    else  // 计数从 1 -> 0 | 未初始化，触发调用函数
    {
        if (transport_iter != receiver_count_.end())
        {
            if (transport_iter->second == 0)
            {
                return false;  // 为 0 时不触发调用
            }
            else // 计数非 0 时，计数减一
            {
                --transport_iter->second;
                if (transport_iter->second != 0)
                {
                    return false;  // 从 非0 -> 非0 不触发调用
                }
                // 从 1 -> 0 触发调用
            }
        }
        // 计数未初始化时，触发调用函数
    }
    return true;
}

template<bool kIsUp>
bool GenericAmiApplicationImpl::OnTransmitterEvent(const std::string& transport_name)
{
    boost::mutex::scoped_lock lock_guard(count_mutex_);
    auto transport_iter = transmitter_count_.find(transport_name);
    if (kIsUp)  // 计数从 0 -> 1，触发调用函数
    {
        if (transport_iter != transmitter_count_.end())
        {
            // 计数加一
            ++transport_iter->second;
            if (transport_iter->second != 1)
            {
                return false; // 任意 -> 非1，不触发调用
            }
            // 计数从 0 -> 1
        }
        else
        {
            transmitter_count_[transport_name] = 1;  // 初始化，计数从 0 -> 1
        }
    }
    else  // 计数从 1 -> 0 | 未初始化，触发调用函数
    {
        if (transport_iter != transmitter_count_.end())
        {
            if (transport_iter->second == 0)
            {
                return false;  // 为 0 时不触发调用
            }
            else // 计数非 0 时，计数减一
            {
                --transport_iter->second;
                if (transport_iter->second != 0)
                {
                    return false;  // 从 非0 -> 非0 不触发调用
                }
                // 从 1 -> 0 触发调用
            }
        }
        // 计数未初始化时，触发调用函数
    }
    return true;
}

std::string GenerateTransmitterName(const std::string& transport_name,
                             const std::string& endpoint_name,
                             const std::string& partition)
{
    // transport_name = endpint_name + "_" + partition + "_" + transmitter_name
    assert(transport_name.length() > 2);
    uint32_t substr_len = endpoint_name.length() + partition.length() + 2;
    return transport_name.substr(substr_len, transport_name.length() - substr_len);
}

void GenericAmiApplicationImpl::OnEvent(ami::Event* event)
{
    std::string event_what = event->what();
    // FIXME: PushEvent()
    if (event->level() > ami::EventLevel::kWarn)
    {
        if (enable_late_rejoin_ && !bootstrap_late_flag_ && 
            event_what == aaf::evt::kBootstrapTooLate)
        {
            ADK_LOG_INFO_AC_TF("receive bootstrap too late event", "{1}", event_what);
            bootstrap_late_flag_ = true;
            return;
        }

        ADK_LOG_ERROR_AC_TF("receive ami error event", "{1}", event_what);
        if (bootstrap_late_flag_)  // 忽略第一次启动失败后的 error 事件，直到重新 rejoin 启动
        {
            return;
        }
        GenericAmiApplication::StopAmiApp();
        return;
    }

    ADK_LOG_INFO_TF(LogCode::kAMIEvent, "AMI event", "what: <{1}>", event_what);

    const auto& ev_props = event->property();
    switch (event->type())
    {
    case ami::EventType::kRoleChanged:
        if (ev_props.GetValue(ami::event::property::kIsLeader, false))
        {
            try
            {
                application_instance_->OnRoleChangeToLeader();
                CheckJoinStream(event->type());
            }
            catch (...)
            {
                ADK_LOG_ERROR_AC_TF("application throw exception in <OnRoleChangeToLeader>", "exception <{1}>",
                    boost::current_exception_diagnostic_information());
                GenericAmiApplication::StopAmiApp();
            }
        }
        if (ev_props.GetValue(ami::event::property::kIsMember, false))
        {
            try
            {
                application_instance_->OnRoleChangeToMember();
                CheckJoinStream(event->type());
            }
            catch (...)
            {
                ADK_LOG_ERROR_AC_TF("application throw exception in <OnRoleChangeToMember>", "exception <{1}>",
                    boost::current_exception_diagnostic_information());
                GenericAmiApplication::StopAmiApp();
            }
        }
        break;
    case ami::EventType::kMemberLost:
        try
        {
            const auto lost_members = ev_props.GetValue(ami::event::property::kLostMembers,
                std::vector<std::string>());
            application_instance_->OnMemberLost(lost_members);
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnMemberLost>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kRecoveryBegin:
        try
        {
            async_ami_executor_.OnRecoveryBegin();
            application_instance_->OnRecoveryBegin();
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnRecoveryBegin>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kRecoverySuccess:
        try
        {
            async_ami_executor_.OnRecoverySuccess();
            application_instance_->OnRecoverySuccess();
            CheckJoinStream(event->type());
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnRecoverySuccess>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kRecoveryMessage:
        try
        {
            application_instance_->OnRecoveryMessage();
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnRecoveryMessage>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kRejoinSuccess:
        try
        {
            std::string rejoin_member = ev_props.GetValue(ami::event::property::kContextName, "");
            application_instance_->OnRejoinSuccess(rejoin_member);
            CheckJoinStream(event->type());
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnRejoinSuccess>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kRejoinFailed:
        try
        {
            // RejoinFailedFollower is error event, 
            // application will stop without delivering event.
            std::string rejoin_member = ev_props.GetValue(ami::event::property::kContextName, "");
            application_instance_->OnRejoinFailedLeader(rejoin_member);
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnRejoinFailedLeader>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kMasterOffline:
        try
        {
            application_instance_->OnMasterOfflineInternal();
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnMasterOffline>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kStopMasterSync:
        try
        {
            application_instance_->OnStopMasterSyncInternal();
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnStopMasterSync>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kRoleChangeToMaster:
        try
        {
            application_instance_->OnRoleChangeToMasterInternal();
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnRoleChangeToMaster>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kDiscardMessage:
        try
        {
            std::string businessSwitchFilePath = event->property().GetValue(ami::event::property::kBusinessSwitchFilePath, "");
            ADK_LOG_INFO_AC_TF("", "DiscardMessage props: {1}", event->property().Dump());
            if (event->property().HasValue(ami::event::property::kTotalOrderMessageSqn))
            {
                uint64_t discard_msg_sqn = event->property().GetValue(ami::event::property::kTotalOrderMessageSqn, (uint64_t)0);
                ADK_LOG_INFO_AC_TF("", "discard_msg_sqn: {1}", discard_msg_sqn);
                application_instance_->OnDiscardMessageTotalOrderSqn(discard_msg_sqn);
            }
            else
            {
                ADK_LOG_ERROR_AC_TF("", "businessSwitchFilePath: {1}", businessSwitchFilePath);
                application_instance_->OnDiscardMessage(businessSwitchFilePath);
            }
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnDiscardMessage>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kNoReceiver:  // 计数从 1 -> 0 | 未初始化，触发调用函数
        try
        {
            std::string transport_name = ev_props.GetValue(ami::event::property::kTransportName, "");

            if (OnReceiverEvent<false>(transport_name))
            {
                std::string endpoint_name = ev_props.GetValue(ami::event::property::kEndpointName, "");
                std::string partition     = ev_props.GetValue(ami::event::property::kPartition, "");
                application_instance_->OnNoReceiver(endpoint_name, partition);  // 调用函数
            }
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnNoReceiver>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kReceiverUp:  // 计数从 0 -> 1，触发调用函数
        try
        {
            std::string transport_name = ev_props.GetValue(ami::event::property::kTransportName, "");

            if (OnReceiverEvent<true>(transport_name))
            {
                std::string endpoint_name = ev_props.GetValue(ami::event::property::kEndpointName, "");
                std::string partition     = ev_props.GetValue(ami::event::property::kPartition, "");
                application_instance_->OnReceiverUp(endpoint_name, partition);  // 调用函数
            }
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnReceiverUp>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kNoTransmitter:  // 计数从 1 -> 0 | 未初始化，触发调用函数
        try
        {
            std::string transport_name = ev_props.GetValue(ami::event::property::kTransportName, "");

            if (OnTransmitterEvent<false>(transport_name))
            {
                std::string endpoint_name     = ev_props.GetValue(ami::event::property::kEndpointName, "");
                std::string partition         = ev_props.GetValue(ami::event::property::kPartition, "");
                std::string transmitter_name  = GenerateTransmitterName(transport_name, endpoint_name, partition);
                application_instance_->OnNoTransmitter(transmitter_name, endpoint_name, partition);  // 调用函数
            }
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnNoTransmitter>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kTransmitterUp:  // 计数从 0 -> 1，触发调用函数
        try
        {
            std::string transport_name = ev_props.GetValue(ami::event::property::kTransportName, "");

            if (OnTransmitterEvent<true>(transport_name))
            {
                std::string endpoint_name     = ev_props.GetValue(ami::event::property::kEndpointName, "");
                std::string partition         = ev_props.GetValue(ami::event::property::kPartition, "");
                std::string transmitter_name  = GenerateTransmitterName(transport_name, endpoint_name, partition);
                application_instance_->OnTransmitterUp(transmitter_name, endpoint_name, partition);
            }
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnTransmitterUp>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    default:
        break;
    }
}

void GenericAmiApplicationImpl::OnSingletonEvent(ami::Event* event)
{
    // FIXME: PushEvent()
    if (event->level() > ami::EventLevel::kWarn)
    {
        ADK_LOG_ERROR_AC_TF("receive ami error event", "{1}", event->what());
        GenericAmiApplication::StopAmiApp();
        return;
    }

    ADK_LOG_INFO_TF(LogCode::kAMIEvent, "AMI event", "what: <{1}>", event->what());

    const auto& ev_props = event->property();
    switch (event->type())
    {
    case ami::EventType::kRoleChanged:
        if (ev_props.GetValue(ami::event::property::kIsLeader, false))
        {
            if (nullptr == ha_context_)
            {
                try
                {
                    application_instance_->OnRoleChangeToLeader();
                }
                catch (...)
                {
                    ADK_LOG_ERROR_AC_TF("application throw exception in <OnRoleChangeToLeader>", "exception <{1}>",
                        boost::current_exception_diagnostic_information());
                    GenericAmiApplication::StopAmiApp();
                }
            }
        }
        if (ev_props.GetValue(ami::event::property::kIsMember, false))
        {
            if (nullptr == ha_context_)
            {
                try
                {
                    application_instance_->OnRoleChangeToMember();
                }
                catch (...)
                {
                    ADK_LOG_ERROR_AC_TF("application throw exception in <OnRoleChangeToMember>", "exception <{1}>",
                        boost::current_exception_diagnostic_information());
                    GenericAmiApplication::StopAmiApp();
                }
            }
        }
        break;
     case ami::EventType::kMemberLost:
        try
        {
            const auto lost_members = ev_props.GetValue(ami::event::property::kLostMembers,
                std::vector<std::string>());
            application_instance_->OnMemberLost(lost_members);
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnMemberLost>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kRecoveryBegin:
        try
        {
            application_instance_->OnSingletonRecoveryBegin();
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnSingletonRecoveryBegin>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kRecoverySuccess:
        try
        {
            application_instance_->OnSingletonRecoverySuccess();
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnSingletonRecoverySuccess>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kRecoveryMessage:
        try
        {
            application_instance_->OnSingletonRecoveryMessage();
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnSingletonRecoveryMessage>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kMasterOffline:
        try
        {
            application_instance_->OnMasterOfflineInternal();
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnMasterOffline>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kStopMasterSync:
        try
        {
            application_instance_->OnStopMasterSyncInternal();
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnStopMasterSync>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kRoleChangeToMaster:
        try
        {
            application_instance_->OnRoleChangeToMasterInternal();
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnRoleChangeToMaster>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kDiscardMessage:
        try
        {
            std::string businessSwitchFilePath = event->property().GetValue(ami::event::property::kBusinessSwitchFilePath, "");
            application_instance_->OnDiscardMessage(businessSwitchFilePath);
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnDiscardMessage>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kNoReceiver:  // 计数从 1 -> 0 | 未初始化，触发调用函数
        try
        {
            std::string transport_name = ev_props.GetValue(ami::event::property::kTransportName, "");

            if (OnReceiverEvent<false>(transport_name))
            {
                std::string endpoint_name = ev_props.GetValue(ami::event::property::kEndpointName, "");
                std::string partition     = ev_props.GetValue(ami::event::property::kPartition, "");
                application_instance_->OnNoReceiver(endpoint_name, partition);  // 调用函数
            }
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnNoReceiver>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kReceiverUp:  // 计数从 0 -> 1，触发调用函数
        try
        {
            std::string transport_name = ev_props.GetValue(ami::event::property::kTransportName, "");

            if (OnReceiverEvent<true>(transport_name))
            {
                std::string endpoint_name = ev_props.GetValue(ami::event::property::kEndpointName, "");
                std::string partition     = ev_props.GetValue(ami::event::property::kPartition, "");
                application_instance_->OnReceiverUp(endpoint_name, partition);  // 调用函数
            }
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnReceiverUp>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kNoTransmitter:  // 计数从 1 -> 0 | 未初始化，触发调用函数
        try
        {
            std::string transport_name = ev_props.GetValue(ami::event::property::kTransportName, "");

            if (OnTransmitterEvent<false>(transport_name))
            {
                std::string endpoint_name     = ev_props.GetValue(ami::event::property::kEndpointName, "");
                std::string partition         = ev_props.GetValue(ami::event::property::kPartition, "");
                std::string transmitter_name  = GenerateTransmitterName(transport_name, endpoint_name, partition);
                application_instance_->OnNoTransmitter(transmitter_name, endpoint_name, partition);  // 调用函数
            }
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnNoTransmitter>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    case ami::EventType::kTransmitterUp:  // 计数从 0 -> 1，触发调用函数
        try
        {
            std::string transport_name = ev_props.GetValue(ami::event::property::kTransportName, "");

            if (OnTransmitterEvent<true>(transport_name))
            {
                std::string endpoint_name     = ev_props.GetValue(ami::event::property::kEndpointName, "");
                std::string partition         = ev_props.GetValue(ami::event::property::kPartition, "");
                std::string transmitter_name  = GenerateTransmitterName(transport_name, endpoint_name, partition);
                application_instance_->OnTransmitterUp(transmitter_name, endpoint_name, partition);
            }
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnTransmitterUp>", "exception <{1}>",
                boost::current_exception_diagnostic_information());
            GenericAmiApplication::StopAmiApp();
        }
        break;
    default:
        break;
    }
}

int32_t GenericAmiApplicationImpl::MonitorInit()
{
    if (indicator_writer_.Init(application_instance_->GetLogDir(), application_instance_->GetApplicationName())
        != adk::ErrorCode::kSuccess)
    {
        ADK_LOG_ERROR_AC_TF("init inidcator writer failed", "log directory <{1}>, application name <{2}>",
                        application_instance_->GetLogDir(), application_instance_->GetApplicationName());
        return ErrorCode::kFailure;
    }

    adk::Monitor::Start();

    if (adk::Monitor::PluginSinker(this) != adk::ErrorCode::kSuccess)
    {
        ADK_LOG_ERROR_AC_TF("plugin monitor sinker failed", "");
        return ErrorCode::kFailure;
    }

    adk::MonitorOps app_hb_ops;
    app_hb_ops.on_collection_indicator = boost::bind(&AppHeartbeat::OnCollection, &app_hb_, _1);
    app_hb_ops.on_query = boost::bind(&AppHeartbeat::OnQuery, &app_hb_, _1, _2, _3);
    app_hb_ops.is_collection_indicator = true;

    REGISTER_OBJECT(AppHeartbeat, "app_heartbeat", &app_hb_ops);

    if (IsEnableAmiBypass())
    {
        adk::MonitorOps bypass_ops;
        bypass_ops.is_collection_indicator = true;
        bypass_ops.on_collection_indicator = boost::bind(&AsyncAmiExecutor::OnCollection, &async_ami_executor_, _1);
        REGISTER_OBJECT(Bypass, name_string_, &bypass_ops);
    }

    return ErrorCode::kSuccess;
}

int32_t GenericAmiApplicationImpl::BuildContextBinding(ami::Context* context,
                                                       std::set<std::string>& context_binding,
                                                       const std::string& context_name)
{
    ami::Property result_props;
    if (context->GetProperty(ami::config::context::kTxEndpointNameList, result_props)  // 该代码保证 Context 已经初始化完成
        != ami::ErrorCode::kSuccess)
    {
        ADK_LOG_ERROR_AC_TF("get static configured endpoint name list failed", "context name <{1}>",
                        context_name);
        return ErrorCode::kFailure;
    }

    if (bootstrap_late_flag_)
    {
        ADK_LOG_INFO_AC_TF("NewContext failed, try again", "");
        bootstrap_late_flag_ = false;
        return ErrorCode::kTryAgain;
    }

    std::vector<std::string> tx_endpoint_name_list = result_props.GetValue(
                            ami::config::context::kTxEndpointNameList, std::vector<std::string>());
    for (auto it = tx_endpoint_name_list.begin(); it != tx_endpoint_name_list.end(); ++it)
        context_binding.insert(*it);

    return ErrorCode::kSuccess;
}

int32_t GenericAmiApplicationImpl::CreateAllTxEndpoints(const std::string& ha_context_name, const std::string& sg_context_name)
{
    ami::Context* context_vec[2] = { ha_context_, singleton_context_ };
    std::set<std::string>* set_vec[2] = { &tx_hactx_endpoint_set_, &tx_sgctx_endpoint_set_ };
    std::set<std::string>* master_set_vec[2] = { &master_tx_hactx_endpoint_set_, &master_tx_sgctx_endpoint_set_ };
    const std::string* context_name[2] = { &ha_context_name, &sg_context_name };

    uint32_t counter = 0;
    do {
        if (context_vec[counter] == NULL)
        {
            continue;
        }

        std::set<std::string>& master_txep_set = *(master_set_vec[counter]);//初始化可能存在的主集群tx_ep
        // handle normal endpoints
        std::set<std::string>& txep_set = *(set_vec[counter]);
        if (txep_set.empty())
        {
            ADK_LOG_INFO_AC_TF("there are no transmit endpoint to create", "context name <{1}>",
                           *context_name[counter]);
        }

        for (const auto& tx_ep_name : txep_set)
        {
            // FIXME: divide by master/slave HighAvailable/Singleton
            if (name_to_ep_hdl_.end() != name_to_ep_hdl_.find(tx_ep_name))
            {
                continue;
            }

            EndpointHandler* aaf_ep_hdl_ptr;
            ami::Property ep_props;
            ep_props.SetValue(ami::config::endpoint::kName, tx_ep_name);
            // FIXME: set tunning properties
            ami::TxEndpoint* ami_tx_ep_hdl = context_vec[counter]->CreateTxEndpoint(ep_props);
            if (ami_tx_ep_hdl != NULL)
            {
                // 主te、热备te都会创建tx方向的回环主题，热备TE灾切后会切换成主TE的工作状态
                if (tx_ep_name == te_loopback_topic_name_)
                {
                    async_ami_executor_.tete_endpoint_ = ami_tx_ep_hdl; // 保存真正的 回环主题 的 EndpointHandler
                    aaf_ep_hdl_ptr = &async_ami_executor_;              // 劫持回环主题的hook对象
                    if (IsTeLeader()) // 主TE
                    {
                        if (async_ami_executor_.work_state_ != WorkState::kLeaderBypassAmi)
                        {
                            async_ami_executor_.work_state_ = WorkState::kLeaderBypassAmi;
                            ADK_LOG_INFO_AC_TF("te hook TxEndpoint, set work_state",
                                               "now work state <{1}>.",
                                               async_ami_executor_.work_state_);
                        }
                    }
                    else  // 热备TE
                    {
                        if (async_ami_executor_.work_state_ != WorkState::kDisasterBypassAmi)
                        {
                            async_ami_executor_.work_state_ = WorkState::kDisasterBypassAmi;
                            ADK_LOG_INFO_AC_TF("te hot hook TxEndpoint, set work_state",
                                               "now work state <{1}>.",
                                               async_ami_executor_.work_state_);
                        }
                    }
                }
                else
                {
                    aaf_ep_hdl_ptr = new EndpointHandler();
                }
                aaf_ep_hdl_ptr->endpoint_ = ami_tx_ep_hdl;
                aaf_ep_hdl_ptr->context_ = context_vec[counter];
                name_to_ep_hdl_.insert(std::make_pair(tx_ep_name, aaf_ep_hdl_ptr));
            }
            else
            {
                ADK_LOG_ERROR_AC_TF("create transmit endpoint failed", "context name <{1}>",
                                *context_name[counter]);
                return ErrorCode::kFailure;
            }

            try
            {
                //回调在ADA框架中注册的OnTxEndpointCreationForAda函数，该函数用于将创建好的远程SQL发送主题传递给ADA框架
                if (FunctionRegistry<OnTxEndpointCreationFunc>::ForeachFunc(
                        [&](OnTxEndpointCreationFunc func) -> int32_t {
                            return func(aaf_ep_hdl_ptr, tx_ep_name);
                        })
                    != ErrorCode::kSuccess)
                {
                    return ErrorCode::kFailure;
                }
                if (application_instance_->OnTxEndpointCreation(
                        aaf_ep_hdl_ptr, tx_ep_name)
                    != ErrorCode::kSuccess)
                {
                    ADK_LOG_ERROR_AC_TF("OnTxEndpointCreation return with error",
                                        "context name <{1}>",
                                        *context_name[counter]);
                    return ErrorCode::kFailure;
                }
            }
            catch (...)
            {
                ADK_LOG_ERROR_AC_TF(
                    "application throw exception in <OnTxEndpointCreation>",
                    "exception <{1}>",
                    boost::current_exception_diagnostic_information());
                return ErrorCode::kFailure;
            }
        }


        std::vector<std::string> master_tx_endpoints;

        context_vec[counter]->PropertyAt(ami::config::context::kMasterTxEndpoints)
                              .GetValue(master_tx_endpoints);
        for (const auto& ep_name : master_tx_endpoints)
        {
            master_txep_set.insert(ep_name);
            total_tx_endpoint_set_.insert(ep_name);
        }

        if (!is_disaster_backup(counter == 0)
            || is_disaster_context(counter == 0))
        {
            for (const auto& tx_ep_name : master_tx_endpoints)
            {
                EndpointHandler* aaf_ep_hdl_ptr;
                ami::Property ep_props;
                ep_props.SetValue(ami::config::endpoint::kName, tx_ep_name);
                // FIXME: set tunning properties
                ami::TxEndpoint* ami_tx_ep_hdl = context_vec[counter]->CreateTxEndpoint(ep_props);
                if (ami_tx_ep_hdl != NULL)
                {
                    aaf_ep_hdl_ptr = new EndpointHandler();
                    aaf_ep_hdl_ptr->endpoint_ = ami_tx_ep_hdl;
                    aaf_ep_hdl_ptr->context_ = context_vec[counter];
                    name_to_ep_hdl_.insert(std::make_pair(tx_ep_name, aaf_ep_hdl_ptr));
                }
                else
                {
                    ADK_LOG_ERROR_AC_TF("create master TxEndpoint failed", "context name <{1}>, endpoint name <{2}>",
                                    *context_name[counter], tx_ep_name);
                    return ErrorCode::kFailure;
                }

                try
                {
                    //回调在ADA框架中注册的OnTxEndpointCreationForAda函数，该函数用于将创建好的远程SQL发送主题传递给ADA框架
                    if (FunctionRegistry<OnTxEndpointCreationFunc>::ForeachFunc(
                            [&](OnTxEndpointCreationFunc func) -> int32_t {
                                return func(aaf_ep_hdl_ptr, tx_ep_name);
                            })
                        != ErrorCode::kSuccess)
                    {
                        return ErrorCode::kFailure;
                    }
                    if (application_instance_->OnTxEndpointCreation(
                            aaf_ep_hdl_ptr, tx_ep_name)
                        != ErrorCode::kSuccess)
                    {
                        ADK_LOG_ERROR_AC_TF(
                            "OnTxEndpointCreation return with error",
                            "context name <{1}>",
                            *context_name[counter]);
                        return ErrorCode::kFailure;
                    }
                }
                catch (...)
                {
                    ADK_LOG_ERROR_AC_TF(
                        "application throw exception in <OnTxEndpointCreation>",
                        "exception <{1}>",
                        boost::current_exception_diagnostic_information());
                    return ErrorCode::kFailure;
                }
            }
        }

    } while ((++counter) < 2);

    return ErrorCode::kSuccess;
}

int32_t GenericAmiApplicationImpl::CreateAllRxEndpoints(const std::string& ha_context_name, const std::string& sg_context_name)
{
    ami::Context* context_vec[2] = { ha_context_, singleton_context_ };
    const std::string* context_name[2] = { &ha_context_name, &sg_context_name };
    std::set<std::string>* set_vec[2] = { &rx_hactx_endpoint_set_, &rx_sgctx_endpoint_set_ };
    std::set<std::string>* master_set_vec[2] = { &master_rx_hactx_endpoint_set_, &master_rx_sgctx_endpoint_set_ };

    ami::MessageHandler* default_handler[2];
    if (ami_mh_ha_ != nullptr)
        default_handler[0] = ami_mh_ha_;
    else
        default_handler[0] = &message_handler_;

    if (ami_mh_sg_ != nullptr)
        default_handler[1] = ami_mh_sg_;
    else
        default_handler[1] = &message_handler_singleton_;

    uint32_t counter = 0;
    std::map<void*, int32_t> handler_to_id_map;
    do {
        if (context_vec[counter] == NULL)
        {
            continue;
        }

        std::set<std::string>& master_rxep_set = *(master_set_vec[counter]);//初始化可能存在的主集群rx_ep
        // handle normal endpoints
        std::set<std::string>& rxep_set = *(set_vec[counter]);
        if (rxep_set.empty())
        {
            ADK_LOG_INFO_AC_TF("no receive endpoint to create", "context name <{1}>",
                           *context_name[counter]);
        }

        for (const auto& rx_ep_name : rxep_set)
        {
            // FIXME: divide by master/slave HighAvailable/Singleton
            if (name_to_rxep_.end() != name_to_rxep_.find(rx_ep_name))
            {
                ADK_LOG_WARN_AC_TF("duplicated RxEndpoint", "endpoint <{1}>, context <{2}>",
                                   rx_ep_name, *context_name[counter]);
                continue;
            }

            ami::Property ep_props;
            ep_props.SetValue(ami::config::endpoint::kName, rx_ep_name);
            // FIXME: set tunning properties

            ami::MessageHandler* msg_handler = NULL;
            try
            {
                //回调在ADA框架中注册的OnRxEndpointCreationForAda函数，该函数用于将创建好的远程SQL接收主题传递给ADA框架
                if (FunctionRegistry<OnRxEndpointCreationFunc>::ForeachFunc(
                        [&](OnRxEndpointCreationFunc func) -> int32_t {
                            return func(rx_ep_name, &msg_handler, (context_vec[counter] == ha_context_));
                        })
                    != ErrorCode::kSuccess)
                {
                    return ErrorCode::kFailure;
                }
                if (msg_handler == NULL)
                {
                    if (application_instance_->OnRxEndpointCreation(
                            rx_ep_name, &msg_handler, (context_vec[counter] == ha_context_))
                        != ErrorCode::kSuccess)
                    {
                        ADK_LOG_ERROR_AC_TF("OnRxEndpointCreation return with error",
                                            "context name <{1}>",
                                            *context_name[counter]);
                        return ErrorCode::kFailure;
                    }
                }
            }
            catch (...)
            {
                ADK_LOG_ERROR_AC_TF(
                    "application throw exception in <OnRxEndpointCreation>",
                    "exception <{1}>",
                    boost::current_exception_diagnostic_information());
                return ErrorCode::kFailure;
            }
            msg_handler = (msg_handler == NULL) ? default_handler[counter] : msg_handler;

            if (counter == 0
                && IsTeHookMessageHandler())   // 高可用 Context，并且是 TE/TE 热备
            {
                static bool first = true;
                if (first)
                {
                    first = false;
                    ADK_LOG_INFO_AC_TF("hook te message handler begin", "");
                }

                AsyncAmiExeMessageHandler* hdl = new AsyncAmiExeMessageHandler;  // 创建内部的消息回调对象，用于劫持应用注册的消息回调对象
                hdl->async_ami_executor_ = &async_ami_executor_;
                hdl->handler_ = msg_handler;  // 保存应用注册的消息回调对象

                void* hdl_addr = msg_handler;
                auto it = handler_to_id_map.find(hdl_addr);
                if (it == handler_to_id_map.end()) // 新出现的 handler 
                {
                    // 拦截高可用 Context 的所有 handler
                    hdl->hdl_id_ = async_ami_executor_.NewHandlerId(msg_handler);  // 为应用注册的消息回调对象生成一个Id
                    if (hdl->hdl_id_ == kInvalidMsgHandlerId)
                    {
                        ADK_LOG_ERROR_AC_TF("the number of registered MessageHandler objects exceeds the limitation",
                                            "the limitation is <{1}>, context name <{2}>",
                                            kMsgHandlerIdMax,
                                            *context_name[counter]);
                        return ErrorCode::kFailure;
                    }

                    handler_to_id_map[hdl_addr] = hdl->hdl_id_;
                    it = handler_to_id_map.find(hdl_addr);
                }
                else
                {
                    hdl->hdl_id_ = it->second;
                }

                msg_handler = hdl;  // 劫持应用注册的消息回调对象

                // TE组件接收自己的回环消息后需要转发给 qe和热备te，转发时携带这个头部消息，供qe和热备te判断，防止处理两次
                if (rx_ep_name == te_loopback_topic_name_)
                {
                    async_ami_executor_.set_tete_hdl_id(it->second);  // 回环发送时，用于填充 header 结构中的 handler id
                    hdl->is_tete_hook_ = true;  // 标识当前被劫持的应用主题是回环主题，方便bypass区分处理
                }

                ADK_LOG_INFO_AC_TF("hook te message handler",
                                   "RxEndpoint <{1}>, hdl_id <{2}>, is_tete_hook <{3}>",
                                   rx_ep_name,
                                   hdl->hdl_id_,
                                   hdl->is_tete_hook_);
            }

            ami::RxEndpoint* ret_ep = context_vec[counter]->CreateRxEndpoint(*msg_handler, ep_props);
            if (ret_ep == NULL)
            {
                ADK_LOG_ERROR_AC_TF("create RxEndpoint failed", "endpoint <{1}>, context <{2}>",
                                rx_ep_name, *context_name[counter]);
                return ErrorCode::kFailure;
            }

            AddRxEndpoint(rx_ep_name, ret_ep);
        }

        // handle master to slave endpoint and master endpoints
        // handle master rx endpoints
        std::vector<std::string> master_rx_endpoints;
        context_vec[counter]->PropertyAt(ami::config::context::kMasterRxEndpoints)
                              .GetValue(master_rx_endpoints);
        for (const auto& ep_name : master_rx_endpoints)
        {
            master_rxep_set.insert(ep_name);
            total_rx_endpoint_set_.insert(ep_name);
        }

        if (!is_disaster_backup(counter == 0)
            || is_disaster_context(counter == 0))
        {
            for (const auto& rx_ep_name : master_rxep_set)  // 使用set，确保和master的创建顺序一致
            {
                ami::MessageHandler* msg_handler = NULL;
                try
                {
                    //回调在ADA框架中注册的OnRxEndpointCreationForAda函数，该函数用于将创建好的远程SQL接收主题传递给ADA框架
                    if (FunctionRegistry<OnRxEndpointCreationFunc>::ForeachFunc(
                            [&](OnRxEndpointCreationFunc func) -> int32_t {
                                return func(rx_ep_name, &msg_handler, (context_vec[counter] == ha_context_));
                            })
                        != ErrorCode::kSuccess)
                    {
                        return ErrorCode::kFailure;
                    }
                    if (msg_handler != NULL)
                        continue;
                    if (application_instance_->OnRxEndpointCreation(
                            rx_ep_name, &msg_handler, (context_vec[counter] == ha_context_))
                        != ErrorCode::kSuccess)
                    {
                        ADK_LOG_ERROR_AC_TF("OnRxEndpointCreation return with error",
                                            "context name <{1}>",
                                            *context_name[counter]);
                        return ErrorCode::kFailure;
                    }
                }
                catch (...)
                {
                    ADK_LOG_ERROR_AC_TF(
                        "application throw exception in <OnRxEndpointCreation>",
                        "exception <{1}>",
                        boost::current_exception_diagnostic_information());
                    return ErrorCode::kFailure;
                }
                msg_handler = (msg_handler == NULL) ? default_handler[counter] : msg_handler;


                if (counter == 0
                    && IsQeHookMessageHandler()) // 拦截 qe 高可用 Context 的所有 handler
                {
                    static bool first = true;
                    if (first)
                    {
                        first = false;
                        ADK_LOG_INFO_AC_TF("hook qe message handler begin", "");
                    }

                    // 设置qe组件的工作状态
                    if (async_ami_executor_.work_state_ != WorkState::kQeBypassAmi)
                    {
                        async_ami_executor_.work_state_ = WorkState::kQeBypassAmi;
                        ADK_LOG_INFO_AC_TF("qe set work_state",
                                           "now work state <{1}>.",
                                           async_ami_executor_.work_state_);
                    }

                    AsyncAmiExeMessageHandler* hdl = new AsyncAmiExeMessageHandler;  // 创建内部的消息回调对象，用于劫持应用注册的消息回调对象
                    hdl->async_ami_executor_ = &async_ami_executor_;
                    hdl->handler_ = msg_handler;  // 保存应用注册的消息回调对象

                    void* hdl_addr = msg_handler;
                    auto it = handler_to_id_map.find(hdl_addr);
                    if (it == handler_to_id_map.end())
                    {
                        hdl->hdl_id_ = async_ami_executor_.NewHandlerId(msg_handler);  // 为应用注册的消息回调对象生成一个Id
                        if (hdl->hdl_id_ == kInvalidMsgHandlerId)
                        {
                            ADK_LOG_ERROR_AC_TF("the number of registered MessageHandler objects exceeds the limitation",
                                                "the limitation is <{1}>, context name <{2}>",
                                                kMsgHandlerIdMax,
                                                *context_name[counter]);
                            return ErrorCode::kFailure;
                        }
                        
                        handler_to_id_map[hdl_addr] = hdl->hdl_id_;
                        it = handler_to_id_map.find(hdl_addr);
                    }
                    else
                    {
                        hdl->hdl_id_ = it->second;
                    }

                    msg_handler = hdl;  // 劫持应用注册的消息回调对象

                    if (rx_ep_name == te_loopback_topic_name_)
                    {
                        async_ami_executor_.set_tete_hdl_id(it->second);  // 回环发送时，用于填充 header 结构中的 handler id
                        hdl->is_tete_hook_ = true;  // 标识当前被劫持的应用主题是回环主题，方便bypass区分处理
                    }

                    ADK_LOG_INFO_AC_TF("hook qe message handler", "RxEndpoint <{1}>, hdl_id <{2}>, is_tete_hook <{3}>",
                                       rx_ep_name, hdl->hdl_id_, hdl->is_tete_hook_);
                }

                ami::Property rx_ep_props;
                rx_ep_props.SetValue(ami::config::endpoint::kName, rx_ep_name);
                ami::RxEndpoint* ret_ep = context_vec[counter]->CreateRxEndpoint(*msg_handler, rx_ep_props);
                if (nullptr == ret_ep)
                {
                    ADK_LOG_ERROR_AC_TF("create master RxEndpoint failed", "endpoint <{1}>, context <{2}>",
                                        rx_ep_name, *context_name[counter]);
                    return ErrorCode::kFailure;
                }

                AddRxEndpoint(rx_ep_name, ret_ep);
            }
        }

    } while ((++counter) < 2);

    return ErrorCode::kSuccess;
}

int32_t GenericAmiApplicationImpl::InitRxEndpointInfo(const std::string& ha_context_name, const std::string& sg_context_name)
{
    ami::Context* context_vec[2] = { ha_context_, singleton_context_ };
    const std::string* context_name[2] = { &ha_context_name, &sg_context_name };
    std::set<std::string>* set_vec[2] = { &rx_hactx_endpoint_set_, &rx_sgctx_endpoint_set_ };
    uint32_t counter = 0;
    do {
        if (context_vec[counter] == NULL)
        {
            continue;
        }

        std::vector<std::string> rxep_vec;
        std::set<std::string>& rxep_set = *(set_vec[counter]);

        if (context_vec[counter]->PropertyAt(ami::config::context::kRxEndpointNameList).GetValue(rxep_vec) != ami::ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("get receive endpoint name list failed", "context name <{1}>",
                            *context_name[counter]);
            return ErrorCode::kFailure;
        }

        for (auto& rx_ep_name : rxep_vec)
        {
            if (reserved_rxep_set_.find(rx_ep_name) != reserved_rxep_set_.end())
            {
                continue;
            }

            rxep_set.insert(rx_ep_name);
            rx_endpoint_set_.insert(rx_ep_name);
            total_rx_endpoint_set_.insert(rx_ep_name);
        }
    } while ((++counter) < 2);

    return ErrorCode::kSuccess;
}

int32_t GenericAmiApplicationImpl::InitTxEndpointInfo(const std::string& ha_context_name, const std::string& sg_context_name)
{
    ami::Context* context_vec[2] = { ha_context_, singleton_context_ };
    std::set<std::string>* set_vec[2] = { &tx_hactx_endpoint_set_, &tx_sgctx_endpoint_set_ };
    const std::string* context_name[2] = { &ha_context_name, &sg_context_name };

    uint32_t counter = 0;
    do {
        if (context_vec[counter] == NULL)
        {
            continue;
        }

        std::vector<std::string> txep_vec;
        std::set<std::string>& txep_set = *(set_vec[counter]);

        if (context_vec[counter]->PropertyAt(ami::config::context::kTxEndpointNameList).GetValue(txep_vec) != ami::ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("get transmit endpoint name list failed", "context name <{1}>",
                            *context_name[counter]);
            return ErrorCode::kFailure;
        }

        for (const auto& rx_ep_name : txep_vec)
        {
            txep_set.insert(rx_ep_name);
            tx_endpoint_set_.insert(rx_ep_name);
            total_tx_endpoint_set_.insert(rx_ep_name);
        }
    } while ((++counter) < 2);
    return ErrorCode::kSuccess;
}

int32_t GenericAmiApplicationImpl::InitRxStreamIDs(const std::string& ha_context_name, const std::string& sg_context_name)
{
    ami::Context* context_vec[2] = { ha_context_, singleton_context_ };
    const std::string* context_name[2] = { &ha_context_name, &sg_context_name };
    std::set<std::string>* set_vec[2] = { &rx_hactx_endpoint_set_, &rx_sgctx_endpoint_set_ };

    std::set<int32_t>* stream_vec[2] = { &rx_hactx_stream_set_, &rx_sgctx_stream_set_ };
    std::set<std::string>* tmp_master_slave_endpoints[2] = {&ha_master_slave_endpoints_, &sg_master_slave_endpoints_};

    uint32_t counter = 0;
    do {
        if (context_vec[counter] == NULL)
        {
            continue;
        }

        std::set<std::string>& rxep_set = *(set_vec[counter]);

        std::vector<int32_t> stream_ids;
        stream_ids.reserve(8);
        for (const auto& rx_ep_name : rxep_set)
        {
            if (context_vec[counter]->PropertyAt(ami::config::context::kRxEndpoint, rx_ep_name)
                                                (ami::config::endpoint::kTransportIdList).GetValue(stream_ids) != ami::ErrorCode::kSuccess)
            {
                ADK_LOG_ERROR_AC_TF("get transport id on RxEndpoint failed", "endpoint <{1}>, context <{2}>", rx_ep_name, context_name[counter]);
                return ErrorCode::kFailure;
            }

            for (auto id : stream_ids)
            {
                if (stream_vec[counter]->find(id) != stream_vec[counter]->end())
                {
                    ADK_LOG_ERROR_AC_TF("duplicated stream id was detected", "");
                    continue;
                }
                stream_vec[counter]->insert(id);
                rx_stream_set_.insert(id);
            }
        }

        std::set<std::string> clear_list;
        for (const auto& master_slave_endpoint : *tmp_master_slave_endpoints[counter])
        {
            if (context_vec[counter]->PropertyAt(ami::config::context::kRxEndpoint, master_slave_endpoint)
                                                (ami::config::endpoint::kTransportIdList).GetValue(stream_ids) != ami::ErrorCode::kSuccess)
            {
                continue;
            }

            for (auto id : stream_ids)
            {
                if (stream_vec[counter]->find(id) != stream_vec[counter]->end())
                {
                    ADK_LOG_ERROR_AC_TF("duplicated stream id was detected", "");
                    continue;
                }
                stream_vec[counter]->insert(id);
                rx_stream_set_.insert(id);
            }

            clear_list.insert(master_slave_endpoint);
        }

        for (const auto& ep_name : clear_list)
            (*tmp_master_slave_endpoints[counter]).erase(ep_name);

    } while ((++counter) < 2);

    std::set<std::string> all_master_slave_endpoints = *tmp_master_slave_endpoints[0];
    for (const auto& ep : *tmp_master_slave_endpoints[1])
    {
        all_master_slave_endpoints.insert(ep);
    }

    if (!all_master_slave_endpoints.empty())
    {
        ADK_LOG_ERROR_AC_TF("can not find the transport id of master-slave forward endpoints", "endpoints <{1}>",
                        boost::algorithm::join(all_master_slave_endpoints, ","));
        return ErrorCode::kFailure;
    }

    return ErrorCode::kSuccess;
}

int32_t GenericAmiApplicationImpl::InitTxStreamIDs(const std::string& ha_context_name, const std::string& sg_context_name)
{
    ami::Context* context_vec[2] = { ha_context_, singleton_context_ };
    const std::string* context_name[2] = { &ha_context_name, &sg_context_name };
    std::set<std::string>* ep_set_vec[2] = { &tx_hactx_endpoint_set_, &tx_sgctx_endpoint_set_ };

    std::set<int32_t>* stream_vec[2] = { &tx_hactx_stream_set_, &tx_sgctx_stream_set_ };
    uint32_t counter = 0;
    do {
        if (context_vec[counter] == NULL)
        {
            continue;
        }

        std::set<std::string>& txep_set = *(ep_set_vec[counter]);

        std::vector<int32_t> stream_ids;
        stream_ids.reserve(8);
        for (const auto& tx_ep_name : txep_set)
        {
            if (context_vec[counter]->PropertyAt(ami::config::context::kTxEndpoint, tx_ep_name)
                                                (ami::config::endpoint::kTransportIdList).GetValue(stream_ids) != ami::ErrorCode::kSuccess)
            {
                ADK_LOG_ERROR_AC_TF("get transport id on TxEndpoint failed", "transport <{1}>, context <{2}>", tx_ep_name, context_name[counter]);
                return ErrorCode::kFailure;
            }

            for (auto id : stream_ids)
            {
                if (stream_vec[counter]->find(id) != stream_vec[counter]->end())
                {
                    ADK_LOG_ERROR_AC_TF("duplicated stream id was detected", "");
                    continue;
                }
                stream_vec[counter]->insert(id);
                tx_stream_set_.insert(id);
            }
        }
    } while ((++counter) < 2);

    return ErrorCode::kSuccess;
}

int32_t GenericAmiApplicationImpl::DoInitTransportInfo(ami::Context* context)
{
    std::vector<ami::Property> tp_infos;
    auto ec = context->PropertyAt(ami::config::context::kTransportInfoList).GetValue(tp_infos);
    if (ec != ErrorCode::kSuccess)
    {
        ADK_LOG_ERROR_AC_TF("get transport info list failed", "");
        return ec;
    }

    int32_t max_transport_id = 0;
    for (auto& tp_info : tp_infos)
    {
        int32_t transport_id = tp_info.GetValue(ami::config::context::kTransportId, 0);
        if (transport_id == 0)
        {
            ADK_LOG_ERROR_AC_TF("transport id is invalid", "transport id <{1}>, transport name <{2}>",
                            transport_id, tp_info.GetValue(ami::config::context::kTransportName, ""));
            return aaf::ErrorCode::kFailure;
        }

        max_transport_id = std::max(max_transport_id, transport_id);
    }

    if (max_transport_id != 0)
    {
        if ((size_t)max_transport_id >= transport_infos_vec_.size())
        {
            transport_infos_vec_.resize(max_transport_id + 8, NULL);
        }

        for (auto& tp_info : tp_infos)
        {
            int32_t transport_id = tp_info.GetValue(ami::config::context::kTransportId, 0);
            assert((size_t)transport_id < transport_infos_vec_.size());
            if (transport_infos_vec_[transport_id] != NULL)
            {
                continue;
            }

            transport_infos_vec_[transport_id] = new TransportInfo;
            TransportInfo& transport_info      = *(transport_infos_vec_[transport_id]);
            transport_info.transport_id        = transport_id;
            transport_info.tier_name           = tp_info.GetValue(ami::config::context::kTierName, "");
            transport_info.endpoint_name       = tp_info.GetValue(ami::config::context::kEndpointName, "");
	    	transport_info.endpoint_id         = tp_info.GetValue(ami::config::context::kEndpointId, 0);
            transport_info.transport_partition = tp_info.GetValue(ami::config::context::kTransportPartition, 1);
            transport_info.transport_name      = tp_info.GetValue(ami::config::context::kTransportName, "");
            transport_info.transport_direction = tp_info.GetValue(ami::config::context::kTransportDirection, 0);
        }
    }

    return aaf::ErrorCode::kSuccess;
}

int32_t GenericAmiApplicationImpl::InitTransportInfo()
{
    if (singleton_context_ != NULL && DoInitTransportInfo(singleton_context_) != aaf::ErrorCode::kSuccess)
    {
        return aaf::ErrorCode::kFailure;
    }

    if (ha_context_ != NULL && DoInitTransportInfo(ha_context_) != aaf::ErrorCode::kSuccess)
    {
        return aaf::ErrorCode::kFailure;
    }

    return aaf::ErrorCode::kSuccess;
}

void GenericAmiApplicationImpl::InitMasterEndpointInfo()
{
    if (singleton_context_ != NULL)
    {
        DoInitMasterEndpointInfo(singleton_context_);
    }

    if (ha_context_ != NULL)
    {
        DoInitMasterEndpointInfo(ha_context_);
    }
}

void GenericAmiApplicationImpl::DoInitMasterEndpointInfo(ami::Context* context)
{
    std::vector<std::string> endpoints;
    context->PropertyAt(ami::config::context::kMasterRxEndpoints)
        .GetValue(endpoints);
    for (auto& ep_name : endpoints)
    {
        master_rx_endpoints_.insert(ep_name);
    }

    endpoints.clear();
    context->PropertyAt(ami::config::context::kMasterTxEndpoints)
        .GetValue(endpoints);
    for (auto& ep_name : endpoints)
    {
        master_tx_endpoints_.insert(ep_name);
    }
}

void GenericAmiApplicationImpl::InitForwardEndpointInfo()
{
    if (singleton_context_ != NULL)
    {
        std::string ep_name;
        singleton_context_->PropertyAt(ami::config::context::kMasterToSlaveEndpoint)
            .GetValue(ep_name);
        if (!ep_name.empty())
            sg_master_slave_endpoints_.insert(ep_name);
    }

    if (ha_context_ != NULL)
    {
        std::string ep_name;
        ha_context_->PropertyAt(ami::config::context::kMasterToSlaveEndpoint)
            .GetValue(ep_name);
        if (!ep_name.empty())
            ha_master_slave_endpoints_.insert(ep_name);
    }
}

int32_t GenericAmiApplicationImpl::GetContextProperty(ami::Property& req_props,
                                                      ami::Property& resp_props)
{
    return ami::Context::GetContextProperty(req_props, resp_props);
}

int32_t GenericAmiApplicationImpl::InitContextProperty()
{
    is_disable_context_ = aaf_property_.GetValue(aaf::config::kIsDisableContext, false);
    if (is_disable_context_)
    {
        return ErrorCode::kSuccess;
    }

    std::string ha_context_name;
    std::string sg_context_name;
    try
    {
        ha_context_name = application_instance_->MakeHighAvailableContextName();
    }
    catch (...)
    {
        ADK_LOG_ERROR_AC_TF("exption catched. in function <MakeHighAvailableContextName>", "");
        return ErrorCode::kFailure;
    }

    try
    {
        sg_context_name = application_instance_->MakeSingletonContextName();
    }
    catch (...)
    {
        ADK_LOG_ERROR_AC_TF("exption catched. in function <MakeSingletonContextName>", "");
        return ErrorCode::kFailure;
    }

    std::string domain_server = aaf_property_.GetValue(aaf::config::kDomainServer,
                                                       "{localhost:2379}");

    ami::Property temp_props;
    temp_props.SetValue(ami::config::context::kName, ha_context_name);
    temp_props.SetValue(ami::config::context::kDomainServer, domain_server);
    if (!config_file_.empty())
    {
        temp_props.SetValue(ami::config::context::kConfigType, "File");
        temp_props.SetValue(ami::config::context::kConfigFilePath, config_file_);
        ADK_LOG_INFO_AC_TF("is using domain_server config file", "file: {1}", config_file_);
    }

    if (aaf_property_.GetValue(aaf::config::kEnableHighAvailableContext, false))
    {
        if (GetContextProperty(temp_props, ha_ctx_props_) == ami::ErrorCode::kSuccess)
        {
            if (!ha_ctx_props_.HasValue(aaf::config::kIsDisableFrameworkUse))
            {
                is_ha_ctx_props_valid_ = true;
            }
            else
            {
                if (!ha_ctx_props_.GetValue(aaf::config::kIsDisableFrameworkUse, false))
                {
                    is_ha_ctx_props_valid_ = true;
                }
                else
                {
                    is_ha_ctx_props_valid_ = false;
                    aaf_property_.SetValue(aaf::config::kEnableHighAvailableContext, "false");
                }
            }
        }
        else
        {
            ADK_LOG_ERROR_AC_TF("get context property failed, context_name <{1}>", ha_context_name);
            return ErrorCode::kFailure;
        }
    }

    if (aaf_property_.GetValue(aaf::config::kEnableSingletonContext, false))
    {
        temp_props.SetValue(ami::config::context::kName, sg_context_name);
        if (GetContextProperty(temp_props, sg_ctx_props_) == ami::ErrorCode::kSuccess)
        {
            if (!sg_ctx_props_.HasValue(aaf::config::kIsDisableFrameworkUse))
            {
                is_sg_ctx_props_valid_ = true;
            }
            else
            {
                if (!sg_ctx_props_.GetValue(aaf::config::kIsDisableFrameworkUse, false))
                {
                    is_sg_ctx_props_valid_ = true;
                }
                else
                {
                    is_sg_ctx_props_valid_ = false;
                    aaf_property_.SetValue(aaf::config::kEnableSingletonContext, "false");
                }
            }
        }
        else
        {
            ADK_LOG_ERROR_AC_TF("get context property failed, context_name <{1}>", sg_context_name);
            return ErrorCode::kFailure;
        }
    }

    // 查看是否在代码中指定该属性
    if (aaf_property_.GetValue(aaf::config::kIsSingletonLateJoinMcast, false))
    {
        is_sig_late_join_mcast_ = true;
    }

    // kIsSingletonLateJoinMcast仅支持单例和高可用context同时有效时使用
    if (!is_sg_ctx_props_valid_ || !is_ha_ctx_props_valid_)
    {
        is_sig_late_join_mcast_ = false;
    }

    // kIsSingletonLateJoinMcast不支持在灾备上使用
    if (GenericAmiApplication::is_disaster_backup())
    {
        is_sig_late_join_mcast_ = false;
    }

    return ErrorCode::kSuccess;
}

template<bool is_sg>
int32_t GenericAmiApplicationImpl::GetIsRecord(const std::string& ctx_name,
                                               const std::string& domain_server,
                                               bool& is_recourd)
{
    if (is_sg)
    {
        if (!is_sg_ctx_props_valid_)
            return ErrorCode::kFailure;

        is_recourd = sg_ctx_props_.GetValue(ami::config::context::kIsRecord, false);
    }
    else
    {
        if (!is_ha_ctx_props_valid_)
            return ErrorCode::kFailure;
        is_recourd = ha_ctx_props_.GetValue(ami::config::context::kIsRecord, false);
    }
    return aaf::ErrorCode::kSuccess;
}

int32_t GenericAmiApplicationImpl::AmiInit()
{
    bool has_singleton_ctx = aaf_property_.GetValue(config::kEnableSingletonContext, false);
    bool has_ha_ctx = aaf_property_.GetValue(config::kEnableHighAvailableContext, false);
    std::string domain_server = aaf_property_.GetValue(aaf::config::kDomainServer, "{localhost:2379}");
    std::string monitor_endpoint_name = aaf_property_.GetValue(config::kMonitorRequestEndpoint, kDefaultMonitorEndpointName);
    reserved_rxep_set_.insert(monitor_endpoint_name);

    ami::Context* ctx_temp = NULL;
    std::string ha_context_name;
    std::string sg_context_name;

    try
    {
        ha_context_name = application_instance_->MakeHighAvailableContextName();
    }
    catch (...)
    {
        ADK_LOG_ERROR_AC_TF("exption catched. in function <MakeHighAvailableContextName>", "");
        return ErrorCode::kFailure;
    }
    try
    {
        sg_context_name = application_instance_->MakeSingletonContextName();
    }
    catch (...)
    {
        ADK_LOG_ERROR_AC_TF("exption catched. in function <MakeSingletonContextName>", "");
        return ErrorCode::kFailure;
    }

    if (!is_disable_context_)
    {
        if (has_ha_ctx)
        {
            ami::Property ha_ctx_props;
            ha_ctx_props.SetValue(ami::config::context::kName, ha_context_name);
            ha_ctx_props.SetValue(ami::config::context::kDomainServer, domain_server);

            if (!config_file_.empty())
            {
                ha_ctx_props.SetValue(ami::config::context::kConfigType, "File");
                ha_ctx_props.SetValue(ami::config::context::kConfigFilePath, config_file_);
            }

            ha_ctx_props.SetValue(ami::config::context::kInitStatus,
                                  aaf_property_.GetValue(aaf::config::kHighAvailableInitStatus, ""));
            ha_ctx_props.SetValue(ami::config::context::kIsFollowerContext,
                                  aaf_property_.GetValue(aaf::config::kIsHighAvailableFollowerContext, "false"));
            if (sharding_num_ != 0)  // 只开启加强跟跑时，sharding_num_将会置为1
            {
                ha_ctx_props.SetValue(ami::config::context::kIsControlForwardWaitProcessDone, "true");
            }
            ha_ctx_props.SetValue(ami::config::context::recorder::kDataPath,
                                  recorder_data_path_);
            ha_ctx_props.SetValue(ami::config::context::kIsDisableIndicatorSave,
                                  true);
            ha_ctx_props.SetValue(ami::config::context::kDefaultLogDir,
                                  application_instance_->GetLogDir());

            //回调在ADA框架中注册的OnConfigureContextPropertyForAda函数，该函数用于配置初始化时创建的Context属性
            FunctionRegistry<OnConfigureContextPropertyFunc>::ForeachFunc(
                [&](OnConfigureContextPropertyFunc func) -> int32_t {
                    return func(ha_context_name, true, ha_ctx_props);
                });
            application_instance_->OnConfigureContextProperty(
                ha_context_name, true, ha_ctx_props);

            // FIXME: set tunning parameters!
            AppEventHandler& app_ev_hdl = *(new AppEventHandler());
            app_ev_hdl.event_hdl_ = boost::bind(&GenericAmiApplicationImpl::OnEvent, this, _1);

            do
            {
                ha_context_ = ami::Context::NewContext(app_ev_hdl, ha_ctx_props);
                if (ha_context_ == NULL)
                {
                    ADK_LOG_ERROR_AC_TF("create high available ami context failed", "context name <{1}>",
                                    ha_context_name);
                    return ErrorCode::kFailure;
                }
                ctx_temp = ha_context_;

                if (IsEnableAmiBypass())
                {
                    async_ami_executor_.msg_ = ha_context_->NewMessage(1);
                    async_ami_executor_.msg2_ = ha_context_->NewMessage(1);
                    async_ami_executor_.Start();
                    ADK_LOG_INFO_AC_TF("start AmiBypass", "asynchronous send msg with AmiBypass");
                }

                auto ec = BuildContextBinding(ha_context_, ha_context_binding_, ha_context_name);
                if (ec == ErrorCode::kTryAgain)
                {
                    ami::Context::DeleteContext(ha_context_);
                    ha_ctx_props.SetValue(ami::config::context::kInitStatus, "Rejoin");  // context 启动状态修改为 Rejoin 
                    aaf_property_.SetValue(aaf::config::kHighAvailableInitStatus, "Rejoin");  // aaf 框架启动状态修改为 Rejoin 
                    ADK_LOG_INFO_AC_TF("new context again", "try to new context with Rejoin status");
                    continue;
                }

                if (ec != ErrorCode::kSuccess)
                {
                    return ErrorCode::kFailure;
                }

                break;
            } while (true);
        }
        // else LOG INFO

        if (has_singleton_ctx)
        {
            ami::Property sig_ctx_props;
            sig_ctx_props.SetValue(ami::config::context::kName, sg_context_name);
            sig_ctx_props.SetValue(ami::config::context::kDomainServer, domain_server);

            if (!config_file_.empty())
            {
                sig_ctx_props.SetValue(ami::config::context::kConfigType, "File");
                sig_ctx_props.SetValue(ami::config::context::kConfigFilePath, config_file_);
            }

            sig_ctx_props.SetValue(ami::config::context::kInitStatus,
                                   aaf_property_.GetValue(aaf::config::kSingletonInitStatus, ""));
            sig_ctx_props.SetValue(ami::config::context::kIsFollowerContext,
                                   aaf_property_.GetValue(aaf::config::kIsSingletonFollowerContext, "false"));
            sig_ctx_props.SetValue(ami::config::context::recorder::kDataPath,
                                   recorder_data_path_);
            sig_ctx_props.SetValue(
                ami::config::context::kIsDisableIndicatorSave, true);
            sig_ctx_props.SetValue(ami::config::context::kDefaultLogDir,
                                   application_instance_->GetLogDir());
            sig_ctx_props.SetValue(ami::config::context::kIsLeaveStream, is_sig_late_join_mcast_);
            //回调在ADA框架中注册的OnConfigureContextPropertyForAda函数，该函数用于配置ADA初始化时创建的Context属性
            FunctionRegistry<OnConfigureContextPropertyFunc>::ForeachFunc(
                [&](OnConfigureContextPropertyFunc func) -> int32_t {
                    return func(sg_context_name, false, sig_ctx_props);
                });
            application_instance_->OnConfigureContextProperty(
                sg_context_name, false, sig_ctx_props);

            AppEventHandler& app_ev_hdl = *(new AppEventHandler());
            app_ev_hdl.event_hdl_       = boost::bind(&GenericAmiApplicationImpl::OnSingletonEvent, this, _1);
            singleton_context_          = ami::Context::NewContext(app_ev_hdl, sig_ctx_props);
            if (singleton_context_ == NULL)
            {
                ADK_LOG_ERROR_AC_TF("create singleton ami context failed", "context name <{1}>",
                                sg_context_name);
                return ErrorCode::kFailure;
            }
            ctx_temp = singleton_context_;

            if (BuildContextBinding(singleton_context_, singleton_context_binding_, sg_context_name) != ErrorCode::kSuccess)
            {
                return ErrorCode::kFailure;
            }

            sig_join_mcast_mutex_.lock();
            if (is_sig_enable_join_mcast_)
            {
                singleton_context_->JoinAllRxEndpoints();
                is_sig_late_join_mcast_ = false;
            }
            sig_join_mcast_mutex_.unlock();
        }
        // else LOG INFO

        if (ctx_temp == NULL)    // FIXME : check configuration error in same place!
        {
            ADK_LOG_ERROR_AC_TF("no ami context created", "");
            return ErrorCode::kFailure;
        }

        GenericAmiApplication::ha_context_ = ha_context_;
        GenericAmiApplication::singleton_context_ = singleton_context_;

        if (InitEndpointInfo(ha_context_name, sg_context_name) != aaf::ErrorCode::kSuccess)
        {
            return ErrorCode::kFailure;
        }

        if (InitStreamIDs(ha_context_name, sg_context_name) != aaf::ErrorCode::kSuccess)
        {
            return ErrorCode::kFailure;
        }

        if (InitTransportInfo() != aaf::ErrorCode::kSuccess)
        {
            return ErrorCode::kFailure;
        }

        InitMasterEndpointInfo();
    }
    else
    {
        ADK_LOG_INFO_AC_TF("Warning: application will not create context","");
    }

    {
        try
        {
            if (application_instance_->OnTxEndpointCreationBegin() != aaf::ErrorCode::kSuccess)
            {
                ADK_LOG_ERROR_AC_TF("OnTxEndpointCreationBegin return with failure", "");
                return ErrorCode::kFailure;
            }
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnTxEndpointCreationBegin>", "exception <{1}>",
                            boost::current_exception_diagnostic_information());
            return ErrorCode::kFailure;
        }

        if (CreateAllTxEndpoints(ha_context_name, sg_context_name) != ErrorCode::kSuccess)
        {
            return ErrorCode::kFailure;
        }
    }

    {
        try
        {
            if (application_instance_->OnRxEndpointCreationBegin() != aaf::ErrorCode::kSuccess)
            {
                ADK_LOG_ERROR_AC_TF("OnRxEndpointCreationBegin return with failure", "");
                return ErrorCode::kFailure;
            }
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("application throw exception in <OnRxEndpointCreationBegin>", "exception <{1}>",
                            boost::current_exception_diagnostic_information());
            return ErrorCode::kFailure;
        }

        if (CreateAllRxEndpoints(ha_context_name, sg_context_name) != ami::ErrorCode::kSuccess)
        {
            return ErrorCode::kFailure;
        }
    }

    if (!is_disable_context_ && aaf_property_.HasValue(aaf::config::kEnableMonitorRequest))
    {
        ami::Property monitor_ep_props;
        monitor_ep_props.SetValue(ami::config::endpoint::kName, monitor_endpoint_name);
        monitor_request_ep_ = ctx_temp->CreateRxEndpoint(*this, monitor_ep_props);
        if (monitor_request_ep_ == NULL)
        {
            ADK_LOG_ERROR_AC_TF("create monitor request endpoint failed", "endpoint name <{1}>",
                            monitor_endpoint_name);
            return ErrorCode::kFailure;
        }
        // ELSE LOG INFO
    }

    return ErrorCode::kSuccess;
}

void GenericAmiApplicationImpl::AmiTxExit()
{
    if (ha_context_ != NULL)
        ami::Context::DeleteContext(ha_context_);

    if (singleton_context_ != NULL)
        ami::Context::DeleteContext(singleton_context_);
}

int32_t GenericAmiApplicationImpl::IsHighAvailableBinding(const std::string& name, bool& is_ha)
{
    auto it = ha_context_binding_.find(name);
    if (it != ha_context_binding_.end())
    {
        is_ha = true;
        return ErrorCode::kSuccess;
    }

    it = singleton_context_binding_.find(name);
    if (it != singleton_context_binding_.end())
    {
        is_ha = false;
        return ErrorCode::kSuccess;
    }

    ADK_LOG_ERROR_AC_TF("context endpoint binding in-exist", "endpoint name <{1}>", name);
    return ErrorCode::kFailure;
}

ami::Context* GenericAmiApplicationImpl::GetBindingContext(const std::string& name, ami::MessageHandler** msg_handler)
{
    bool is_ha;
    if (IsHighAvailableBinding(name, is_ha) != ErrorCode::kSuccess)
    {
        return NULL;
    }

    if (is_ha)
    {
        if (msg_handler != NULL)
        {
            if (ami_mh_ha_ != nullptr)
                *msg_handler = ami_mh_ha_;
            else
                *msg_handler = &message_handler_;
        }
        return ha_context_;
    }

    if (msg_handler != NULL)
    {
        if (ami_mh_sg_ != nullptr)
            *msg_handler = ami_mh_sg_;
        else
            *msg_handler = &message_handler_singleton_;
    }
    return singleton_context_;
}

ami::RxEndpoint* GenericAmiApplicationImpl::GetRxEndpoint(const std::string& rx_ep_name)
{
    auto it = name_to_rxep_.find(rx_ep_name);
    if (it == name_to_rxep_.end())
        return NULL;
    return it->second;
}

void GenericAmiApplicationImpl::AddRxEndpoint(const std::string& rx_ep_name, ami::RxEndpoint* rx_ep)
{
    name_to_rxep_.insert(std::make_pair(rx_ep_name, rx_ep));
}

void GenericAmiApplicationImpl::ResetForDeleteAllContext()
{
    // AppHeartbeat                            app_hb_;
    name_to_ep_hdl_.clear();
    name_to_rxep_.clear();
    ha_context_binding_.clear();
    singleton_context_binding_.clear();
    // aaf_property_.Clear();   // 此处不清空, 因为在参数解析步骤中会Assign Follower等属性. 所以不清空,
    // domain_server_ = "";     // 从参数中解析得到. 先不清除, 默认切换前后使用相同的ds地址. 如果切换后需要使用不同的ds, 则需要通过 domain_server() 函数修改
    ha_context_ = nullptr;
    singleton_context_ = nullptr;
    monitor_context_ = nullptr;
    monitor_request_ep_ = nullptr;
    name_string_ = "";
    partition_no_ = 1;
    site_id_ = 1;
    replica_id_ = 11;
    // adk::IndicatorWriter                    indicator_writer_;
    // GenericAmiApplication*                  application_instance_;
    // MessageHandlerHighAvailable             message_handler_;                 // FIXME : cache alignment
    // MessageHandlerSingleton                 message_handler_singleton_;
    ami_mh_ha_ = nullptr;
    ami_mh_sg_ = nullptr;
    is_monitor_rx_stop_ = false;
    master_rx_hactx_endpoint_set_.clear();
    master_rx_sgctx_endpoint_set_.clear();
    total_rx_endpoint_set_.clear();
    rx_endpoint_set_.clear();
    rx_hactx_endpoint_set_.clear();
    rx_sgctx_endpoint_set_.clear();
    tx_endpoint_set_.clear();
    master_tx_hactx_endpoint_set_.clear();
    master_tx_sgctx_endpoint_set_.clear();
    total_tx_endpoint_set_.clear();
    tx_hactx_endpoint_set_.clear();
    tx_sgctx_endpoint_set_.clear();
    reserved_rxep_set_.clear();
    rx_stream_set_.clear();
    rx_hactx_stream_set_.clear();
    rx_sgctx_stream_set_.clear();
    tx_stream_set_.clear();
    tx_hactx_stream_set_.clear();
    tx_sgctx_stream_set_.clear();
    transport_infos_vec_.clear();
    master_rx_endpoints_.clear();
    master_tx_endpoints_.clear();
    ha_master_slave_endpoints_.clear();
    sg_master_slave_endpoints_.clear();

    ha_init_status_ = "Bootstrap";           // 初始化状态参数, 先默认重置. 应用可以在 OnConfigureFramework 中自行对context的启动property进行修改
    sig_init_status_ = "Bootstrap";
    // recorder_data_path_ = "";            // 不能clear, 因为aaf的recorder路径固定.
    // is_ha_follower_context_ = false;     // 参数解析得到, 暂不clear
    // is_sig_follower_context_ = false;    // 参数解析得到, 暂不clear

    ha_ctx_props_.Clear();
    is_ha_ctx_props_valid_ = false;
    is_sg_ctx_props_valid_ = false;
    sg_ctx_props_.Clear();
    // is_enable_sampling_ = false;         // 从参数解析得到, 不能重置
    // sampling_name_= "";                  // 从参数解析或根据规则从app_name生成.  TODO: 目前不支持白夜市切换后需要修改 sampling_name,
    is_disable_context_ = false;
}


static int32_t LoggerReadyHook(void* data)
{
    GenericAmiApplication* app_ins = (GenericAmiApplication*)data;
    if (app_ins == nullptr)
        return ErrorCode::kFailure;

    if (ami::g_application_logger != nullptr)
    {
        ami::g_application_logger->set_min_log_level([]()->ami::LogLevel_def{
            switch (g_aaf_log_level)
            {
                case ADK_LOG_LEVEL_TRACE:
                    return ami::LogLevel::kTrace;
                case ADK_LOG_LEVEL_DEBUG:
                    return ami::LogLevel::kDebug;
                case ADK_LOG_LEVEL_INFO:
                    return ami::LogLevel::kInfo;
                case ADK_LOG_LEVEL_WARN:
                    return ami::LogLevel::kWarn;
                case ADK_LOG_LEVEL_ERROR:
                    return ami::LogLevel::kError;
                case ADK_LOG_LEVEL_FATAL:
                    return ami::LogLevel::kFatal;
                default:
                    return ami::LogLevel::kInfo;
            }
        }());
    }

    return app_ins->OnLoggerReady();
}

GenericAmiApplication::GenericAmiApplication()
{
    ha_ctx_id_ = constant::kInvalidContextId;
    singleton_ctx_id_ = constant::kInvalidContextId;
    RegisterAAFInitHookLoggerReady(LoggerReadyHook, this);
}

GenericAmiApplication::GenericAmiApplication(bool flag) : GenericApplication(flag)
{
    ha_ctx_id_ = constant::kInvalidContextId;
    singleton_ctx_id_ = constant::kInvalidContextId;
    RegisterAAFInitHookLoggerReady(LoggerReadyHook, this);
}

static std::string GetLoginUserHome(const std::string substitute_path = "")
{
    struct passwd* pw = getpwuid(geteuid());
    if (pw == NULL)
    {
        return "";
    }
    return std::string(pw->pw_dir) + "/" + substitute_path;
}

const std::string& GenericAmiApplication::GetRecorderDataPath()
{
	return g_generic_ami_application_impl.recorder_data_path_;
}

void GenericAmiApplication::RegisterHighAvailableHandler(ami::MessageHandler* mh)
{
    g_generic_ami_application_impl.ami_mh_ha_ = mh;
}

void GenericAmiApplication::RegisterSingletonHandler(ami::MessageHandler* mh)
{
    g_generic_ami_application_impl.ami_mh_sg_ = mh;
}

AfPerformanceType GenericAmiApplication::GetArchforcePerformance()
{
    char* type = std::getenv("AF_PERFORMANCE");
    if (type != nullptr)
    {
        std::string af_performance = std::string(type);
        if (af_performance == "Standard")
        {
            return kStandard;
        }
        else if (af_performance == "HighThroughput")
        {
            return kHighThroughput;
        }
        else if (af_performance == "LowLatency")
        {
            return kLowLatency;
        }
        else if (af_performance == "LowUtilization")
        {
            return kLowUtilization;
        }
        else
        {
            ADK_LOG_WARN_AC_TF("invaid AF_PERFORMANCE value ",
                               "should be 'Standard' or 'HighThroughput' or 'LowLatency' or 'LowUtilization'");
            return kStandard;
        }
    }
    else
    {
        return kStandard;
    }
}

bool GenericAmiApplication::is_disaster_backup()
{
    return g_generic_ami_application_impl.is_disaster_backup(true)
           || g_generic_ami_application_impl.is_disaster_backup(false);
}

bool is_disaster_backup_inter(bool is_ha)
{
    return g_generic_ami_application_impl.is_disaster_backup(is_ha);
}

bool is_disaster_context_inter(bool is_ha)
{
    return g_generic_ami_application_impl.is_disaster_context(is_ha);
}

ami::Property GenericAmiApplication::ha_context_property()
{
    return g_generic_ami_application_impl.ha_ctx_props_;
}

void GenericAmiApplication::SetProgramOption()
{
    g_generic_ami_application_impl.recorder_data_path_ = GetLoginUserHome("recorder_data");
    if (g_generic_ami_application_impl.recorder_data_path_.empty())
    {
        ADK_LOG_ERROR_AC_TF("get login user home path failed", "");
        StopAmiApp();
        return;
    }

    AddOptionWithArgument<std::string>("domain-server", "the AMI domain server addresss", "{localhost:2379}");
    AddOptionWithArgument<std::string>("ds-config", "the AMI config file path; if use config file, domain-server will be invalid. example ~/config/xxx.config", "");
    AddOptionWithArgument<std::string>("init-status", "the init status of high available context, [Bootstrap,Recovery,Rejoin]", "Bootstrap");
    AddOptionWithArgument<std::string>("init-status-singleton", "the init status of singleton context, [Bootstrap,Recovery,Rejoin]", "Bootstrap");
    AAF_ADDOPT_ACCEPTOR("recorder-data-path", "set the recorder persistent data path",
                        g_generic_ami_application_impl.recorder_data_path_,
                        g_generic_ami_application_impl.recorder_data_path_);
    AddOption("enable-sampling", "enable sampling");
    AddOptionWithArgument<std::string>("sampling-name","the context name of Sampling","");
    AddOption("version", "display app and ami version");
    AddOption("is-follower-context", "specify the follower context");
    AddOption("is-follower-context-singleton", "specify the follower singleton context");
    AddOption("separate-log", "separate APP(AAF)log from AMI log");
    AddOption("enable-late-rejoin", "start with the Rejoin status when Bootstrap too late");
    AddOption("enable-advance-follower", "use advance follower");
    AddOptionWithArgument<int32_t>("sharding-num", "use multi sharding parallel, the number of application sharding", 0);
    AddOption("sig-late-join-mcast", "the singleton context join mcast group when ha context is ready");
    AddOption("leave-stream-on-exit", "rx endpoints leave steam on exit");
    // 新加启动参数时，请考虑是否需要在跟跑加强agent子进程中生效，如果需求请添加agent的启动参数

    try
    {
        SetAmiAppOption();
    }
    catch (...)
    {
        ADK_LOG_ERROR_AC_TF("application throw exception in <SetAmiAppOption>", "exception <{1}>",
                        boost::current_exception_diagnostic_information());
        StopAmiApp();
    }
}

void GenericAmiApplication::OnProgramOption(const std::string& option_name)
{
    if (option_name == "domain-server")
    {
        g_generic_ami_application_impl.domain_server_ = GetOptionArgument<std::string>("domain-server");
    }

    if (option_name == "ds-config")
    {
        g_generic_ami_application_impl.config_file_ = GetOptionArgument<std::string>("ds-config");
    }

    if (option_name == "init-status")
    {
        std::string option_val = GetOptionArgument<std::string>("init-status");
        if (option_val == "bootstrap" || option_val == "Bootstrap")
        {
            g_generic_ami_application_impl.ha_init_status_ = "Bootstrap";
        }
        else if (option_val == "recovery" || option_val == "Recovery")
        {
            g_generic_ami_application_impl.ha_init_status_ = "Recovery";
        }
        else if (option_val == "rejoin" || option_val == "Rejoin")
        {
            g_generic_ami_application_impl.ha_init_status_ = "Rejoin";
        }
        else
        {
            ADK_LOG_ERROR_AC_TF("set init-status option value error",
                    "should be <Bootstrap,bootstrap,Recovery,recovery,Rejoin,rejoin>");
            StopAmiApp();
        }
    }

    if (option_name == "init-status-singleton")
    {
        std::string option_val = GetOptionArgument<std::string>("init-status-singleton");
        if (option_val == "bootstrap" || option_val == "Bootstrap")
        {
            g_generic_ami_application_impl.sig_init_status_ = "Bootstrap";
        }
        else if (option_val == "recovery" || option_val == "Recovery")
        {
            g_generic_ami_application_impl.sig_init_status_ = "Recovery";
        }
        else if (option_val == "rejoin" || option_val == "Rejoin")
        {
            g_generic_ami_application_impl.ha_init_status_ = "Rejoin";
        }
        else
        {
            ADK_LOG_ERROR_AC_TF("set init-status-singleton option value error",
                    "should be <Bootstrap,bootstrap,Recovery,recovery,Rejoin,rejoin>");
            StopAmiApp();
        }
    }

    if (option_name == "is-follower-context-singleton")
    {
        g_generic_ami_application_impl.is_sig_follower_context_ = true;
    }

    if (option_name == "is-follower-context")
    {
        g_generic_ami_application_impl.is_ha_follower_context_ = true;
    }

    if (option_name == "enable-advance-follower")
    {
        g_generic_ami_application_impl.is_advance_follower_ = true;
    }

    if (option_name == "sharding-num")
    {
        g_generic_ami_application_impl.sharding_num_ = GetOptionArgument<int32_t>("sharding-num");
    }

    // Format<AMI_DS_OPS_START_TYPE>
    // ContextName1=InitStatus1,ContextName2=InitStatus2
    const char *ops_start_type_environ = getenv("AMI_DS_OPS_START_TYPE");
    if (ops_start_type_environ != nullptr)
    {
        std::vector<std::string> ops_init_status_vec;
        boost::split(ops_init_status_vec, ops_start_type_environ, boost::is_any_of(", "), boost::token_compress_on);
        for (auto& ops_init_status : ops_init_status_vec)
        {
            auto pos = ops_init_status.find("=");
            if (pos == ops_init_status.npos)
            {
                ADK_LOG_WARN_AC_TF("Invalid key:value in AMI_DS_OPS_START_TYPE environment variable",
                        "should be <ContextName1=InitStatus1,ContextName2=InitStatus2>");
                break;
            }
            std::string ctx_name(ops_init_status, 0, pos);
            std::string ops_status(ops_init_status, pos + 1, ops_init_status.size());
            if (ctx_name == MakeHighAvailableContextName())
            {
                g_generic_ami_application_impl.ha_init_status_ = ops_status;
            }
            else if (ctx_name == MakeSingletonContextName())
            {
                g_generic_ami_application_impl.sig_init_status_ = ops_status;
            }
        }
    }

    if (option_name == "enable-sampling")
    {
        g_generic_ami_application_impl.is_enable_sampling_ = true;
    }

    if (option_name == "sampling-name")
    {
        g_generic_ami_application_impl.sampling_name_ = GetOptionArgument<std::string>("sampling-name");
    }

    if (option_name == "version")
    {
        adk::ConfigFile conf_file;
        std::string app_version = conf_file.GetAppVersion();
        if (app_version == "")
        {
            app_version = GetAppVersion();
            if (app_version == "")
            {
                app_version = "unknown";
            }
        }
        std::string depend_ami_version = conf_file.GetAMIVersion();
        if (depend_ami_version == "")
        {
            depend_ami_version = GetAmiVersion();
            if (depend_ami_version == "")
            {
               depend_ami_version = "unknown";
            }
        }

        std::string current_ami_version = ami::GetVersionCode();
        std::string os_version = ami::GetOsVersion();
        std::string compiler_version = ami::GetComplilerVersion();
        std::string compile_date = ami::GetComplieDate();

        std::cout << "Copyright (c) 2018 Archforce Financial Technology, Inc." << "\n"
                  << "app version < " << app_version << " >\n"
                  << "dependencis:  " << "\n"
                  << "  ami version < " << depend_ami_version << " >\n"
                  << "runtime:  " << "\n"
                  << "  ami version      < " << current_ami_version  << " >\n"
                  << "  os version       < " << os_version  << " >\n"
                  << "  compiler version < " << compiler_version  << " >\n"
                  << "  compile date     < " << compile_date  << " >"
                  << std::endl;
        StopAmiApp();
    }

    if (option_name == "separate-log")
    {
        g_generic_ami_application_impl.is_separate_log_ = true;
    }

    if (option_name == "enable-late-rejoin")
    {
        g_generic_ami_application_impl.enable_late_rejoin_ = true;
    }

    if (option_name == "sig-late-join-mcast")
    {
        g_generic_ami_application_impl.is_sig_late_join_mcast_ = true;
    }

    if (option_name == "leave-stream-on-exit")
    {
        g_generic_ami_application_impl.is_leave_stream_onexit_ = true;
    }

    try
    {
        OnAmiAppOption(option_name);
    }
    catch (...)
    {
        ADK_LOG_ERROR_AC_TF("application throw exception in <OnAmiAppOption>", "exception <{1}>",
                        boost::current_exception_diagnostic_information());
        StopAmiApp();
    }
}

int32_t GenericAmiApplication::OnParseProgramOptionEnd()
{
    if (!g_generic_ami_application_impl.is_separate_log_)
    {
        ami::g_application_logger = new ami::AppLogger();
    }

    try
    {
        OnConfigureFramework(g_generic_ami_application_impl.aaf_property_);
    }
    catch (...)
    {
        ADK_LOG_ERROR_AC_TF("application throw exception in <OnConfigureFramework>", "exception <{1}>",
                        boost::current_exception_diagnostic_information());
        return ErrorCode::kFailure;
    }

    g_generic_ami_application_impl.set_application_instance(this);
    auto app_name = GetApplicationName();
    bool is_check_app_name = g_generic_ami_application_impl.aaf_property_.GetValue(aaf::config::kEnableAppNameCheck, true);
    if (is_check_app_name)
    {
        boost::regex regex("[A-Za-z]+_[0-9]+_[0-9]+_[0-9]+");
        if (!boost::regex_match(app_name, regex))
        {
            ADK_LOG_ERROR_AC_TF("application name is not valid", "application name: <{1}>, the valid application name "
                            "format is \"[A-Za-z]+_[0-9]+_[0-9]+_[0-9]+\"", app_name);
            return ErrorCode::kFailure;
        }

        std::vector<std::string> splits;
        boost::split(splits, app_name, boost::is_any_of("_"), boost::token_compress_on);
        g_generic_ami_application_impl.name_string_ = splits[0];
        g_generic_ami_application_impl.partition_no_ = boost::lexical_cast<uint32_t>(splits[1]);
        g_generic_ami_application_impl.site_id_ = boost::lexical_cast<uint32_t>(splits[2]);
        g_generic_ami_application_impl.replica_id_ = boost::lexical_cast<uint32_t>(splits[3]);
    }
    else
    {
        g_generic_ami_application_impl.name_string_ = app_name;
        g_generic_ami_application_impl.partition_no_ = 1;
        g_generic_ami_application_impl.site_id_ = 1;
        g_generic_ami_application_impl.replica_id_ = 11;
    }
    return ErrorCode::kSuccess;
}

EndpointHandler* GenericAmiApplication::CreateTxEndpoint(const std::string& tx_ep_name)
{
    boost::mutex::scoped_lock lock_guard(g_generic_ami_application_impl.fw_mutex_);

    auto it = g_generic_ami_application_impl.name_to_ep_hdl_.find(tx_ep_name);
    if (it != g_generic_ami_application_impl.name_to_ep_hdl_.end())
        return it->second;
    ADK_LOG_ERROR_AC_TF("get transmit endpoint failed", "endpoint <{1}>", tx_ep_name);
    return NULL;
}

int32_t  GenericAmiApplication::CreateRxEndpoint(const std::string& rx_ep_name)
{
    boost::mutex::scoped_lock lock_guard(g_generic_ami_application_impl.fw_mutex_);

    ami::RxEndpoint* ret_ep = g_generic_ami_application_impl.GetRxEndpoint(rx_ep_name);
    if (ret_ep != NULL)
    {
        return ErrorCode::kSuccess;
    }
    ADK_LOG_ERROR_AC_TF("get receive endpoint failed", "endpoint <{1}>", rx_ep_name);
    return ErrorCode::kFailure;
}

std::string GenericAmiApplication::GetInitStatus()
{
    return g_generic_ami_application_impl.ha_init_status_;
}

std::string GenericAmiApplication::GetSingletonInitStatus()
{
    return g_generic_ami_application_impl.sig_init_status_;
}

std::string GenericAmiApplication::GetNameString()
{
    return g_generic_ami_application_impl.name_string_;
}

uint32_t GenericAmiApplication::GetSiteID()
{
    return g_generic_ami_application_impl.site_id_;
}

uint32_t GenericAmiApplication::GetReplicaID()
{
    return g_generic_ami_application_impl.replica_id_;
}

uint32_t GenericAmiApplication::GetPartitionNo()
{
    return g_generic_ami_application_impl.partition_no_;
}

const TransportInfo* GenericAmiApplication::GetTransportInfo(int32_t id)
{
    if ((size_t)id >= g_generic_ami_application_impl.transport_infos_vec_.size())
    {
        return NULL;
    }

    return g_generic_ami_application_impl.transport_infos_vec_[id];
}

std::set<std::string>& GenericAmiApplication::GetRxEndpointSet()
{
    return g_generic_ami_application_impl.total_rx_endpoint_set_;
}

std::set<std::string>& GenericAmiApplication::GetTxEndpointSet()
{
    return g_generic_ami_application_impl.total_tx_endpoint_set_;
}

std::set<int32_t>& GenericAmiApplication::GetRxStreamIDs()
{
    return g_generic_ami_application_impl.rx_stream_set_;
}

std::set<int32_t>& GenericAmiApplication::GetTxStreamIDs()
{
    return g_generic_ami_application_impl.tx_stream_set_;
}

int32_t GenericAmiApplication::DoGetContextId()
{
    int32_t ctx_id = constant::kInvalidContextId;
    if (ha_context_ != NULL)
    {
        ha_context_->PropertyAt(ami::config::context::kId).GetValue(ctx_id);
    }

    return ctx_id;
}

int32_t GenericAmiApplication::DoGetSingletonContextId()
{
    int32_t ctx_id = constant::kInvalidContextId;
    if (singleton_context_ != NULL)
    {
        singleton_context_->PropertyAt(ami::config::context::kId).GetValue(ctx_id);
    }

    return ctx_id;
}

bool GenericAmiApplication::IsMasterRxEndpoint(const std::string& ep_name)
{
    return g_generic_ami_application_impl.master_rx_endpoints_.find(ep_name)
           != g_generic_ami_application_impl.master_rx_endpoints_.end();
}

int32_t GenericAmiApplication::GetRxEndpointPartitions(const std::string& ep_name, std::vector<int32_t>& partitions)
{
    ami::Context* context = nullptr;
    auto& rxep_list = g_generic_ami_application_impl.rx_endpoint_set_;
    if (rxep_list.find(ep_name) != rxep_list.end())
    {
        auto& rxep_sgctx_list = g_generic_ami_application_impl.rx_sgctx_endpoint_set_;
        if (rxep_sgctx_list.find(ep_name) != rxep_sgctx_list.end())
        {
            context = singleton_context_;
        }
        else
        {
            context = ha_context_;
        }

        if (context == nullptr)
        {
            ADK_LOG_ERROR_AC_TF("empty context reference", "");
            return ErrorCode::kFailure;
        }

        int32_t ec = context->PropertyAt(ami::config::context::kRxEndpoint, ep_name)
                                        (ami::config::endpoint::kPartitions).GetValue(partitions);
        if (ec != ami::ErrorCode::kSuccess)
        {
            return ErrorCode::kFailure;
        }

        return ErrorCode::kSuccess;
    }

    // 根据单例context的主进群列表（如果存在）里面的是否包含该主题作为判断依据
    auto& rxep_sgctx_master_list = g_generic_ami_application_impl.master_rx_sgctx_endpoint_set_;
    if (rxep_sgctx_master_list.find(ep_name) != rxep_sgctx_master_list.end())
    {
        context = singleton_context_;// 使用singleton_context
    }
    else
    {
        context = ha_context_;// 使用ha_context
    }

    if (context == nullptr)
    {
        ADK_LOG_ERROR_AC_TF("empty context reference", "");
        return ErrorCode::kFailure;
    }

    int32_t ec = context->PropertyAt(ami::config::context::kMasterRxEndpoint, ep_name)
                                                (ami::config::endpoint::kPartitions)
                                                .GetValue(partitions);
    if (ec == ami::ErrorCode::kSuccess)
    {
        return ErrorCode::kSuccess;
    }

    return ErrorCode::kFailure;
}

int32_t GenericAmiApplication::GetTxEndpointLBGs(const std::string& ep_name, std::vector<int32_t>& lb_groups)
{
	ami::Context* context = NULL;
	auto& txep_sgctx_list = g_generic_ami_application_impl.tx_sgctx_endpoint_set_;
	if (txep_sgctx_list.find(ep_name) != txep_sgctx_list.end())
	{
		context = singleton_context_;
	}
	else if (g_generic_ami_application_impl.tx_hactx_endpoint_set_.find(ep_name)
			 != g_generic_ami_application_impl.tx_hactx_endpoint_set_.end())
	{
		context = ha_context_;
	}

	if (context == NULL)
	{
		ADK_LOG_ERROR_AC_TF("empty context reference", "endpoint <{1}>", ep_name);
		return ErrorCode::kFailure;
	}

	int32_t ec = context->PropertyAt(ami::config::context::kTxEndpoint, ep_name)
									(ami::config::endpoint::kLoadBalanceGroups)
									.GetValue(lb_groups);
	if (ec != ami::ErrorCode::kSuccess)
	{
		return ErrorCode::kFailure;
	}

	return ErrorCode::kSuccess;
}

int32_t GenericAmiApplication::GetTxEndpointPartitions(const std::string& ep_name, std::vector<int32_t>& partitions)
{
    if (g_generic_ami_application_impl.sharding_agent_ != nullptr)
    {
        if (g_generic_ami_application_impl.sharding_agent_->GetProxy() == nullptr)
        {
            ADK_LOG_ERROR_AC_TF("proxy is nullptr, please GetTxEndpointPartitions after OnAmiInitBegin", "");
            return ErrorCode::kFailure;
        }
        return g_generic_ami_application_impl.sharding_agent_->GetProxy()->GetTxEndpointPartitions(ep_name, partitions);
    }

    ami::Context* context = nullptr;
    auto& txep_list = g_generic_ami_application_impl.tx_endpoint_set_;
    if (txep_list.find(ep_name) == txep_list.end())
    {
        // 根据单例context的主集群列表（如果存在）里面的是否包含该主题作为判断依据
        auto& txep_sg_master_ctx_list = g_generic_ami_application_impl.master_tx_sgctx_endpoint_set_;
        if (txep_sg_master_ctx_list.find(ep_name) != txep_sg_master_ctx_list.end())
        {
            context = singleton_context_;// 使用singleton_context
        }
        else
        {
            context = ha_context_;// 使用ha_context
        }

        if (context == nullptr)
        {
            ADK_LOG_ERROR_AC_TF("empty context reference", "");
            return ErrorCode::kFailure;
        }
        int32_t ec = context->PropertyAt(ami::config::context::kMasterTxEndpoint, ep_name)
                                                    (ami::config::endpoint::kPartitions)
                                                    .GetValue(partitions);
        if (ec == ami::ErrorCode::kSuccess)
        {
            return ErrorCode::kSuccess;
        }

        return ErrorCode::kFailure;
    }

    auto& txep_sgctx_list = g_generic_ami_application_impl.tx_sgctx_endpoint_set_;
    if (txep_sgctx_list.find(ep_name) != txep_sgctx_list.end())
    {
        context = singleton_context_;
    }
    else
    {
        context = ha_context_;
    }

    if (context == NULL)
    {
        ADK_LOG_ERROR_AC_TF("empty context reference", "");
        return ErrorCode::kFailure;
    }

    int32_t ec = context->PropertyAt(ami::config::context::kTxEndpoint, ep_name)
                                    (ami::config::endpoint::kPartitions).GetValue(partitions);
    if (ec != ami::ErrorCode::kSuccess)
    {
        return ErrorCode::kFailure;
    }

    return ErrorCode::kSuccess;
}

ami::Property& GenericAmiApplication::MutableFrameworkConfiguration()
{
    return g_generic_ami_application_impl.aaf_property_;
}

void GenericAmiApplication::OnAAFSingnal(int sig_num, int value)
{}

std::string GenericAmiApplication::MakeHighAvailableContextName()
{
    return GetApplicationName();
}

std::string GenericAmiApplication::MakeSingletonContextName()
{
    return MakeHighAvailableContextName() + "_Singleton";
}

template<typename T>
void AssignProperty(const std::string& prop_name, const T& value)
{
    if (!g_generic_ami_application_impl.aaf_property_.HasValue(prop_name))
    {
        g_generic_ami_application_impl.aaf_property_.SetValue(prop_name, value);
    }
}

int32_t GenericAmiApplication::OnDaemonizeEnd()
{
    // FIXME: get application configuration from etcd server

    // if kDomainServer is not configured, we use the program option setting.
    if (!g_generic_ami_application_impl.aaf_property_.HasValue(aaf::config::kDomainServer))
    {
        g_generic_ami_application_impl.aaf_property_.SetValue(aaf::config::kDomainServer,
                                                              g_generic_ami_application_impl.domain_server_);
    }

    if (g_generic_ami_application_impl.is_enable_sampling_)
    {
        if (g_generic_ami_application_impl.sampling_name_.empty())
        {
            g_generic_ami_application_impl.sampling_name_ = GetApplicationName() + "_se";
        }

        ami::Property props;
        props.SetValues()
            (ami::config::sampling::kContextName, g_generic_ami_application_impl.sampling_name_)
            (ami::config::sampling::kDomainServer, g_generic_ami_application_impl.domain_server_);
        int32_t ec = ami::SamplingEngine::Start(props);
        if (ec != ErrorCode::kSuccess)
        {
            ADK_LOG_WARN_AC_TF("start SamplingEngine failed","");
        }
		return ErrorCode::kSuccess;
    }

    AssignProperty(aaf::config::kHighAvailableInitStatus,
                   g_generic_ami_application_impl.ha_init_status_);
    AssignProperty(aaf::config::kSingletonInitStatus,
                   g_generic_ami_application_impl.sig_init_status_);

    AssignProperty(aaf::config::kIsHighAvailableFollowerContext,
                   g_generic_ami_application_impl.is_ha_follower_context_);
    AssignProperty(aaf::config::kIsSingletonFollowerContext,
                   g_generic_ami_application_impl.is_sig_follower_context_);


    // FIXME: error check here!

    return ErrorCode::kSuccess;
}

int32_t GenericAmiApplication::OnLogInit(std::string& log_dir, std::string& log_name)
{
    log_name = GetApplicationName();
    log_dir = g_generic_ami_application_impl.aaf_property_.GetValue(config::kLogDirPath, "");
    if (log_dir.empty())
    {
        struct passwd* pw = getpwuid(geteuid());
        if (pw == NULL)
        {
            ADK_LOG_ERROR_AC_TF("get login user name failed", "errno <{1}>, desc <{2}>",
                            errno, strerror(errno));
            return ErrorCode::kFailure;
        }
        log_dir = std::string(pw->pw_dir) + "/log/";
    }
    return ErrorCode::kSuccess;
}

int32_t GenericAmiApplication::SetSingletonLockFileDirectory(std::string& file_path)
{
    file_path = g_generic_ami_application_impl.aaf_property_.GetValue(config::kLockDirPath, "");
    if (file_path.empty())
    {
        struct passwd* pw = getpwuid(geteuid());
        if (pw == NULL)
        {
            ADK_LOG_ERROR_AC_TF("get login user name failed", "errno <{1}>, desc <{2}>",
                              errno, strerror(errno));
            return ErrorCode::kFailure;
        }
        file_path = std::string(pw->pw_dir) + "/lock/";
    }
    return ErrorCode::kSuccess;
}

extern void SetTidFilePath(const std::string& file_path);
void GenericAmiApplicationImpl::SaveSelfMemoryMap()
{
    if (std::getenv("AAF_GEN_TID_FILE_COREDUMP") != nullptr)
    {
        std::ifstream map_file("/proc/self/maps");
        std::string follower_mode_info_file_path = recorder_data_path_
            + "/core_info/" + std::to_string(getpid());
        boost::system::error_code ec;
        boost::filesystem::create_directories(follower_mode_info_file_path, ec);
        std::ofstream new_map_file(follower_mode_info_file_path + "/maps");
        std::string line_content;
        while (map_file.good())
        {
            std::getline(map_file, line_content);
            new_map_file << line_content << std::endl;
        }

        SetTidFilePath(follower_mode_info_file_path);
    }
}

int32_t GenericAmiApplicationImpl::CheckAmiBypassValid()
{
    // for ami-bypass
    const char* env_str = std::getenv("AAF_TE_LOOPBACK_TOPIC");
    if (env_str != nullptr)
    {
        te_loopback_topic_name_ = env_str;
        if (!IsTeHookMessageHandler() && !IsQeHookMessageHandler())
        {
            ADK_LOG_ERROR_AC_TF("miss environment",
                                "AAF_IS_TE_HOOK_MESSAGE_HANDLER or AAF_IS_QE_HOOK_MESSAGE_HANDLER environment must be set");
            return ErrorCode::kFailure;
        }
        ADK_LOG_INFO_AC_TF("enable AmiBypass", "current te loopback topic name <{1}>, "
                           "AAF_IS_TE_LEADER: <{2}>, "
                           "AAF_IS_TE_HOOK_MESSAGE_HANDLER: <{3}>, "
                           "AAF_IS_QE_HOOK_MESSAGE_HANDLER: <{4}>.", 
                           te_loopback_topic_name_,
                           IsTeLeader(),
                           IsTeHookMessageHandler(),
                           IsQeHookMessageHandler());
    }
    return ErrorCode::kSuccess;
}

void GenericAmiApplicationImpl::AmiRxLeaveStreams()
{
    if (!is_leave_stream_onexit_)
    {
        return;
    }

    if (singleton_context_ != nullptr)
    {
        singleton_context_->LeaveAllRxEndpoints();
    }

    if (ha_context_ != nullptr)
    {
        ha_context_->LeaveAllRxEndpoints();
    }

    usleep(100);
}

int32_t GenericAmiApplication::GetShardingNum()
{
    return g_generic_ami_application_impl.sharding_num_;
}

int32_t GenericAmiApplication::OnInit()
{
    if (g_generic_ami_application_impl.InitContextProperty() != ErrorCode::kSuccess)
        return ErrorCode::kFailure;

    if (g_generic_ami_application_impl.is_sg_ctx_props_valid_
        && g_generic_ami_application_impl.is_advance_follower_)
    {
        ADK_LOG_ERROR_AC_TF("Advance Follower Invalid Args",
                            "advance follower can not use with singleton context");
        return ErrorCode::kFailure;
    }

    if (g_generic_ami_application_impl.is_advance_follower_
        && g_generic_ami_application_impl.sharding_num_ == 0)
    {
        g_generic_ami_application_impl.sharding_num_ = 1;
    }

    if (g_generic_ami_application_impl.sharding_num_
        && g_generic_ami_application_impl.sharding_agent_ == nullptr)  // 防止递归进入
    {
        adk::pipeline::sharding::set_is_sharding(true);
        ADK_LOG_INFO_AC_TF("multi sharding Init ",
                           "context <{1}> is init as ShardingAgent",
                           GetApplicationName());
        auto* sharding_agent = new sharding::ShardingAgent();
        g_generic_ami_application_impl.sharding_agent_ = sharding_agent;

        // InitAgent 中会fork 分片进程，所以提前准备虚拟对象
        if (g_generic_ami_application_impl.is_ha_ctx_props_valid_)
        {
            g_generic_ami_application_impl.ha_context_ = (ami::Context*)(new sharding::DummyContextImpl());
            GenericAmiApplication::ha_context_ = g_generic_ami_application_impl.ha_context_;
        }

        if (g_generic_ami_application_impl.is_sg_ctx_props_valid_)
        {
            g_generic_ami_application_impl.singleton_context_ = (ami::Context*)(new sharding::DummyContextImpl());
            GenericAmiApplication::singleton_context_ =
                g_generic_ami_application_impl.singleton_context_;
        }

        auto ec = sharding_agent->InitAgent(this,
                                           g_generic_ami_application_impl.ha_ctx_props_,
                                           g_generic_ami_application_impl.sg_ctx_props_);  
                                           // this 就是应用的实例，传递给 sharding_agent，后续调用接口
        if (ec != aaf::ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("ShardingAgent init failed",
                                "begin to exitting");
            return ec;
        }

        // 修改 application_instance_ 指向 sharding_agent 
        g_generic_ami_application_impl.set_application_instance(sharding_agent);

        // 然后重新调用GenericAmiApplication::OnInit函数，
        // 此时后续调用 OnAmiInitBegin 就会进入 ShardingAgent::OnAmiInitBegin 实现劫持效果
        return sharding_agent->OnInit();
    }

    GenericAmiApplication* app_inst = this;
    if (g_generic_ami_application_impl.sharding_agent_)
    {
        app_inst = g_generic_ami_application_impl.sharding_agent_->aaf_instance();
    }

    try
    {
        if (app_inst->OnFrameworkInitBegin() != ErrorCode::kSuccess)
        {
            return ErrorCode::kFailure;
        }
    }
    catch (...)
    {
        ADK_LOG_ERROR_AC_TF("application throw exception in <OnFrameworkInitBegin>",
                            "exception <{1}>",
                            boost::current_exception_diagnostic_information());
        return ErrorCode::kFailure;
    }

    if (g_generic_ami_application_impl.sharding_agent_)
    {
        auto ec = g_generic_ami_application_impl.sharding_agent_->StartProxy();
        if (ec != aaf::ErrorCode::kSuccess)
        {
            return ec;
        }
    }

    try
    {
        //回调在ADA框架中注册的OnAmiInitBeginForAda函数，该函数用于完成ADA需要在AMI初始化前完成的动作
        if (FunctionRegistry<OnAmiInitBeginFunc>::ForeachFunc(
                [](OnAmiInitBeginFunc func) -> int32_t { return func(); })
            != ErrorCode::kSuccess)
        {
            return ErrorCode::kFailure;
        }
        if (OnAmiInitBegin() != ErrorCode::kSuccess)
        {
            return ErrorCode::kFailure;
        }
    }
    catch (...)
    {
        ADK_LOG_ERROR_AC_TF("application throw exception in <OnAmiInitBegin>",
                            "exception <{1}>",
                            boost::current_exception_diagnostic_information());
        return ErrorCode::kFailure;
    }

    if (!is_running())
        return ErrorCode::kSuccess;

    // 在MonitorInit之前, 检查AmiBypass功能相关配置是否合法, 用于后续判断是否开启Bypass功能指标收集
    if (g_generic_ami_application_impl.CheckAmiBypassValid() != ErrorCode::kSuccess)
    {
        return ErrorCode::kFailure;
    }

    // FIXME: start and suspend monitor thread!
    if (g_generic_ami_application_impl.MonitorInit() != ErrorCode::kSuccess)
    {
        return ErrorCode::kFailure;
    }

    if (g_generic_ami_application_impl.AmiInit() != ErrorCode::kSuccess)
    {
        return ErrorCode::kFailure;
    }

    // keep SaveSelfMemoryMap after AmiInit
    // save memory map info, used by follower
    g_generic_ami_application_impl.SaveSelfMemoryMap();

    if (g_generic_ami_application_impl.AutoEndpointCreation() != ErrorCode::kSuccess)
    {
        return ErrorCode::kFailure;
    }

    try
    {
        if (OnAmiInitEnd() != ErrorCode::kSuccess)
        {
            return ErrorCode::kFailure;
        }
    }
    catch (...)
    {
        ADK_LOG_ERROR_AC_TF("application throw exception in <OnAmiInitEnd>",
                            "exception <{1}>",
                            boost::current_exception_diagnostic_information());
        return ErrorCode::kFailure;
    }

    if (!is_running())
        return ErrorCode::kSuccess;

    return ErrorCode::kSuccess;
}

void GenericAmiApplication::OnSignal(int sig_num, int value)
{
    if (g_generic_ami_application_impl.sharding_agent_)
    {
        g_generic_ami_application_impl.sharding_agent_->OnSignal(sig_num, value);
    }

    if (g_generic_ami_application_impl.is_leave_stream_onexit_ &&
        (sig_num == SIGTERM || sig_num == SIGINT || sig_num == SIGQUIT))
    {
        GenericApplication::Stop();
    }
    else
    {
        GenericApplication::OnSignal(sig_num, value);
        // to set is_default_signalhdl_ = true
    }
}

void GenericAmiApplication::OnExit()
{
    ExitByEnv("AAF_DIRECT_EXIT", [&]() {
        g_generic_ami_application_impl.AmiRxLeaveStreams();
        ADK_LOG_INFO_AC_TF("aaf do exit", "aaf _exit without doing clear jobs");
    });

    if (g_generic_ami_application_impl.is_advance_follower_)
    {
        return;
    }

    try
    {
        OnAmiExitBegin();
        //回调在ADA框架中注册的OnAmiExitEndForAda函数，AMI退出之前ADA的最后一次备份工作可在该接口内完成
        FunctionRegistry<OnAmiExitEndFunc, 2>::ForeachFunc(
            [](OnAmiExitEndFunc func) -> int32_t {
                func();
                return ErrorCode::kSuccess;
            });
    }
    catch (...)
    {
        ADK_LOG_ERROR_AC_TF("application throw exception in <OnAmiExitBegin>",
                            "exception <{1}>",
                            boost::current_exception_diagnostic_information());
    }

    g_generic_ami_application_impl.AmiRxLeaveStreams();

    g_generic_ami_application_impl.MonitorExit();    // FIXME: supply method to unregister object !
                                                     // stop Monitor first to prevent app coredump
    g_generic_ami_application_impl.AmiRxExit();

    try
    {
        OnAmiRxExitEnd();
    }
    catch (...)
    {
        ADK_LOG_ERROR_AC_TF("application throw exception in <OnAmiRxExitEnd>", "exception <{1}>",
                        boost::current_exception_diagnostic_information());
    }

    g_generic_ami_application_impl.AmiTxExit();

    try
    {
        OnAmiExitEnd();
    }
    catch (...)
    {
        ADK_LOG_ERROR_AC_TF("application throw exception in <OnAmiExitEnd>", "exception <{1}>",
                        boost::current_exception_diagnostic_information());
    }
}

bool GenericAmiApplication::IsAdvanceFollower()
{
    return g_generic_ami_application_impl.is_advance_follower_;
}

int32_t GenericAmiApplication::DeleteAllContext(uint32_t timeout_milli)
{
    int32_t ec = ami::Context::DeleteAllContext(timeout_milli);
    if (ec != ErrorCode::kSuccess)
        return ec;

    /// 0. DeleteAllContext成功, 底层已删除context指针. 故原有context指针不再可用
    ///    在此处先置为空, 避免OnExit中还是按正常指针调用.
    g_generic_ami_application_impl.ha_context_ = nullptr;
    g_generic_ami_application_impl.singleton_context_ = nullptr;

    /// 1. 先退出上一场的资源
    OnExit();

    /// 2. 清空aaf框架中缓存的资源
    g_generic_ami_application_impl.ResetForDeleteAllContext();

    return ErrorCode::kSuccess;
}

int32_t GenericAmiApplication::CreateAllContext()
{
    int32_t ec = ErrorCode::kFailure;

    /// 3. 如果白夜市切换需要更新框架参数,OnConfigureFramework
    ec = OnParseProgramOptionEnd();
    if (ec != ErrorCode::kSuccess)
        return ec;

    /// 4. 进行初始化工作. context\Rx\Tx 创建以及相应回调函数调用
    ec = OnInit();
    if (ec != ErrorCode::kSuccess)
        return ec;

    return ErrorCode::kSuccess;
}

std::string& GenericAmiApplication::domain_server()
{
    return g_generic_ami_application_impl.domain_server_;
}

int32_t GenericAmiApplication::GetShardingIndex()
{
    if (g_generic_ami_application_impl.sharding_agent_)
    {
        return g_generic_ami_application_impl.sharding_agent_->sharding_index();
    }
    return 0;
}

ami::Context*    GenericAmiApplication::ha_context_;
ami::Context*    GenericAmiApplication::singleton_context_;

}  // aaf
