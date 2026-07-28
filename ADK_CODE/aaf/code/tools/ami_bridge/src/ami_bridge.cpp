/**
 * @author 牛亮亮(niuliangliang@af.local)
 */

/// cpp std
#include <fstream>
#include <system_error>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
/// boost
//#include <boost/filesystem.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

///< adk, ami public
#include <adk/arch/generic.h>
#include <adk/monitor/monitor.h>
#include <ami/ami_rx_record_channel.h>

/// ami impl
// #include "ami_constant.h"
#include "util.h"
// #include "../../../src/context_impl.h"
#include "../../../../../adk/code/src/io_engine/last_error.h"

///< impl
#include "ami_bridge.h"
#include "bridge_msg.h"  //AmiMsgPacket
#include "ami_bridge_default_config.h"
#include "bridge_tcp_handler.h"

#undef LOG
#undef LOG_DECLARE
#undef LOG_DEFINE
#undef LOG_LOCAL
#undef LOG_TRACE
#undef LOG_DEBUG
#undef LOG_INFO
#undef LOG_WARN
#undef LOG_ERROR
#undef LOG_FATAL

#ifdef IF_ERR_RET
#undef IF_ERR_RET
#endif
#define IF_ERR_RET(x, ...)        \
    if (std::error_code ec = (x)) \
    {                             \
        __VA_ARGS__;              \
        return ec;                \
    }

#ifdef IF_ERR_RET_AAF
#undef IF_ERR_RET_AAF
#endif
#define IF_ERR_RET_AAF(x, ...)    \
    if (std::error_code ec = (x)) \
    {                             \
        __VA_ARGS__;              \
        return aaf::kFailure;     \
    }

namespace ami
{
namespace bridge
{

namespace ba = boost::asio;
namespace bs = boost::system;
namespace bf = boost::filesystem;
namespace bt = boost::property_tree;
namespace cb = config::bridge;

ADK_LOG_DEFINE(ami::bridge::AmiBridge);

bool AmiBridge::s_is_low_latency = false;

static const std::string kEndpointInfo("EndpointInfo");
static const std::string kEndpointName("EndpointName");
static const std::string kEndpointId("EndpointId");
static const std::string kPartitions("Partitions");

/****************************************************
 *  self-defining function of class AmiBridge
 */

std::error_code AmiBridge::Init()
{
    config_agent_.reset(new ConfigAgent());

    Property config_agent_property;
    config_agent_property.SetValue(
        config::context::kDomainServer, domain_server_);

    if (config_agent_->Init(config_agent_property) == false)
    {
        ADK_LOG_ERROR_AC_TF("config agent init fail", "");
        return kConfigError;
    }

    // 获取Bridge的配置
    Property bridge_property;
    if (kSuccess != config_agent_->GetBridgeConfig(app_name_, &bridge_property))
    {
        ADK_LOG_ERROR_AC_TF("get bridge config fail", "");
        return kConfigError;
    }

    if (!bridge_property.HasValue(cb::kContextName))
    {
        ADK_LOG_ERROR_AC_TF("no config item", "need config item: <{1}>", cb::kContextName);
        return kConfigError;
    }
    context_name_ = bridge_property.GetStringValue(cb::kContextName);

    if (!bridge_property.HasValue(cb::kTierChannelName))
    {
        ADK_LOG_ERROR_AC_TF("no config item", "need config item: <{1}>", cb::kTierChannelName);
        return kConfigError;
    }
    tier_channel_name_ = bridge_property.GetStringValue(cb::kTierChannelName);

    if (!bridge_property.HasValue(cb::kIsServer))
    {
        ADK_LOG_ERROR_AC_TF("no config item", "need config item: <{1}>", cb::kIsServer);
        return kConfigError;
    }
    is_server_ = bridge_property.GetBoolValue(cb::kIsServer);

    if (is_server_)
    {
        if (!bridge_property.HasValue(cb::kListenIp)
            || !bridge_property.HasValue(cb::kListenPort))
        {
            ADK_LOG_ERROR_AC_TF("no config item as server peer",
                                "need config item: <{1}>, <{2}>",
                                cb::kListenIp, cb::kListenPort);
            return kConfigError;
        }

        self_ip_   = bridge_property.GetStringValue(cb::kListenIp);
        self_port_ = bridge_property.GetIntValue(cb::kListenPort);
    }
    else
    {
        if (!bridge_property.HasValue(cb::kPeerAddrs))
        {
            ADK_LOG_ERROR_AC_TF("no config item as client peer",
                                "need config item: <{1}> ",
                                cb::kPeerAddrs);
            return kConfigError;
        }
        std::vector<Property> peer_addrs_;
        peer_addrs_ = bridge_property.GetValue(cb::kPeerAddrs, peer_addrs_);
        for (auto addr : peer_addrs_)
        {
            peer_ip_.push_back(addr.GetValue("Ip", ""));
            peer_port_.push_back(addr.GetValue("Port", 0));
        }

        if (bridge_property.HasValue(cb::kLocalPort))
        {
            local_port_ = bridge_property.GetIntValue(cb::kLocalPort);
        }
    }

    if (bridge_property.HasValue(cb::kIsLowLatency))
    {
        s_is_low_latency = bridge_property.GetValue(cb::kIsLowLatency, false);
        if (s_is_low_latency)
        {
            bridge_property.SetValue(cb::kTcpNoDely, true);
        }
        ADK_LOG_INFO_AC_TF("bridge performance", "kIsLowLatency <{1}>", s_is_low_latency);
    }

    if (bridge_property.HasValue(cb::kTcpNoDely))
    {
        is_tcp_nodelay_ = bridge_property.GetBoolValue(cb::kTcpNoDely);
    }

    if (bridge_property.HasValue(cb::kSnapshotCycleMilli))
    {
        snapshot_cycle_milli_ = bridge_property.GetValue(
            cb::kSnapshotCycleMilli,
            decltype(snapshot_cycle_milli_)::value_type());

        if (*snapshot_cycle_milli_ > 0 && *snapshot_cycle_milli_ < 1000)
        {
            snapshot_cycle_milli_ = 1000u;
        }
    }
    else
    {
        snapshot_cycle_milli_ = cdv::kSnapshotCycleMilli;
    }

    if (bridge_property.HasValue(cb::kCompressLibPath))
    {
        compress_lib_path_ = bf::path(
            bridge_property.GetStringValue(
                cb::kCompressLibPath));
    }

    if (bridge_property.HasValue(cb::kCompressConfigStr))
    {
        compress_config_ = bridge_property.GetStringValue(
            cb::kCompressConfigStr);
    }

    tcp_heartbeat_inv_milli_ = bridge_property.GetValue(
        cb::kTcpHeartbeatInvMilli, cdv::kTcpHeartbeatInvMilli);
    if (tcp_heartbeat_inv_milli_ < cdv::kTcpHeartbeatInvMilliMin)
    {
        ADK_LOG_WARN_AC_TF("Invalid property", "set <{1}> to <{2}> < min <{3}>", cb::kTcpHeartbeatInvMilli, tcp_heartbeat_inv_milli_, cdv::kTcpHeartbeatInvMilliMin);
        tcp_heartbeat_inv_milli_ = cdv::kTcpHeartbeatInvMilliMin;
    }

    tcp_timeout_multiplier_ = bridge_property.GetValue(
        cb::kTcpTimeoutMultiplier, cdv::kTcpTimeoutMultiplier);
    if (tcp_timeout_multiplier_ < cdv::kTcpTimeoutMultiplierMin)
    {
        ADK_LOG_WARN_AC_TF("Invalid property", "set <{1}> to <{2}> < min <{3}>", cb::kTcpTimeoutMultiplier, tcp_timeout_multiplier_, cdv::kTcpTimeoutMultiplierMin);
        tcp_timeout_multiplier_ = cdv::kTcpTimeoutMultiplierMin;
    }

    ami_rx_queue_len_ = bridge_property.GetValue(
        cb::kAmiRxMsgQLen, cdv::kAmiRxMsgQLenMin);
    if (ami_rx_queue_len_ < cdv::kAmiRxMsgQLenMin)
    {
        ADK_LOG_WARN_AC_TF("Invalid property", "set <{1}> to <{2}> < min <{3}>", cb::kAmiRxMsgQLen, ami_rx_queue_len_, cdv::kAmiRxMsgQLenMin);
        ami_rx_queue_len_ = cdv::kAmiRxMsgQLenMin;
    }

    // packets_record_QLen_ = bridge_property.GetValue(
    //     cb::kPacketsRecordQLen, cdv::kPacketsRecordQLenMin);
    // if(packets_record_QLen_ < cdv::kPacketsRecordQLenMin)
    // {
    //     ADK_LOG_WARN_AC_TF("Invalid property", "set '{1}' to '{2}' < min '{3}'",
    //                          cb::kPacketsRecordQLen, packets_record_QLen_,
    //                           cdv::kPacketsRecordQLenMin);
    //     packets_record_QLen_ = cdv::kPacketsRecordQLenMin;
    // }
    ack_step_len_ = bridge_property.GetValue(
        cb::kTcpAckCnt, cdv::kTcpAckCntMax);
    if (ack_step_len_ > cdv::kTcpAckCntMax)
    {
        ADK_LOG_WARN_AC_TF("Invalid property", "set <{1}> to <{2}> > max <{3}>", 
                            cb::kTcpAckCnt, ack_step_len_, cdv::kTcpAckCntMax);
        ack_step_len_ = cdv::kTcpAckCntMax;
    }

    if (bridge_property.HasValue(cb::kRateControl))
    {
        Property rate_ctrl_info;
        rate_ctrl_info = bridge_property.GetValue(cb::kRateControl, rate_ctrl_info);

        int32_t net_bandwidth   = rate_ctrl_info.GetIntValue(cb::kNetBandWidth);  //Mbps
        net_bandwidth_permicro_ = net_bandwidth;                                  //bpus
        ratectl_inv_micro_      = rate_ctrl_info.GetIntValue(cb::kRateCtlInvMicro);
        if (net_bandwidth_permicro_ == 0 || ratectl_inv_micro_ == 0)
        {
            ADK_LOG_WARN_AC_TF("Invalid property", "need config item: <{1}> <{2}>", 
                                cb::kNetBandWidth, cb::kRateCtlInvMicro);
        }
        else
        {
            is_rate_ctl_   = true;
            fragment_size_ = (net_bandwidth_permicro_ * ratectl_inv_micro_) / 8;
            ADK_LOG_INFO_AC_TF("Rate Control info: ", 
                                "control interval <{1}> us, fragment_size <{2}> Bytes",
                                ratectl_inv_micro_, fragment_size_);
        }
    }

    return kSuccess;
}

std::error_code AmiBridge::InitTcpEndpoint()
{
    if (!is_server_)
    {
        //初始化client
        static int index = 0;
        int server_num   = peer_ip_.size();
        if (server_num == 0)
        {
            ADK_LOG_ERROR_AC_TF("peer addrs num is zero", "");
            StopAmiApp();
        }
        std::string peer_ip      = peer_ip_[index % server_num];
        unsigned short peer_port = peer_port_[index % server_num];

        adk::io_engine::Property client_props;
        client_props(adk::io_engine::config::endpoint::kRemoteIp, peer_ip)
                    (adk::io_engine::config::endpoint::kRemotePort, peer_port)
                    (adk::io_engine::config::endpoint::kLocalPort, local_port_)
                    (adk::io_engine::config::endpoint::kIsSingleton, false)
                    (adk::io_engine::config::endpoint::kHeartbeatTimeoutMilli,
                        (tcp_heartbeat_inv_milli_ * tcp_timeout_multiplier_))
                    (adk::io_engine::config::endpoint::kEventHandler, event_handler_)
                    (adk::io_engine::config::endpoint::kConnectHandler, connect_handler_)
                    (adk::io_engine::config::endpoint::kMessageHandler, message_handler_)
                    (adk::io_engine::config::endpoint::kRetryConnectTimes, 5)
                    (adk::io_engine::config::endpoint::kTcpNoDelay, is_tcp_nodelay_)
                    ;
        ADK_LOG_INFO_AC_TF("start connect to peer", "address <{1}:{2}>", peer_ip, peer_port);
        tcp_endpoint_ = tcp_engine_->Connect(client_props);
        index++;
        if (tcp_endpoint_ == nullptr)
        {
            ADK_LOG_ERROR_AC_TF("create endpoint failed", "specific reason: <{1}>", adk::io_engine::GetErrorInfo());
            return kFailure;
        }
    }
    else
    {
        //初始化server
        if (tcp_accepter_ == nullptr)
        {
            adk::io_engine::Property accept_props;
            accept_props(adk::io_engine::config::endpoint::kListenIp, self_ip_)
                        (adk::io_engine::config::endpoint::kListenPort, self_port_)
                        (adk::io_engine::config::endpoint::kReuseAddr, true)
                        (adk::io_engine::config::endpoint::kHeartbeatTimeoutMilli,
                            (tcp_heartbeat_inv_milli_ * tcp_timeout_multiplier_))
                        (adk::io_engine::config::endpoint::kEventHandler, event_handler_)
                        (adk::io_engine::config::endpoint::kAcceptHandler, accept_handler_)
                        (adk::io_engine::config::endpoint::kMessageHandler, message_handler_)
                        (adk::io_engine::config::endpoint::kTcpNoDelay, is_tcp_nodelay_);
            ADK_LOG_INFO_AC_TF("start accept new connection", "listen address <{1}:{2}>", 
                                self_ip_, self_port_);
            tcp_accepter_ = tcp_engine_->Accept(accept_props);
            if (!tcp_accepter_)
            {
                AMI_TD_SEND_EVENT(std::string(adk::io_engine::GetErrorInfo()));
                ADK_LOG_ERROR_AC_TF("create acceptor failed",
                                    "specific reason: <{1}>",
                                    adk::io_engine::GetErrorInfo());
                return kFailure;
            }
        }
    }
    return kSuccess;
}

std::error_code AmiBridge::InitTcpEngine()
{
    event_handler_     = new TcpEventHandler(this);
    accept_handler_    = new TcpAcceptHandler(this);
    message_handler_   = new TcpMessageHandler(this);
    connect_handler_   = new TcpConnectHandler(this);
    heartbeat_handler_ = new TcpHeartbeatHandler(this);

    adk::io_engine::Property props;
    tcp_engine_ = adk::io_engine::TcpEngine::Create(props);
    if (nullptr == tcp_engine_)
    {
        return kFailure;
    }
    return kSuccess;
}

std::error_code AmiBridge::SyncLocalSession()
{
    /// 1.向本端发送同步消息
    LocalSyncMsg local_sync_msg(local_session_id_);
    RepMessage* tier_local_sync_msg = bridge_tier_channel_->NewMessage(local_sync_msg.total_len());
    memcpy(tier_local_sync_msg->data(), &local_sync_msg, local_sync_msg.total_len());
    tier_local_sync_msg->set_data_len(local_sync_msg.total_len());
    ADK_LOG_INFO_AC_TF("send local sync Msg", "session_id <{1}>", local_session_id_);

    local_sync_finish_ = false;
    while (kSuccess != bridge_tier_channel_->ReplicateMsg(tier_local_sync_msg))
    {
        if (is_running())
        {
            ADK_LOG_ERROR_AC_TF("ReplicateMsg failed", "");
            BRIDGE_IDLE();
        }
        else
        {
            return kFailure;
        }
    }

    /// 2.等待本端同步完成
    while (!local_sync_finish_)
    {
        if (is_running())
        {
            BRIDGE_IDLE();
        }
        else
        {
            return kFailure;
        }
    }

    ADK_LOG_INFO_AC_TF("handle local sync msg completely",
                       "local_session_id <{1}>, peer_session_id <{2}>",
                       local_session_id_,
                       peer_session_id_);
    ADK_BARRIER();
    peer_sync_finish_ = false;
    ADK_BARRIER();
    return kSuccess;
}

std::error_code AmiBridge::SyncPeerSession()
{
    //< 1.向对端发送同步消息
    auto tcp_peer_sync_info                    = rx_ep_info_prop_.Dump();
    uint32_t buff_size                         = PeerSyncMsg::Size() + tcp_peer_sync_info.length() + 1;
    adk::io_engine::Message* tcp_peer_sync_msg = tcp_endpoint_->NewMessage(buff_size);
    memset((void*)(tcp_peer_sync_msg->data()), 0, buff_size);
    ((PeerSyncMsg*)(tcp_peer_sync_msg->data()))->set_data(kPeerSync,
                                                            local_session_id_,
                                                            ami_tx_msg_cnt_,
                                                            tcp_tx_packet_cnt_,
                                                            tcp_peer_sync_info);

    tcp_peer_sync_msg->set_data_len(buff_size);
    ADK_LOG_INFO_AC_TF("send peer sync msg",
                       "local_session_id <{1}>, peer_session_id <{2}>, recv_seq <{3}>, send_ack <{4}>, send_seq <{5}>, recv_ack <{6}>",
                       local_session_id_,
                       peer_session_id_,
                       ami_tx_msg_cnt_,
                       last_send_ack_seq_,
                       tcp_tx_packet_cnt_,
                       last_recv_ack_seq_);

    while (kSuccess != tcp_endpoint_->SendMsg(tcp_peer_sync_msg))
    {
        if (is_running() && tcp_stat_ == kConnected)
            continue;
        else
            return kFailure;
    }

    //< 2.等待同步对端完成
    while (!peer_sync_finish_)
    {
        if (is_running() && tcp_stat_ == kConnected)
        {
            ::usleep(0);
            continue;
        }
        else
            return kFailure;
    }

    ADK_LOG_INFO_AC_TF("handle peer sync Msg completely",
                       "local_session_id <{1}>, peer_session_id <{2}>, receive msg sqn <{3}>",
                       local_session_id_,
                       peer_session_id_,
                       tcp_rx_packet_cnt_);

    return kSuccess;
}

std::error_code AmiBridge::WriteSessionFile(uint64_t local_session_id, uint64_t peer_session_id)
{
    // 将本次会话本端和对端的SessionId写到文件，故障恢复使用
    std::string filename = GetRecorderDataPath();
    boost::system::error_code ec;
    try
    {
        if (!boost::filesystem::exists(filename, ec))
        {
            boost::filesystem::create_directories(filename, ec);
        }
    }
    catch (...)
    {
        auto* errmsg = std::strerror(errno);
        if (!boost::filesystem::exists(filename, ec))  // 被他人提前创建不报错
        {
            ADK_LOG_ERROR_AC_TF("can not create folder", "path <{1}>, <{2}>", filename, errmsg);
            return kFailure;
        }
    }

    filename += "/" + app_name_ += "_session.txt";
    std::ofstream out;
    out.open(filename.c_str(), std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        ADK_LOG_ERROR_AC_TF("session_record file open failed", "filename <{1}>, <{2}>", filename, std::strerror(errno));
        return kFailure;
    }
    else
    {
        out << local_session_id << "," << peer_session_id;
    }
    out.close();
    return kSuccess;
}

std::error_code AmiBridge::ReadSessionFile(uint64_t& local_session_id, uint64_t& peer_session_id)
{
    // 从文件中读取上次会话本端和对端的SessionId，故障恢复使用
    std::string filename = GetRecorderDataPath() + "/" + app_name_ + "_session.txt";
    std::string session[2];
    std::ifstream in;
    in.open(filename.c_str(), std::ios::in);
    if (in.fail())
    {
        ADK_LOG_WARN_AC_TF("session_record file not exsit", "");
        return kFailure;
    }
    else
    {
        int i = 0;
        while (std::getline(in, session[i], ','))
        {
            ++i;
        }
    }
    local_session_id = std::stoul(session[0]);
    peer_session_id  = std::stoul(session[1]);

    in.close();
    return kSuccess;
}

std::error_code AmiBridge::RecoveryBreakpoint()
{
    // 1.恢复会话ID
    uint64_t last_local_session_id = 0;
    uint64_t last_peer_session_id  = 0;
    if (ReadSessionFile(last_local_session_id, last_peer_session_id) == kSuccess)
    {
        local_session_id_ = last_local_session_id;
        peer_session_id_  = last_peer_session_id;
    }
    else  //文件不存在
    {
        ADK_LOG_ERROR_AC_TF("session_record file not exsit, can not init recovery", "");
        return kFailure;
    }
    ADK_LOG_INFO_AC_TF("get last session id completly",
                       "local_session_id <{1}>, peer_session_id <{2}>",
                       local_session_id_,
                       peer_session_id_);

    // 2.恢复发送断点
    Context* ctx          = nullptr;
    AmiRecordAgent* agent = nullptr;
    if (!(ctx = GetContext()) || !(agent = ctx->GetRecordAgent()))
    {
        ADK_LOG_ERROR_AC_TF("CANNOT get valid record channel, "
                            "maybe 'recorder' disabled",
                            "");
        StopAmiApp();
        return kFailure;
    }
    AmiMsgPacket* head = const_cast<AmiMsgPacket*>(tcp_packets_queue_->Head());
    if (head == nullptr)
    {
        /* 经过Recovery，上次所有未被对端ACK的数据会在OnMessage中重新恢复到tcp_packets_queue
        ** 如果tcp_packets_queue为空，存在以下两种可能：
        ** 1. 上次发送的数据已经全部ACK了
        ** 2. 上次没有发送过数据
        ** 这两种情况下，需要从recorder中读取记录的最后一条数据，所有的发送端点都以上次recorder的记录为准
        */
        ADK_LOG_WARN_AC_TF("recovery is finish, but cannot find Breakpoint",
                           "need to read recorder last msg");
        if (agent->GetRxHistMessageCnt(ami_rx_msg_cnt_) != kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("get rx breakpoint failed,exit app", "");
            StopAmiApp();
        }
        if (ami_rx_msg_cnt_ == 0)
        {
            ADK_LOG_WARN_AC_TF("read recorder last msg is NULL",
                               "last session didn't send msg");
        }
        tcp_tx_packet_cnt_ = ami_rx_msg_cnt_;
        last_recv_ack_seq_ = ami_rx_msg_cnt_;
    }
    else
    {
        /* 经过Recovery，上次所有未被对端ACK的数据会在OnMessage中重新恢复到tcp_packets_queue
        ** 如果tcp_packets_queue不为空，表示上次有未被ACK的数据，这种情况下。
        ** tcp_packets_queue队列的头部减一表示上次收到的ACK, 尾部表示从AMI上收到并发送到AMI上的最后一条消息
        */
        last_recv_ack_seq_ = head->total_sqn() - 1;
        // ami_rx_msg_cnt_       = last_recv_ack_seq_ + tcp_packets_queue_.length();
        // tcp_tx_packet_cnt_ = ami_rx_msg_cnt_;
    }

    ADK_LOG_INFO_AC_TF("get tx breakpoint completly",
                       "ami_rx_msg_cnt <{1}>, tcp_tx_packet_cnt <{2}>, last_recv_ack_seq <{3}>",
                       ami_rx_msg_cnt_,
                       tcp_tx_packet_cnt_,
                       last_recv_ack_seq_);

    // 3.恢复接收断点
    uint64_t last_msg_seq             = 0;
    std::set<std::string> tx_name_set = GetTxEndpointSet();
    for (auto& tx_name : tx_name_set)
    {
        std::vector<int32_t> partitions;
        GetTxEndpointPartitions(tx_name, partitions);
        for (auto& tx_pt : partitions)
        {
            AmiTxRecordChannel* tx_chann = agent->GetTxChannel(tx_name, tx_pt);
            if (tx_chann == nullptr)
            {
                ADK_LOG_ERROR_AC_TF("<{1}> endpoint should be set IsRecord", tx_name);
                return kFailure;
            }

            int32_t ec = tx_chann->GetHistMessage(
                [&tx_name, &tx_pt, &last_peer_session_id, &last_msg_seq](ami::Message* msg) {
                    uint64_t session_id = 0;
                    uint64_t msg_sqn = 0;
                    msg->get_persistent_context(session_id, msg_sqn);
                     if (session_id == last_peer_session_id && msg_sqn > last_msg_seq)
                     {
                         last_msg_seq = msg_sqn;    //遍历所有的TX，获取最后一条消息
                     }
                     ADK_LOG_INFO_AC_TF("get rx history",
                        "ep_name <{1}>, ep_patition <{2}>, session_id <{3}>, msg_sqn <{4}>",
                        tx_name, tx_pt, session_id, msg_sqn);
                    return ErrorCode::kSuccess; },
                AmiRecorderBase::kMostRecent,
                AmiRecorderBase::kMostRecent);

            if (ec != ErrorCode::kSuccess)
            {
                ADK_LOG_ERROR_AC_TF("CANNOT get valid rx msg break point", "");
                return kFailure;
            }
        }
    }
    /* Bridge关闭了TX方向的Recovery，因此Bridge不会读取recorder的数据进行重演
    ** 而是读取Recorder记录的最后TX消息，所有的断点都以recoder记录的最后一条TX消息序号对齐
    ** 表示上次从TCP上接收的消息已经全部发送到AMI上
    */
    tcp_rx_packet_cnt_  = last_msg_seq;
    tcp_rep_packet_cnt_ = last_msg_seq;
    tier_rx_packet_cnt_ = last_msg_seq;
    last_send_ack_seq_  = last_msg_seq;
    ami_tx_msg_cnt_     = last_msg_seq;
    ADK_LOG_INFO_AC_TF("get rx breakpoint completly",
                       "tcp_rx_packet_cnt <{1}>, tier_rx_packet_cnt <{2}>, ami_tx_msg_cnt <{3}>, last_send_ack_seq <{4}>",
                       tcp_rx_packet_cnt_,
                       tier_rx_packet_cnt_,
                       ami_tx_msg_cnt_,
                       last_send_ack_seq_);

    // 3.恢复完成
    ADK_BARRIER();
    is_recovery_finish_ = true;

    return kSuccess;
}

std::error_code AmiBridge::RetransmitMsg()
{
    //> 1.保存历史队列，创建一个空的已发送队列
    TcpPacketsQueue* history_queue = nullptr;
    if (tcp_packets_queue_ == tcp_packets_queue1_)
    {
        history_queue      = tcp_packets_queue1_;
        tcp_packets_queue_ = tcp_packets_queue2_;
    }
    else
    {
        history_queue      = tcp_packets_queue2_;
        tcp_packets_queue_ = tcp_packets_queue1_;
    }

    AmiMsgPacket packet;

    //< 2.重传record消息(只有主备切换场景才会触发)，发送完成放入到已发放队列中
    if (recorder_retransmit_num_ > 0)
    {
        ADK_LOG_INFO_AC_TF("retransmit msg from record", "");

        Context* ctx          = nullptr;
        AmiRecordAgent* agent = nullptr;
        if (!(ctx = GetContext()) || !(agent = ctx->GetRecordAgent()))
        {
            ADK_LOG_ERROR_AC_TF("CANNOT get valid record channel, "
                                "maybe 'recorder' disabled",
                                "");
            StopAmiApp();
            return kFailure;
        }

        ErrorCode_def get_hist_msg_ret = kSuccess;

        get_hist_msg_ret = agent->GetRxHistMessage(
            [this](Message* msg) -> ErrorCode {
                // 判断消息有效性
                const auto ep_id = msg->get_endpoint_id();
                RxEndpointStuff* rxep_stuff;
                if (!rxep_map_.count(ep_id))
                {
                    ADK_LOG_ERROR_AC_TF("unexpected message from recorder",
                                        "ep_id <{1}>", ep_id);
                    return kSuccess;
                }
                else
                {
                    rxep_stuff = &rxep_map_.at(ep_id);
                }

                // 复制record消息
                Message* ami_msg = GetContext()->NewMessage(msg->size());
                ami_msg->append(msg->const_data(), msg->size());

                // 构造AmiMsgPacket消息
                AmiMsgPacket tx_packet;
                if (rxep_stuff->to_compress)
                {
                    size_t buffer_len  = 0;
                    const char* buffer = compressor_proto_->Compress(ami_msg, buffer_len);
                    tx_packet          = AmiMsgPacket(ami_msg, 1, buffer, buffer_len);
                }
                else
                {
                    tx_packet = AmiMsgPacket(ami_msg, 0);
                }

                tx_packet.endpoint_id()  = msg->get_endpoint_id();
                tx_packet.transport_id() = msg->get_transport_id();
                tx_packet.total_sqn()    = msg->get_total_order_seq_num();
                const aaf::TransportInfo* transport_info =
                    GetTransportInfo(msg->get_transport_id());
                tx_packet.partition_no() = transport_info->transport_partition;

                static bool retransmit_first = true;
                if (retransmit_first)
                {
                    retransmit_first = false;
                    ADK_LOG_INFO_AC_TF("retransmit msg from record",
                                       "start_sqn <{1}>, retransmit_length <{2}>",
                                       msg->get_total_order_seq_num(),
                                       recorder_retransmit_num_);
                }

                // 记录并发送消息
                tcp_packets_queue_->Push(tx_packet);
                adk::io_engine::Message* message = tcp_endpoint_->NewMessage(tx_packet.total_len());
                memcpy(message->data(), &tx_packet, tx_packet.header_len());
                if (tx_packet.compress())
                {
                    memcpy(message->data() + tx_packet.header_len(),
                           tx_packet.data(),
                           tx_packet.data_len());
                }
                else
                {
                    memcpy(message->data() + tx_packet.header_len(),
                           ((Message*)tx_packet.data())->const_data(),
                           tx_packet.data_len());
                }
                message->set_data_len(tx_packet.total_len());
                uint64_t send_retries = 0;  // 记录发送失败的最大次数
                while (tcp_endpoint_->SendMsg<false>(message) != ErrorCode::kSuccess)
                {
                    if (!is_running())
                    {
                        StopAmiApp();
                    }
                    ++send_retries;
                    ::usleep(1);
                }
                if (ADK_UNLIKELY(send_retries))
                {
                    ++tcp_tx_err_cnt_;
                    tcp_tx_max_retries_ = std::max(tcp_tx_max_retries_, send_retries);
                }
                tcp_tx_packet_cnt_ = tx_packet.total_sqn();  //断点已经对齐，重传消息被认为新消息需要重新统计
                recorder_retransmit_num_--;
                return kSuccess;
            },
            recorder_retransmit_begin_,
            recorder_retransmit_begin_ + recorder_retransmit_num_);
    }

    //< 3.重传历史队列中的消息，故障恢复场景，历史队列中的消息是从recorder中恢复出来的，
    //      发送完成放入已发送队列中，等待对端ack后释放
    int32_t retransmit_num = 0;
    while (!history_queue->Pop(packet))
    {
        adk::io_engine::Message* message = tcp_endpoint_->NewMessage(packet.total_len());
        memcpy(message->data(), &packet, packet.header_len());
        if (packet.compress())
        {
            memcpy(message->data() + packet.header_len(),
                   packet.data(),
                   packet.data_len());
        }
        else
        {
            memcpy(message->data() + packet.header_len(),
                   ((Message*)packet.data())->const_data(),
                   packet.data_len());
        }
        message->set_data_len(packet.total_len());
        if (tcp_endpoint_->SendMsg(message) != ErrorCode::kSuccess)
        {
            ++tcp_tx_err_cnt_;
        }
        tcp_packets_queue_->Push(packet);
        tcp_tx_packet_cnt_ = packet.total_sqn();  //断点已经对齐，重传消息被认为新消息需要重新统计
    }

    ADK_LOG_INFO_AC_TF("retransmit msg completely",
                       "send_sqn <{1}>, recv_ack <{2}>",
                       tcp_tx_packet_cnt_,
                       last_recv_ack_seq_);

    return kSuccess;
}

int32_t AmiBridge::DirectSendMsg(AmiMsgPacket& tx_packet)
{
    adk::io_engine::Message* message = tcp_endpoint_->NewMessage(tx_packet.total_len());
    memcpy(message->data(), &tx_packet, tx_packet.header_len());
    if (tx_packet.compress())
    {
        memcpy(message->data() + tx_packet.header_len(),
               tx_packet.data(),
               tx_packet.data_len());
    }
    else
    {
        memcpy(message->data() + tx_packet.header_len(),
               ((Message*)tx_packet.data())->const_data(),
               tx_packet.data_len());
    }
    message->set_data_len(tx_packet.total_len());

    return Send(message);
}

int32_t AmiBridge::FragmentSendMsg(AmiMsgPacket& packet)
{
    static TimePoint next_send_time = Clock::now();
    if (packet.total_len() < fragment_size_)
    {
        while (Clock::now() < next_send_time)
        {
            ADK_PAUSE();
        }
        if (kSuccess != DirectSendMsg(packet))
        {
            return kFailure;
        }
        uint32_t interval = (packet.total_len() * 8) / net_bandwidth_permicro_;
        next_send_time    = Clock::now() + std::chrono::microseconds(interval);
    }
    else
    {
        AMI_TD_SEND_EVENT_ONCE("fragment send msg");
        //lock
        std::lock_guard<std::mutex> lck(mtx_);

        //< send header
        char* fragment_buffer            = (char*)(&packet);
        adk::io_engine::Message* message = tcp_endpoint_->NewMessage(packet.header_len());
        memcpy(message->data(), fragment_buffer, packet.header_len());
        message->set_data_len(packet.header_len());

        if (kSuccess != Send(message))
            return kFailure;

        //< flagment sendmsg
        // int32_t fragment_num = (packet.data_len() + fragment_size_ - 1 ) / fragment_size_;
        uint32_t left_len = packet.data_len();
        if (packet.compress())
        {
            fragment_buffer = const_cast<char*>(packet.data());
        }
        else
        {
            ami::Message* msg = (Message*)(packet.data());
            fragment_buffer   = const_cast<char*>(msg->const_data());
        }

        do
        {
            uint32_t buff_size               = left_len > fragment_size_ ? fragment_size_ : left_len;
            adk::io_engine::Message* message = tcp_endpoint_->NewMessage(buff_size);
            memcpy(message->data(), fragment_buffer, buff_size);
            message->set_data_len(buff_size);

            while (Clock::now() < next_send_time)
            {
                ADK_PAUSE();
            }
            if (kSuccess != Send(message))
            {
                return kFailure;
            }

            fragment_buffer += buff_size;
            left_len -= buff_size;

            if (buff_size != fragment_size_)  //last fragment
            {
                uint32_t interval = (buff_size * 8) / net_bandwidth_permicro_;
                next_send_time    = Clock::now() + std::chrono::microseconds(interval);
            }
            else
            {
                next_send_time = Clock::now() + std::chrono::microseconds(ratectl_inv_micro_);
            }
        } while (left_len != 0);

        //unlock
    }
    return kSuccess;
}

void AmiBridge::SendAmiMsg()
{
    ADK_LOG_INFO_AC_TF("start thread SendAmiMsg", "");
    ami::Message* ami_msg             = nullptr;
    RxEndpointStuff* rxep_stuff       = nullptr;
    adk::variant::VariantEntry* entry = nullptr;
    static adk::log::IntervalLogger interval_logger(3);

    //连接正常,循环发送消息
    while (tcp_stat_ == kConnected && is_running())
    {
        if (kSuccess != ami_rx_queue_->WaitEntry(&entry))
        {
            BRIDGE_IDLE();
            continue;
        }
        char* tmp = entry->buffer;
        ami_msg   = *(Message**)tmp;

        ///1. 正确性检查
        const auto ep_id = ami_msg->get_endpoint_id();
        const auto rxep  = rxep_map_.find(ep_id);
        if (rxep != rxep_map_.end())
        {
            rxep_stuff = &rxep->second;
        }
        else
        {
            ami_rx_err_cnt_++;
            ADK_INV_LOG_WARN_AC_TF(interval_logger, "unexpected message from rx_endpoint", "endpoint_id <{1}>", ep_id);
            GetContext()->DeleteMessage(ami_msg);
            ami_rx_queue_->FreeEntry(entry);
            continue;
        }

        const auto transport_id = ami_msg->get_transport_id();
        if (!tp_map_.count(transport_id))
        {
            tp_map_.emplace(
                std::make_pair(
                    transport_id, TransportStuff(transport_id, ami_msg->get_partition_no())));
        }
        ami_rx_msg_cnt_ = ami_msg->get_total_order_seq_num();  //从ami上接收的消息统计

        ///2. 根据配置项决定是否压缩
        AmiMsgPacket tcp_tx_packet;
        if (rxep_stuff->to_compress)
        {
            SAVE_MSG("sendmsg.txt", ami_msg->size(), ami_msg->data());
            try
            {
                size_t buffer_len  = 0;
                const char* buffer = compressor_proto_->Compress(ami_msg, buffer_len);
                tcp_tx_packet      = AmiMsgPacket(ami_msg, 1, buffer, buffer_len);
            }
            catch (const std::system_error& e)
            {
                ADK_LOG_ERROR_AC_TF(
                    "compress msg failed and exit thread SendAmiMsg",
                    "ami_msg <{1}>, code <{2}>, message <{3}>, detail <{4}>",
                    ami_msg,
                    e.code(),
                    e.code().message(),
                    e.what());
                return;
            }
            AMI_TD_SEND_EVENT_ONCE("compresss ami message");
            GetContext()->DeleteMessage(ami_msg);
        }
        else
        {
            tcp_tx_packet = AmiMsgPacket(ami_msg, 0);
        }

        rxep_stuff->msg_cnt++;
        rxep_stuff->total_payload_bytes += ami_msg->size();
        rxep_stuff->total_cpayload_bytes += tcp_tx_packet.data_len();

        ///3.缓存消息
        tcp_packets_queue_->Push(tcp_tx_packet);
        ami_rx_queue_->FreeEntry(entry);
        tcp_tx_packet_cnt_ = tcp_tx_packet.total_sqn();  //发送到tcp的消息统计

        ///4.发送tcp消息
        if (!is_rate_ctl_)
        {
            if (kSuccess != DirectSendMsg(tcp_tx_packet))
            {
                ADK_LOG_ERROR_AC_TF("send msg without rate control",
                                    "send tcp packet sqn <{1}> failed and exit thread SendAmiMsg",
                                    tcp_tx_packet.total_sqn());
                return;
            }
#ifdef TEST_FRAMEWORK_DEBUG
            static int32_t s_leader_test_cnt = 0;
            if (test_flag_ == BACKUP_FAST_THAN_LEADER && ++s_leader_test_cnt > 2000)
            {
                sleep(100000);  //发送2000条数据后进入休眠
            }
#endif
        }
        else
        {
            if (kSuccess != FragmentSendMsg(tcp_tx_packet))
            {
                ADK_LOG_ERROR_AC_TF("send msg with rate control",
                                    "send tcp packet sqn <{1}> failed and exit thread SendAmiMsg",
                                    tcp_tx_packet.total_sqn());
                return;
            }
        }

        // tcp_tx_packet_cnt_ = tcp_tx_packet.total_sqn();    //发送到tcp的消息统计
        bridge_tx_payload_bytes_ += ami_msg->size();
        bridge_tx_cpayload_bytes_ += tcp_tx_packet.data_len();
        bridge_tx_bytes_ += tcp_tx_packet.total_len();
    }
    ADK_LOG_INFO_AC_TF("exit thread SendAmiMsg", "reason: tcp_stat <{1}>", (int)tcp_stat_);
}

void AmiBridge::BackAmiMsg()
{
    ADK_LOG_INFO_AC_TF("start thread BackAmiMsg", "");

    static adk::log::IntervalLogger interval_logger(3);

    ami::Message* ami_msg             = nullptr;
    RxEndpointStuff* rxep_stuff       = nullptr;
    adk::variant::VariantEntry* entry = nullptr;

    //连接正常,循环发送消息
    while (IsBackup() && tcp_stat_ == kConnected)
    {
        if (kSuccess != ami_rx_queue_->WaitEntry(&entry))
        {
            BRIDGE_IDLE();
            continue;
        }
        char* tmp = entry->buffer;
        ami_msg   = *(Message**)tmp;

        ///1. 正确性检查
        const auto ep_id = ami_msg->get_endpoint_id();
        const auto rxep  = rxep_map_.find(ep_id);
        if (rxep != rxep_map_.end())
        {
            rxep_stuff = &rxep->second;
        }
        else
        {
            ami_rx_err_cnt_++;
            ADK_INV_LOG_WARN_AC_TF(interval_logger, "unexpected message from rx_endpoint", "endpoint_id <{1}>", ep_id);
            GetContext()->DeleteMessage(ami_msg);
            continue;
        }

        const auto transport_id = ami_msg->get_transport_id();
        if (!tp_map_.count(transport_id))
        {
            tp_map_.emplace(
                std::make_pair(
                    transport_id, TransportStuff(transport_id, ami_msg->get_partition_no())));
        }
        ami_rx_msg_cnt_ = ami_msg->get_total_order_seq_num();

        ///2. 根据配置项决定是否压缩
        AmiMsgPacket tcp_tx_packet;
        if (rxep_stuff->to_compress)
        {
            try
            {
                size_t buffer_len  = 0;
                const char* buffer = compressor_proto_->Compress(ami_msg, buffer_len);
                tcp_tx_packet      = AmiMsgPacket(ami_msg, 1, buffer, buffer_len);
            }
            catch (const std::system_error& e)
            {
                ADK_LOG_ERROR_AC_TF(
                    "compress msg failed and exit thread BackAmiMsg",
                    "ami_msg <{1}>, code <{2}>, message <{3}>, detail <{4}>",
                    ami_msg,
                    e.code(),
                    e.code().message(),
                    e.what());
                return;
            }
            GetContext()->DeleteMessage(ami_msg);
        }
        else
        {
            tcp_tx_packet = AmiMsgPacket(ami_msg, 0);
        }

        rxep_stuff->msg_cnt++;
        rxep_stuff->total_payload_bytes += ami_msg->size();
        rxep_stuff->total_cpayload_bytes += tcp_tx_packet.data_len();

        ///3.缓存消息
        tcp_packets_queue_->Push(tcp_tx_packet);
        tcp_tx_packet_cnt_ = tcp_tx_packet.total_sqn();
        ami_rx_queue_->FreeEntry(entry);

        ///4.定长清空消息
        // 测试场景
        auto temp_tcp_rx_packet_cnt = ACCESS_ONCE(tcp_tx_packet_cnt_);
        auto temp_recv_ack_seq      = ACCESS_ONCE(last_recv_ack_seq_);

        uint64_t pop_num = temp_tcp_rx_packet_cnt - temp_recv_ack_seq;
        if (pop_num > ack_step_len_)
        {
            AmiMsgPacket packet;
            if (tcp_packets_queue_->Pop(packet) != kSuccess)
            {
                ADK_INV_LOG_WARN_AC_TF(interval_logger, "Pop msg from tcp_packets_queue failed", "tcp_packets_queue length <{1}>, tcp_tx_packet_cnt <{2}>, last_recv_ack_seq <{3}>", tcp_packets_queue_->length(), temp_tcp_rx_packet_cnt, temp_recv_ack_seq);
                continue;
            }
            bridge_tx_payload_bytes_ += packet.orig_data_len();
            bridge_tx_cpayload_bytes_ += packet.data_len();
            bridge_tx_bytes_ += packet.total_len();
            if (packet.compress())
            {
                delete[] packet.data();
            }
            else
            {
                GetContext()->DeleteMessage((Message*)(packet.data()));
            }

            ++last_recv_ack_seq_;
        }

#ifdef TEST_FRAMEWORK_DEBUG
        static int32_t s_backup_test_cnt = 0;
        if (test_flag_ == BACKUP_SLOW_THAN_LEADER && ++s_backup_test_cnt >= 2000)
        {
            while (IsBackup())
            {
                sleep(1);  //备节点‘发送’2000条消息后进入休眠
            }
        }
#endif
    }

    ADK_LOG_INFO_AC_TF("exit thread BackAmiMsg", "");
}

void AmiBridge::SendAckMsg()
{
    ADK_LOG_INFO_AC_TF("start thread SendAckMsg", "");
    bool is_first       = true;
    TimePoint ack_timer = Clock::now() + std::chrono::milliseconds(tcp_heartbeat_inv_milli_);
    while (IsLeader() && tcp_stat_ == kConnected)
    {
        // uint64_t temp_tier_rx_packet_cnt = ACCESS_ONCE(tier_rx_packet_cnt_);
        uint64_t temp_ami_tx_msg_cnt = ACCESS_ONCE(ami_tx_msg_cnt_);
        if (Clock::now() > ack_timer || temp_ami_tx_msg_cnt - last_send_ack_seq_ > ack_step_len_)
        {
            if (is_first)
            {
                ADK_LOG_INFO_AC_TF("first send TcpAck msg,",
                                   "last recv seq <{1}>, now recv sqn <{2}>",
                                   last_send_ack_seq_,
                                   temp_ami_tx_msg_cnt);
                is_first = false;
            }
            ack_timer = Clock::now() + std::chrono::milliseconds(tcp_heartbeat_inv_milli_);
            TcpAckMsg ack_msg(temp_ami_tx_msg_cnt);

            //prevent to multi-thread send when fragment package
            std::lock_guard<std::mutex> lck(mtx_);

            tcp_endpoint_->SendMsg(&ack_msg, ack_msg.total_len());
            last_send_ack_seq_ = temp_ami_tx_msg_cnt;
        }
        else
        {
            BRIDGE_IDLE();
        }
    }

    ADK_LOG_INFO_AC_TF("exit thread SendAckMsg",
                       "reason: role_stat <{1}>, tcp_stat <{2}>",
                       (int)(IsLeader()),
                       (int)tcp_stat_);
}

bool AmiBridge::IsLeader()
{
    if (role_stat_ == kLeader && tier_channel_handler_->tc_role() == kTcRoleLeader)
    {
        return true;
    }
    else
        return false;
}

bool AmiBridge::IsBackup()
{
    if (role_stat_ == kBackup && tier_channel_handler_->tc_role() == kTcRoleBackup)
    {
        return true;
    }
    else
        return false;
}

bool AmiBridge::OnCollectIndicator(bt::ptree& status_tree)
{
    status_tree.put("ami_rx_msg_cnt", ami_rx_msg_cnt_);
    status_tree.put("ami_rx_err_msg", ami_rx_err_cnt_);
    status_tree.put("tcp_tx_pkt_cnt", tcp_tx_packet_cnt_);
    status_tree.put("tcp_tx_err_cnt", tcp_tx_err_cnt_);
    status_tree.put("tcp_tx_max_retries", tcp_tx_max_retries_);
    tcp_tx_max_retries_ = 0;  // 最大重试次数每三秒重置
    status_tree.put("total_tx_payload", ByteSize(bridge_tx_payload_bytes_));
    status_tree.put("total_tx_cpayload", ByteSize(bridge_tx_cpayload_bytes_));
    status_tree.put("total_tx_bytes", ByteSize(bridge_tx_bytes_));

    status_tree.put("tcp_rx_pkt_cnt", tcp_rx_packet_cnt_);
    status_tree.put("tcp_rx_err_pkt", tcp_rx_err_cnt_);
    status_tree.put("tcp_rep_pkt_cnt", tcp_rep_packet_cnt_);
    status_tree.put("tier_rx_pkt_cnt", tier_rx_packet_cnt_);
    status_tree.put("ami_tx_msg_cnt", ami_tx_msg_cnt_);
    status_tree.put("total_rx_payload", ByteSize(bridge_rx_cpayload_bytes_));
    status_tree.put("total_rx_dpayload", ByteSize(bridge_rx_payload_bytes_));
    status_tree.put("total_rx_bytes", ByteSize(bridge_rx_bytes_));
    status_tree.put(" ", "");

    if (!rxep_map_.empty())
    {
        bt::ptree& rxep_status_tree =
            status_tree.add_child("RxEndpoints", bt::ptree());
        for (const auto& rx_ep_stuff : rxep_map_)
        {
            auto rx_name = rx_ep_stuff.second.ep_name;
            auto it      = rxep_status_tree.push_back(bt::ptree::value_type(rx_name, bt::ptree()));
            it->second.put("messages", rx_ep_stuff.second.msg_cnt);
            it->second.put("payload_bytes", ByteSize(rx_ep_stuff.second.total_payload_bytes));
            it->second.put("cpayload_bytes", ByteSize(rx_ep_stuff.second.total_cpayload_bytes));
        }
    }

    if (!txep_map_.empty())
    {
        bt::ptree& txep_status_tree =
            status_tree.add_child("TxEndpoints", bt::ptree());
        for (const auto& tx_ep_stuff : txep_map_)
        {
            auto tx_name = tx_ep_stuff.second.ep_name;
            auto it      = txep_status_tree.push_back(bt::ptree::value_type(tx_name, bt::ptree()));
            it->second.put("messages", tx_ep_stuff.second.msg_cnt);
            it->second.put("payload_bytes", ByteSize(tx_ep_stuff.second.total_payload_bytes));
            it->second.put("dpayload_bytes", ByteSize(tx_ep_stuff.second.total_cpayload_bytes));
        }
    }

    std::string tcp_ind;
    if (tcp_engine_ && tcp_engine_->CollectIndicator(tcp_ind) == adk::ErrorCode::kSuccess)
    {
        bt::ptree& tcp_engine_indi_tree =
            status_tree.add_child("TcpEngine", bt::ptree());
        try
        {
            std::stringstream str_stream(tcp_ind);
            bt::read_json(str_stream, tcp_engine_indi_tree);
        }
        catch (const std::exception& e)
        {
            ADK_LOG_DEBUG_AC_TF("get tcp_engine indicator failed ", "error: {1}", e.what());
        }
    }
    return true;
}

void AmiBridge::MakePeerRxEndPointInfo(PeerSyncMsg* handshake_msg)
{
    if (handshake_msg->ep_info_size() != 0)
    {
        ADK_LOG_WARN_AC_TF("peer handshake msg format is inconsistent", "");
        return;
    }

    try
    {
        peer_rxep_map_.clear();
        ami::Property peer_ep_info(handshake_msg->endpoint_info());
        ADK_LOG_INFO_AC_TF("recv peer rx_ep info", "<{1}>", peer_ep_info.Dump());
        ami::Property local_ep_info;
        std::vector<ami::Property> prop_vec;
        for (auto& item : txep_map_)
        {
            ami::Property tmp_prop;
            tmp_prop.SetValue(kEndpointName, item.second.ep_name);
            tmp_prop.SetValue(kEndpointId, item.second.ep_id);
            tmp_prop.SetValue(kPartitions, tx_info_map_.at(item.second.ep_id));
            prop_vec.push_back(tmp_prop);
        }
        local_ep_info.SetValue(kEndpointName, prop_vec);
        ADK_LOG_INFO_AC_TF("local tx_ep info", "<{1}>", local_ep_info.Dump());

        auto endpoints_info = peer_ep_info.GetPropertyVectorValue(kEndpointName);
        for (auto& item : endpoints_info)
        {
            auto ep_name    = item.GetStringValue(kEndpointName);
            auto ep_id      = item.GetIntValue(kEndpointId);
            auto partitions = item.GetIntVectorValue(kPartitions);

            // 根据对端发送的endpoint_name查找本端同名endpoint_name的endpoint_id
            auto dest_iter = txep_map_.begin();
            for (; dest_iter != txep_map_.end(); ++dest_iter)
            {
                if (dest_iter->second.ep_name == ep_name)
                {
                    break;
                }
            }

            if (dest_iter != txep_map_.end())
            {
                peer_rxep_map_.emplace(ep_id, PeerRxEndPointStuff(ep_id, ep_name, partitions, &dest_iter->second));
            }
            else
            {
                // 找不到同名的主题，后续在CheckEndpoint中校验，这里只构建不校验
                peer_rxep_map_.emplace(ep_id, PeerRxEndPointStuff(ep_id, ep_name, partitions, nullptr));
                ADK_LOG_WARN_AC_TF("peer ep_name can not find", "ep_name <{1}>", ep_name);
            }
        }
    }
    catch (const std::exception& e)
    {
        ADK_LOG_WARN_AC_TF("error handmsg", "error <{1}>, raw str info <{2}>", e.what(), (const char*)handshake_msg->endpoint_info());
    }
}

/*****************************************************************
 * override OnXXX callbacks of base class GenericAmiApplication  
 */
void AmiBridge::SetAmiAppOption()
{
    AddOption("disaster", "Init disater mode");
    AddOptionWithArgument<std::string>("close-rfd", "Bridge manager set this argument when launtch bridge", "-1");
    AddOptionWithArgument<std::string>("close-wfd", "Bridge manager set this argument when launtch bridge", "-1");
}

void AmiBridge::OnAmiAppOption(const std::string& option_name)
{
    if (option_name == "domain-server")
    {
        domain_server_ = GetOptionArgument<std::string>("domain-server");
    }
    if (option_name == "close-rfd")
    {
        std::string read_fd = GetOptionArgument<std::string>("close-rfd");
        close_fd_           = std::stoi(read_fd);
    }
    if (option_name == "close-wfd")
    {
        std::string write_fd = GetOptionArgument<std::string>("close-wfd");
        if (write_fd != "-1")
        {
            close(std::stoi(write_fd));
        }
    }
}

void AmiBridge::OnConfigureFramework(Property& fw_props)
{
    fw_props.SetValue(aaf::config::kEnableHighAvailableContext, true);
    fw_props.SetValue(aaf::config::kEnableAppNameCheck, false);
}

int32_t AmiBridge::OnAmiInitBegin()
{
    ADK_LOG_INFO_AC_TF("init Ami begin", "");

    //读取配置初始化Bridge
    app_name_ = GetApplicationName();
    IF_ERR_RET_AAF(Init(), ADK_LOG_ERROR_AC_TF("init bridge failed", ""));

    //获取启动状态
    if (GetInitStatus() == "Recovery")
    {
        init_type_          = kRecovery;
        is_recovery_finish_ = false;
        ADK_LOG_INFO_AC_TF("get app init status", "<{1}>", (int)init_type_);
    }

    //判断灾备角色
    is_disaster_ = is_disaster_backup();

    //初始化Compressor/Decompressor
    if (!compress_lib_path_.empty())
    {
        try
        {
            compressor_proto_.reset(
                new Compressor(compress_lib_path_, compress_config_));
            decompressor_proto_.reset(
                new Decompressor(compress_lib_path_, compress_config_));
        }
        catch (const std::system_error& e)
        {
            ADK_LOG_ERROR_AC_TF("init compressor/decompressor failed",
                                "code <{1}>, message <{2}>, detail <{3}>",
                                e.code(),
                                e.code().message(),
                                e.what());
            return aaf::kFailure;
        }
    }
    //初始化消息队列
    ami_rx_queue_       = AmiMessageQueue::Create("ami_rx_queue", ami_rx_queue_len_);
    tcp_packets_queue1_ = TcpPacketsQueue::Create("tcp_packets_queue1_");
    tcp_packets_queue2_ = TcpPacketsQueue::Create("tcp_packets_queue2_");
    tcp_packets_queue_  = tcp_packets_queue1_;
    //初始化TCPEngine
    IF_ERR_RET_AAF(InitTcpEngine(), ADK_LOG_ERROR_AC_TF("create tcp_engine failed...", ""));
    //初始化local_session_id
    if (init_type_ == kBootstrap)
    {
        timespec tv;
        clock_gettime(CLOCK_REALTIME, &tv);
        local_session_id_ = ((tv.tv_sec * 1000000000 + tv.tv_nsec) << 30) + GetContextId();
    }
    //初始化monitor....
    adk::MonitorOps monitor_ops;
    monitor_ops.is_collection_indicator = true;
    monitor_ops.on_collection_indicator =
        boost::bind(&AmiBridge::OnCollectIndicator, this, _1);
    adk::Monitor::RegisterObject(
        ami::config::category::kBridge, GetApplicationName(), &monitor_ops);

    return aaf::kSuccess;
}

int32_t AmiBridge::OnTxEndpointCreationBegin()
{
    ADK_LOG_INFO_AC_TF("create tx_endpoints begin", "");
    return aaf::kSuccess;
}

int32_t AmiBridge::OnTxEndpointCreation(
    aaf::EndpointHandler* ep_hdl, const std::string& ep_name)
{
    auto ep_id = GetTxEndpointId(ep_name);

    Property ep_prop;
    std::vector<int32_t> no_partitions;
    IF_ERR_RET_AAF((ami::ErrorCode)config_agent_->GetTxEndpointConfig(
        ep_name, std::string(), no_partitions, &ep_prop));
    bool to_compress    = ep_prop.GetValue(config::endpoint::kToCompress, false);
    bool is_loadbalance = ep_prop.GetValue(config::endpoint::kLoadBalance, false);
    txep_map_.emplace(
        std::make_pair(ep_id, TxEndpointStuff(ep_id, ep_name, to_compress, is_loadbalance, ep_hdl)));
    GetTxEndpointPartitions(ep_name, no_partitions);
    tx_info_map_.emplace(std::make_pair(ep_id, no_partitions));
    if (to_compress && (!compressor_proto_ || !decompressor_proto_))
    {
        ADK_LOG_ERROR_AC_TF("tx endpoint need to be decompressed, but decompressor not configured",
                            "endpoint <{1}>",
                            ep_name);
        return aaf::kFailure;
    }

    ADK_LOG_INFO_AC_TF("create tx endpoint ok",
                       "endpoint_name <{1}> endpoint_id <{2}>",
                       ep_name,
                       ep_id);
    return aaf::kSuccess;
}

int32_t AmiBridge::OnRxEndpointCreationBegin()
{
    ADK_LOG_INFO_AC_TF("create rx_endpoints begin", "");
    is_txep_finish_ = true;
    //初始化TierChannel
    tier_channel_handler_ = new BridgeTierChannelHandler(this);
    bridge_tier_channel_  = GetContext()->CreateTierChannel(
                                tier_channel_name_, *tier_channel_handler_);
    if (bridge_tier_channel_ == nullptr)
    {
        ADK_LOG_ERROR_AC_TF("create tier_channel failed", "");
        return aaf::kFailure;
    }

    auto& ep_set = this->GetRxEndpointSet();
    std::vector<ami::Property> prop_vec;
    for (const std::string ep_name : ep_set)
    {
        auto ep_id = GetRxEndpointId(ep_name);
        Property ep_prop;
        std::vector<int32_t> no_partitions;
        IF_ERR_RET_AAF((ami::ErrorCode)config_agent_->GetRxEndpointConfig(
            ep_name, no_partitions, &ep_prop));
        bool to_compress = ep_prop.GetValue(config::endpoint::kToCompress, false);
        rxep_map_.emplace(
            std::make_pair(ep_id, RxEndpointStuff(ep_id, ep_name, to_compress)));
        GetRxEndpointPartitions(ep_name, no_partitions);
        rx_info_map_.emplace(std::make_pair(ep_id, no_partitions));
        prop_vec.push_back(ami::Property()(kEndpointName, ep_name)(kEndpointId, ep_id)(kPartitions, no_partitions));
        if (to_compress && (!compressor_proto_ || !decompressor_proto_))
        {
            ADK_LOG_ERROR_AC_TF("rx endpoint need to be compressed, but compressor not configured",
                                "endpoint <{1}>",
                                ep_name);
            return aaf::kFailure;
        }

        ADK_LOG_INFO_AC_TF("create tx endpoint ok",
                           "endpoint_name <{1}> endpoint_id <{2}>",
                           ep_name, ep_id);
    }
    rx_ep_info_prop_.SetValue(kEndpointName, prop_vec);
    ADK_LOG_INFO_AC_TF("rx ep info", "<{1}>", rx_ep_info_prop_.Dump());

    return aaf::kSuccess;
}

int32_t AmiBridge::OnRxEndpointCreation(
    const std::string& ep_name, MessageHandler** msg_hdl, bool)
{
    return aaf::kSuccess;
}

int32_t AmiBridge::OnAmiInitEnd()
{
    ADK_LOG_INFO_AC_TF("init Ami completly", "");
    return aaf::kSuccess;
}

void AmiBridge::OnRoleChangeToMaster()
{
    ADK_LOG_INFO_AC_TF("Bridge role change to master", "");
    AMI_TD_SEND_EVENT("role change to master");
    is_disaster_ = false;
}

#ifdef TEST_FRAMEWORK_DEBUG
void AmiBridge::OnSignal(int sig_num, int value)
{
    if (sig_num == SIGUSR1)
    {
        SET_SIGNAL_HANDLE_FLAG(test_flag_, value);
    }
}
#endif

int32_t AmiBridge::OnRun()
{
    if (is_disaster_)
    {
        return aaf::kPassed;
    }

    if (tcp_stat_ == kBroken)
    {
        //主机启动流程
        if (IsLeader())
        {
            /********************* 清理资源************************
            **** 1.程序重新启动，清空资源，进入启动流程。
            **** 2.tcp断开后重连，为区分上次连接，需清空上次连接的资源。
            */
            if (send_msg_thread_.joinable())
            {
                send_msg_thread_.join();
            }
            if (send_ack_thread_.joinable())
            {
                send_ack_thread_.join();
            }
            if (backup_thread_.joinable())
            {
                backup_thread_.join();
            }
            local_sync_finish_ = false;
            peer_sync_finish_  = false;
            if (tcp_endpoint_)
            {
                tcp_endpoint_->Shutdown();
                tcp_endpoint_->Close();
            }

            /********************* 等待恢复 ************************
            **** 程序有三种启动方式：重启，重连(主备切换)，恢复
            **** 1.重启：这种启动方式重启开启了一次对话，不需要恢复上次会话数据，
            ****         is_recovery_ok为true，表示不需要等待。
            **** 2.重连：这种启动方式和上次连接是同一次会话，所有的数据并未丢失，不需要恢复，
            ****         is_recovery_ok为true，表示不需要等待。
            **** 3.恢复：这种启动方式和上次连接是同一次会话，但是数据已经丢失，需要重新恢复，
            ****         is_recovery_ok为false，表示正在恢复中，需要等待恢复完成。
            */
            while (!is_recovery_finish_)
            {
                if (is_running())
                {
                    usleep(1);
                }
                else
                {
                    StopAmiApp();
                    return aaf::kPassed;
                }
            }
            /********************* 本端同步 ************************
            ** 本端同步主要作用：
            ** 1.统一记录一个唯一的session_id,保证主备处于同一会话中。
            ** 2.该消息作为不同会话间的分隔流，保证在处理本端同步时，上一场会话的数据已经全部处理完成。
            */
            if (SyncLocalSession() != kSuccess)
            {
                ADK_LOG_ERROR_AC_TF("sync local session failed", "");
                tcp_endpoint_ = nullptr;
                return aaf::kFailure;
            }

#ifdef TEST_FRAMEWORK_DEBUG
            if (test_flag_ == SERVER_STEP_INIT)
            {
                //server端收到信号后停止30秒
                sleep(30);
            }
#endif

            //fix: 保证server端建立连接前的状态为connecting
            tcp_stat_ = kConnecting;
            ADK_BARRIER();
            //fix: 保证server端在此之后才能接收TCP连接
            tcp_endpoint_ = nullptr;

            /********************* 启动连接 ************************
            **** 使用tcp_engine建立tcp连接，如果超时，连接状态标识tcp_stat会被置为kBroken,
            **** 然后重新开始启动流程
            */
            IF_ERR_RET_AAF(InitTcpEndpoint(),
                           ADK_LOG_ERROR_AC_TF("Init tcp endpoint failed...", ""));
            ADK_LOG_INFO_AC_TF("Init tcp endpoint ok,waiting create tcp connection...", "");
            while (is_running() && tcp_stat_ == kConnecting)
            {
                if (DetectLink() != aaf::kSuccess)
                {
                    ADK_LOG_ERROR_AC_TF("bridge find manager has been exit",
                                        "errno <{1}>, desc <{2}>",
                                        errno,
                                        strerror(errno));
                    StopAmiApp();
                    return aaf::kFailure;
                }
                ::usleep(1);
            }

            /********************* 对端同步 ************************
			**** 建立连接成功后，首先向对端发送一次同步消息，PeerSyncMsg主要作用：
			**** 1. 告知对端本端的rx_endpoint的配置信息，对端根据该信息进行主题校验。
			**** 2. 告知对端本端的local_session_id，对端根据该信息决定连接类型。
			****    >>重启：表示重新建立新的会话;
			****    >>重连：表示与上次连接是同一次会话，主要包括三种情况(断链重连，主备切换，节点恢复)。
			*/
            if (is_running() && tcp_stat_ == kConnected)
            {
                if (SyncPeerSession() != kSuccess)
                {
                    AMI_TD_SEND_EVENT("SyncPeerSession failed");
                    ADK_LOG_ERROR_AC_TF("proc handshake failed...", "");
                    return aaf::kPassed;
                }

                AMI_TD_SEND_EVENT("SyncPeerSession success");
            }

            /********************* 发送消息 ************************
            **** 所有启动方式到此已经完成了消息流对齐，此时开始进行消息发送，消息发送主要包括：
            **** 1. 启动send_ack线程，该线程主要是向对端发送本端消息接收情况，除此之外，主要充当
            ****    tcp的心跳消息，首先启动该线程就是为了避免心跳超时
            **** 1. 重传消息：重启方式不会重传，只有重连方式才会进行重传。
            **** 2. 启动send_msg线程，该线程主要是向对端发送ami消息。
            */
            if (is_running() && tcp_stat_ == kConnected)
            {
                send_ack_thread_ = std::thread(std::bind(&AmiBridge::SendAckMsg, this));
                RetransmitMsg();
                send_msg_thread_ = std::thread(std::bind(&AmiBridge::SendAmiMsg, this));

                AMI_TD_SEND_EVENT("Leader init success");
            }
        }

        //备机启动流程
        while (IsBackup() && is_running())
        {
            if (tcp_stat_ == kConnected)
            {
                backup_thread_ = std::thread(std::bind(&AmiBridge::BackAmiMsg, this));
                AMI_TD_SEND_EVENT("Backup init success");
                break;
            }
        }
    }
    else if (tcp_stat_ == kConnected)
    {
        if (IsBackup() && is_running() && !backup_thread_.joinable())
        {
            backup_thread_ = std::thread(std::bind(&AmiBridge::BackAmiMsg, this));
            AMI_TD_SEND_EVENT("Backup init success");
        }
    }

    return aaf::kPassed;
}

void AmiBridge::OnIdle()
{
    if (DetectLink() != aaf::kSuccess)
    {
        ADK_LOG_ERROR_AC_TF("bridge find manager has been exit",
                            "errno <{1}>, desc <{2}>",
                            errno, strerror(errno));
        StopAmiApp();
    }

    static adk::log::IntervalLogger inv_logger(*snapshot_cycle_milli_ / 1000);

    ADK_INV_LOG_INFO_AC_TF(
        inv_logger,
        "Idle",
        "\n"
        "********************************************************************"
        "\n"
        "{1}\n"
        "********************************************************************",
        *this);
#ifdef TEST_FRAMEWORK_DEBUG
    static bool only_once = false;
    if (!only_once && test_flag_ == BACKUP_FAST_THAN_LEADER)
    {
        only_once = true;
        AMI_TD_SEND_EVENT("recv signal 5");
    }
    if (!only_once && test_flag_ == BACKUP_SLOW_THAN_LEADER)
    {
        only_once = true;
        AMI_TD_SEND_EVENT("recv signal 6");
    }
    if (!only_once && test_flag_ == CONN_BREAK_AND_EXIT)
    {
        only_once = true;
        AMI_TD_SEND_EVENT("recv signal 7");
    }
    if (!only_once && test_flag_ == SERVER_STEP_INIT)
    {
        only_once = true;
        AMI_TD_SEND_EVENT("recv signal 8");
    }
    if (test_flag_ == CONN_BREAK_AND_EXIT)
    {
        while (is_running() && tcp_stat_ == kConnected)
        {
            sleep(1);
        }
        StopAmiApp();
    }
#endif
    ::usleep(1);
}

void AmiBridge::OnAmiExitBegin()
{
    ADK_LOG_INFO_AC_TF("exit Ami begin", "");
    tcp_stat_  = kBroken;
    role_stat_ = kError;

    if (send_msg_thread_.joinable())
    {
        send_msg_thread_.join();
        ADK_LOG_INFO_AC_TF("exit thread SendAmiMsg", "");
    }
    if (send_ack_thread_.joinable())
    {
        send_ack_thread_.join();
        ADK_LOG_INFO_AC_TF("exit thread SendAckMsg", "");
    }
    if (backup_thread_.joinable())
    {
        backup_thread_.join();
        ADK_LOG_INFO_AC_TF("exit thead BackAmiMsg", "");
    }

    if (is_server_ && tcp_accepter_)
    {
        AMI_TD_SEND_EVENT("close acceptor on Exit");
        tcp_accepter_->Close();
        tcp_accepter_ = nullptr;
        ADK_LOG_INFO_AC_TF("clear tcp_accepter", "");
    }

    if (tcp_endpoint_)
    {
        tcp_endpoint_->Shutdown();
        tcp_endpoint_->Close();
        tcp_endpoint_ = nullptr;
        ADK_LOG_INFO_AC_TF("clear tcp_endpoint", "");
    }

    if (bridge_tier_channel_)
    {
        TierChannel::DestroyTierChannel(bridge_tier_channel_);
        bridge_tier_channel_ = nullptr;
        ADK_LOG_INFO_AC_TF("clear tier_channel", "");
    }
}

void AmiBridge::OnAmiExitEnd()
{
    ADK_LOG_INFO_AC_TF("exit Ami completly", "");
    adk::Monitor::UnregisterObject(
        ami::config::category::kBridge, GetApplicationName());
    if (tcp_engine_)
        TcpEngine::Destroy(tcp_engine_);

    compressor_proto_.reset();
    decompressor_proto_.reset();

    ADK_LOG_INFO_AC_TF("Bridge exit ok",
                       "total send {1} msg and receive {2} msg",
                       tcp_tx_packet_cnt_,
                       tier_rx_packet_cnt_);
    AMI_TD_SEND_EVENT("Exit success");
}

void AmiBridge::OnMessage(Message* msg)
{
    msg->forward_acquire();
    while (kSuccess != ami_rx_queue_->Push(msg) && is_running())
    {
        BRIDGE_IDLE();
    }
    static adk::log::IntervalLogger interval_logger(3);
    /* 恢复场景
    ** 将从上次commitsync的下一条开始接收AMI上的消息，将消息放入到已发送队列中，
    ** 恢复到上一场退出的状态，然后开始对齐断点。
    */
    if (!is_recovery_finish_)
    {
        ami::Message* ami_msg             = nullptr;
        RxEndpointStuff* rxep_stuff       = nullptr;
        adk::variant::VariantEntry* entry = nullptr;

        ami_rx_queue_->WaitEntry(&entry);
        char* tmp = entry->buffer;
        ami_msg   = *(Message**)tmp;

        ///1. 正确性检查
        const auto ep_id = ami_msg->get_endpoint_id();
        const auto rxep  = rxep_map_.find(ep_id);
        if (rxep != rxep_map_.end())
        {
            rxep_stuff = &rxep->second;
        }
        else
        {
            ami_rx_err_cnt_++;
            ADK_INV_LOG_WARN_AC_TF(interval_logger, "unexpected message from rx_endpoint", "endpoint_id({1})", ep_id);
            GetContext()->DeleteMessage(ami_msg);
            ami_rx_queue_->FreeEntry(entry);
            return;
        }
        ami_rx_msg_cnt_ = ami_msg->get_total_order_seq_num();  //从ami上接收的消息统计

        const auto transport_id = ami_msg->get_transport_id();
        if (!tp_map_.count(transport_id))
        {
            tp_map_.emplace(
                std::make_pair(
                    transport_id, TransportStuff(transport_id, ami_msg->get_partition_no())));
        }

        ///2. 根据配置项决定是否压缩
        AmiMsgPacket tcp_tx_packet;
        if (rxep_stuff->to_compress)
        {
            try
            {
                size_t buffer_len  = 0;
                const char* buffer = compressor_proto_->Compress(ami_msg, buffer_len);
                tcp_tx_packet      = AmiMsgPacket(ami_msg, 1, buffer, buffer_len);
            }
            catch (const std::system_error& e)
            {
                ADK_INV_LOG_ERROR_AC_TF(
                    interval_logger,
                    "compress msg failed and exit thread SendAmiMsg",
                    "ami_msg({1}), code({2}), message({3}), detail({4})",
                    ami_msg,
                    e.code(),
                    e.code().message(),
                    e.what());
                return;
            }
        }
        else
        {
            tcp_tx_packet = AmiMsgPacket(ami_msg, 0);
        }

        ///3.缓存消息
        tcp_packets_queue_->Push(tcp_tx_packet);

        ///4.记录上场数据发送序列
        tcp_tx_packet_cnt_ = tcp_tx_packet.total_sqn();

        if (tcp_tx_packet.compress())
            GetContext()->DeleteMessage(ami_msg);
        ami_rx_queue_->FreeEntry(entry);
    }
}

void AmiBridge::OnRecoverySuccess()
{
    ADK_LOG_INFO_AC_TF("init Recovery finish", "");
    //等待Tx创建完成
    while (!is_txep_finish_)
    {
        usleep(1);
    }
    if (RecoveryBreakpoint() != kSuccess)
    {
        StopAmiApp();
        ADK_LOG_ERROR_AC_TF("Recovery last session info failed", "");
        return;
    }
    ADK_LOG_INFO_AC_TF("Recovery last session info success", "");
    AMI_TD_SEND_EVENT("Recovery success");
}

void AmiBridge::OnRoleChangeToLeader()
{
    if (role_stat_ == kLeader)
    {
        return;
    }
    role_stat_ = kLeader;
    ADK_BARRIER();
    tcp_stat_ = kBroken;
    ADK_LOG_INFO_AC_TF("bridge Ami role change to leader", "");
}

void AmiBridge::OnMemberLost(const std::vector<std::string>& lost_members)
{
    ADK_LOG_INFO_AC_TF("OnMemberLost", "");
}

void AmiBridge::OnConfigureContextProperty(const std::string& context_name,
                                           bool is_ha_ctx,
                                           ami::Property& props)
{
    props.SetValue(ami::config::context::kIsDisableLoop, true);
    props.SetValue(ami::config::endpoint::kLoadBalance, false);
    props.SetValue(ami::config::context::kIsDisableTxReplay, true);
    props.SetValue("IsBridgeContext", true);
}
/*********************************************************
 *  other global functions
*/
std::ostream& operator<<(std::ostream& os, const AmiBridge& b)
{
    os << "[[ Bridge/" << b.app_name_ << " ]]\n";

    os << "send msessages:\n"
       << "    Ami == "
       << "msg(" << b.ami_rx_msg_cnt_
       << "|" << b.ami_rx_err_cnt_
       << ")/acked(" << b.last_recv_ack_seq_
       << ") ==>TcpEngine == " << b.tcp_tx_packet_cnt_ << " ==> TCP\n"
       << "    payload bytes [p/cp/tp]："
       << ByteSize(b.bridge_tx_payload_bytes_) << "/"
       << ByteSize(b.bridge_tx_cpayload_bytes_) << "/"
       << ByteSize(b.bridge_tx_bytes_) << "/"
       << ((float)b.bridge_tx_cpayload_bytes_ / b.bridge_tx_payload_bytes_) * 100 << "%\n";

    static double last_send_payload = 0;
    double per_bandwidth            = (double)(b.bridge_tx_bytes_ - last_send_payload) * 8 / 1000000ul / (*(b.snapshot_cycle_milli_) / 1000);
    last_send_payload               = b.bridge_tx_bytes_;

    os << "receive messages:\n"
       << "    TCP == "
       << "msg(" << b.tcp_rx_packet_cnt_
       << "|" << b.tcp_rx_err_cnt_
       << ") ==>TcpEngine == " << b.tcp_rep_packet_cnt_
       << " ==>TierChannel ==" << b.tier_rx_packet_cnt_
       << " ==> Ami ==>" << b.ami_tx_msg_cnt_ << "\n"
       << "    payload bytes [p/dp/tp]: "
       << ByteSize(b.bridge_rx_cpayload_bytes_) << "/"
       << ByteSize(b.bridge_rx_payload_bytes_) << "/"
       << ByteSize(b.bridge_rx_bytes_) << "\n";

    os << "indication info: \n"
       << "    ami_rx_queue length: " << b.ami_rx_queue_->length() << '\n'
       << "    tcp_packet_queue length: " << b.tcp_packets_queue_->length() << '\n'
       << "    tcp per_bandwidth: " << per_bandwidth << "Mb/s" << '\n';
    // << "    tcp_callback_cnt: " << b.message_handler_->tcp_callback_cnt_ << '\n'
    // << "    tcp_follow_up_cnt: " << b.message_handler_->tcp_follow_cnt_ << '\n'
    // << "    tcp_recv_handshake_cnt: " << b.message_handler_->tcp_recv_handshake_cnt_ <<'\n'
    // << "    tcp_recv_ack_cnt: " << b.message_handler_->tcp_recv_ack_cnt_ <<'\n'
    // << "    tcp_recv_packet_cnt: " << b.tcp_recv_packet_cn_ << '\n'
    // << "    tier_channel_recv_handshake: " << b.tier_channel_handler_->tier_callback1_cnt_<<'\n'
    // << "    tier_channel_recv_msg: " << b.tier_channel_handler_->tier_callback2_cnt_;

    auto cnt = b.rxep_map_.size();
    if (cnt > 0)
    {
        os << "\n"
           << "ami rx detail [msg/p/cp]:\n";
    }
    for (const auto& rx_ep_stuff : b.rxep_map_)
    {
        os << "    " << rx_ep_stuff.second.ep_name << ":"
           << rx_ep_stuff.second.msg_cnt << "/"
           << ByteSize(rx_ep_stuff.second.total_payload_bytes) << "/"
           << ByteSize(rx_ep_stuff.second.total_cpayload_bytes)
           << '\n';
    }

    cnt = b.txep_map_.size();
    if (cnt > 0)
    {
        os << "\n"
           << "ami tx detail [msg/p/dp]:\n";
    }
    for (const auto& tx_ep_stuff : b.txep_map_)
    {
        os << "    " << tx_ep_stuff.second.ep_name << ":"
           << tx_ep_stuff.second.msg_cnt << "/"
           << ByteSize(tx_ep_stuff.second.total_cpayload_bytes) << "/"
           << ByteSize(tx_ep_stuff.second.total_payload_bytes)
           << '\n';
    }
    return os;
}

}
}  // end namesapce ami::bridge

ami::bridge::AmiBridge g_ami_bridge_app;  //创建bridge对象
boost::optional<ami::Message::SqnType> g_total_msg_cnt;
