/**
 * @author 牛亮亮(niuliangliang@af.local)
 */

#ifndef AMI_BRIDGE_APP_H_
#define AMI_BRIDGE_APP_H_

/// cpp std
#include <string>         //< string
#include <memory>         //< unique_ptr
#include <unordered_map>  //< unordered_map
#include <thread>         //< thread
#include <mutex>
#include <chrono>  //chrono

/// boost
#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <boost/timer/timer.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>

/// ami, adk public
#include <ami.h>  //< message
#include <aaf.h>  //< endpointHandler
#include <aaf/log_code_base.h>
#include <adk/io_engine.h>                          //< TcpEngine
#include <adk/lock_free_queue_variant.h>            //< variant::SPSCQueue
#include <adk/lock_free_unbounded_queue_variant.h>  //variant::SPSCUnboundedQueue
#include <adk/lock_free_msg_queue.h>                //<SPSCQueue
#include <adk/log.h>                                //< ADK_LOG_INFO_AC_AF
#include <adk/pipe.h>
#include <ami/tier_channel.h>

/// impl
#include "bridge_base.h"
#include "bridge_msg.h"  //AmiMsgPacket
#include "compressor.h"  //compressor
#include "config_agent.h"

namespace ami
{
namespace bridge
{

//declare class
class TcpHeartbeatHandler;
class TcpEventHandler;
class TcpAcceptHandler;
class TcpConnectHandler;
class TcpMessageHandler;
class BridgeTierChannelHandler;

class AmiBridge : public aaf::GenericAmiApplication
{
public:
    typedef std::chrono::steady_clock Clock;
    typedef Clock::time_point TimePoint;
    typedef adk::variant::SPSCQueue<Message*> AmiMessageQueue;
    typedef adk::variant::SPSCUnboundedQueue<AmiMsgPacket> TcpPacketsQueue;
    static const int32_t kLogCodeBaseApp = 90000;

public:
    AmiBridge() {}
    ~AmiBridge() {}

private:
    /***************************************************************************
     * override OnXXX callbacks of base class GenericAmiApplication
     */
    virtual void OnConfigureFramework(Property& fw_props);
    virtual void SetAmiAppOption();
    virtual void OnAmiAppOption(const std::string& option_name);
    virtual int32_t OnAmiInitBegin();
    virtual int32_t OnTxEndpointCreationBegin();
    virtual int32_t OnTxEndpointCreation(aaf::EndpointHandler*, const std::string&);
    virtual int32_t OnRxEndpointCreationBegin();
    virtual int32_t OnRxEndpointCreation(const std::string& ep_name, MessageHandler** msg_hdl, bool);
    virtual int32_t OnAmiInitEnd();
    virtual int32_t OnRun();
    virtual void OnIdle();
    virtual void OnAmiExitBegin();
    virtual void OnAmiExitEnd();
    virtual void OnMessage(Message* msg);
    virtual void OnRecoverySuccess();
    virtual void OnRoleChangeToLeader();
    virtual void OnRoleChangeToMaster();
    virtual void OnMemberLost(const std::vector<std::string>& lost_members);
    virtual void OnConfigureContextProperty(const std::string& context_name,
                                            bool is_ha_ctx,
                                            ami::Property& props);
#ifdef TEST_FRAMEWORK_DEBUG
    virtual void OnSignal(int sig_num, int value);
#endif
    /***************************************************************************
     * self-defining function of class AmiBridge
     */
    std::error_code Init();
    std::error_code InitTcpEndpoint();
    std::error_code InitTcpEngine();
    std::error_code SyncLocalSession();
    std::error_code SyncPeerSession();
    std::error_code RecoveryBreakpoint();
    std::error_code RetransmitMsg();
    int32_t DirectSendMsg(AmiMsgPacket& packet);
    int32_t FragmentSendMsg(AmiMsgPacket& packet);
    bool OnCollectIndicator(boost::property_tree::ptree& status_tree);
    void SendAmiMsg();  //发送线程
    void SendAckMsg();  //ack线程
    void BackAmiMsg();  //备机线程

    Message::IDType GetTxEndpointId(const std::string& ep_name)
    {
        Message::IDType ep_id = 0;
        GetContext()->PropertyAt(config::context::kTxEndpoint, ep_name)
                                (config::endpoint::kId)
                                .GetValue(ep_id);
        return ep_id;
    }

    Message::IDType GetRxEndpointId(const std::string& ep_name)
    {
        Message::IDType ep_id = 0;
        GetContext()->PropertyAt(config::context::kRxEndpoint, ep_name)
                                (config::endpoint::kId)
                                .GetValue(ep_id);
        return ep_id;
    }

    inline int32_t Send(adk::io_engine::Message* message)
    {
        uint64_t send_retries = 0;
        // 非阻塞发送，用于检测重试次数，控制退出
        while (kSuccess != tcp_endpoint_->SendMsg<false>(message))
        {
            if (tcp_stat_ == kConnected)
            {
                BRIDGE_IDLE();
                ++send_retries;
            }
            else
            {
                return kFailure;
            }
        }
        if (ADK_UNLIKELY(send_retries))
        {
            ++tcp_tx_err_cnt_;
            tcp_tx_max_retries_ = std::max(tcp_tx_max_retries_, send_retries);
        }
        return kSuccess;
    }

    inline int32_t DetectLink()
    {
        char buff;
        if (close_fd_ != -1)
        {
            int32_t ret = read(close_fd_, &buff, 1);
            if (ret == -1 && (errno == EWOULDBLOCK || errno == EAGAIN))
            {
                return aaf::kSuccess;
            }
            else
            {
                return aaf::kFailure;
            }
        }

        return aaf::kSuccess;
    }

public:
    bool IsLeader();
    bool IsBackup();
    std::error_code WriteSessionFile(uint64_t local_session_id, uint64_t peer_session_id);
    std::error_code ReadSessionFile(uint64_t& local_session_id, uint64_t& peer_session_id);
    void MakePeerRxEndPointInfo(PeerSyncMsg* handshake_msg);

public:
    /*************************************
     * 配置项
     */
    std::unique_ptr<ConfigAgent> config_agent_;
    std::string domain_server_;
    std::string app_name_;
    std::string context_name_;
    std::string tier_channel_name_;
    bool is_server_      = false;
    bool is_disaster_    = false;
    bool is_tcp_nodelay_ = false;
    static bool s_is_low_latency;
    std::string self_ip_       = "127.0.0.1";
    unsigned short self_port_  = 30001;
    unsigned short local_port_ = 0;
    std::vector<std::string> peer_ip_;
    std::vector<unsigned short> peer_port_;
    boost::optional<size_t> snapshot_cycle_milli_;
    boost::filesystem::path compress_lib_path_;
    std::string compress_config_;
    size_t tcp_heartbeat_inv_milli_;
    size_t tcp_timeout_multiplier_;
    size_t ami_rx_queue_len_;
    //size_t packets_record_QLen_;
    size_t ack_step_len_;
    int32_t close_fd_;
    bool is_rate_ctl_               = false;
    int32_t net_bandwidth_permicro_ = 0;  // bit/us
    int32_t ratectl_inv_micro_      = 0;
    uint32_t fragment_size_         = 0;  // Byte
    std::mutex mtx_;
    uint32_t test_flag_ = 0;

    /*************************************
     * 流程控制标识
     */
    InitType init_type_               = kBootstrap;  //Bridge启动方式(启动或恢复)
    RunType run_type_                 = kUnknow;     //Bridge运行方式(重启或重连)
    uint64_t local_session_id_        = 0u;          //本地集群会话id
    uint64_t peer_session_id_         = 0u;          //对端集群会话id
    volatile bool is_txep_finish_     = false;       //tx_endpoint创建完成标识
    volatile bool is_recovery_finish_ = true;        //消息恢复完成标识
    volatile bool local_sync_finish_  = false;       //本地同步完成标识
    volatile bool peer_sync_finish_   = false;       //对端同步完成标识
    volatile TcpStatus tcp_stat_      = kBroken;     //tcp连接状态
    volatile RoleStatus role_stat_    = kBackup;     //主备角色

    /*************************************
     * 从ami总线上收消息并且发送到tcp链路
     */
    ami::Property rx_ep_info_prop_;  // 保存本端配置中的endpoint信息，用于peer_sync
    RxEndpointMapType rxep_map_;
    EndpointInfoMapType rx_info_map_;
    TransportMapType tp_map_;
    AmiMessageQueue* ami_rx_queue_       = nullptr;
    TcpPacketsQueue* tcp_packets_queue1_ = nullptr;
    TcpPacketsQueue* tcp_packets_queue2_ = nullptr;
    TcpPacketsQueue* tcp_packets_queue_  = nullptr;
    std::unique_ptr<Compressor> compressor_proto_;

    Message::SqnType ami_rx_msg_cnt_           = 0u;  //从ami接收到的消息数
    Message::SqnType ami_rx_err_cnt_           = 0u;  //从ami接收到的非法消息数
    Message::SqnType tcp_tx_packet_cnt_        = 0u;  //发送到tcp的消息数
    Message::SqnType tcp_tx_err_cnt_           = 0u;  //发送到tcp失败的消息次数
    Message::SqnType tcp_tx_max_retries_       = 0u;  //发送到tcp最大重试次数（每个指标周期内）
    Message::SqnType last_recv_ack_seq_        = 0u;  //前一次接收到的ACK确认号
    Message::SqnType bridge_tx_payload_bytes_  = 0u;
    Message::SqnType bridge_tx_cpayload_bytes_ = 0u;
    Message::SqnType bridge_tx_bytes_          = 0u;

    std::thread send_msg_thread_;
    std::thread send_ack_thread_;
    std::thread backup_thread_;

    /*************************************
     * 从tcp链路上收消息并且发送到ami总线
     */
    PeerRxEndpointMapType peer_rxep_map_;
    TxEndpointMapType txep_map_;
    EndpointInfoMapType tx_info_map_;
    std::unique_ptr<Decompressor> decompressor_proto_;

    Message::SqnType tcp_rx_packet_cnt_        = 0u;  //从tcp接收的消息数
    Message::SqnType tcp_rx_err_cnt_           = 0u;  //从tcp接收的非法消息数
    Message::SqnType tcp_rep_packet_cnt_       = 0u;  //复制给tierchannel的消息数
    Message::SqnType tier_rx_packet_cnt_       = 0u;  //tierchannel接收到的消息数
    Message::SqnType ami_tx_msg_cnt_           = 0u;  //tierchannel发送到ami的消息数
    Message::SqnType last_send_ack_seq_        = 0u;  //前一次发送的确认号
    Message::SqnType bridge_rx_payload_bytes_  = 0u;
    Message::SqnType bridge_rx_cpayload_bytes_ = 0u;
    Message::SqnType bridge_rx_bytes_          = 0u;

    /*************************************
     * tcp链路相关
     */
    adk::io_engine::TcpEngine* tcp_engine_  = nullptr;
    adk::io_engine::Acceptor* tcp_accepter_ = nullptr;
    adk::io_engine::Endpoint* tcp_endpoint_ = nullptr;
    TcpHeartbeatHandler* heartbeat_handler_ = nullptr;
    TcpEventHandler* event_handler_         = nullptr;
    TcpMessageHandler* message_handler_     = nullptr;
    TcpConnectHandler* connect_handler_     = nullptr;
    TcpAcceptHandler* accept_handler_       = nullptr;

    /*************************************
     * 高可用相关
     */
    BridgeTierChannelHandler* tier_channel_handler_ = nullptr;
    TierChannel* bridge_tier_channel_               = nullptr;
    std::mutex mutex_tier_channel_;
    uint32_t recorder_retransmit_num_   = 0u;
    uint32_t recorder_retransmit_begin_ = 0u;

#ifdef TEST_FRAMEWORK_DEBUG
    uint32_t last_break_ = 0;
#endif

    ADK_LOG_DECLARE_AC(kLogCodeBaseApp);

    friend std::ostream& operator<<(std::ostream&, const AmiBridge&);
};

}
}  //namespace ami::bridge

#endif  // AMI_BRIDGE_APP_H_
