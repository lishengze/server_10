/**
 * @author 牛亮亮(niuliangliang@af.local)
 */
#include <math.h>
#include <ami.h>
#include <adk/log_code_base.h>
#include <adk/log.h>
#include <adk/util.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <boost/asio/buffer.hpp>

#include "bridge_base.h"
#include "bridge_tcp_handler.h"
#include "ami_bridge.h"
#include "bridge_msg.h"
// #include "ami_constant.h"

//#include "ami_bridge_base.h"

namespace ami
{
namespace bridge
{

ADK_LOG_DEFINE(ami::bridge::TcpHeartbeatHandler);

/******************************************************************
 *  AcceptHandler function
 */
TcpHeartbeatHandler::TcpHeartbeatHandler(AmiBridge* pbridge)
{
    bridge_ = pbridge;
}

void TcpHeartbeatHandler::SendHBMsg(Endpoint* endpoint)
{
    AmiMsgPacket tcp_heartbeat_msg = AmiMsgPacket();
    endpoint->SendMsg(&tcp_heartbeat_msg, tcp_heartbeat_msg.header_len());
    ADK_LOG_TRACE_AC_TF("send heartbeat,",
                        "-->reciver<{1}:{2}> message len <{3}>",
                        endpoint->remote_ip(),
                        endpoint->remote_port(),
                        tcp_heartbeat_msg.header_len());
}
uint32_t TcpHeartbeatHandler::GetPeriodMilli()
{
    return bridge_->tcp_heartbeat_inv_milli_;
}

/******************************************************************
 *  EventHandler function
 */
ADK_LOG_DEFINE(ami::bridge::TcpEventHandler);
TcpEventHandler::TcpEventHandler(AmiBridge* pbridge)
{
    bridge_ = pbridge;
}
void TcpEventHandler::OnEvent(Endpoint* endpoint, Event* event)
{
    static uint32_t tcp_abnormal_count = 0;
    switch (event->type())
    {
    case adk::io_engine::EventType::kSocketError:       ///> 底层Socket出错         error
    case adk::io_engine::EventType::kConnectFailed:     ///> 连接失败                error
    case adk::io_engine::EventType::kHeartbeatTimeout:  ///> 心跳超时           warn
    {
        if (++tcp_abnormal_count == 5)
        {
            ADK_LOG_ERROR_AC_TF("recive tcp event",
                                "event information:<{1}>,port<{2}>",
                                event->what(),
                                endpoint->remote_port());
            tcp_abnormal_count = 0;
        }
        else
        {
            ADK_LOG_WARN_AC_TF("recive tcp event",
                               "event information:<{1}>,port<{2}>",
                               event->what(),
                               endpoint->remote_port());
        }

        endpoint->Shutdown();
        bridge_->tcp_stat_ = TcpStatus::kBroken;
        AMI_TD_SEND_EVENT("Tcp break");
        break;
    }
    case adk::io_engine::EventType::kEndpointClosed:  ///> Endpoint关闭完成       info
    {
        break;
    }
    default:
        break;
    }
}

/******************************************************************
 *  AcceptHandler function
 */

ADK_LOG_DEFINE(ami::bridge::TcpAcceptHandler);
TcpAcceptHandler::TcpAcceptHandler(AmiBridge* pbridge)
{
    bridge_ = pbridge;
}
void TcpAcceptHandler::OnAccept(Endpoint* endpoint, adk::io_engine::Property& ep_props)
{
    if (nullptr != bridge_->tcp_endpoint_)
    {
        ADK_LOG_WARN_AC_TF("accept tcp connector more than one, reject this connector",
                           "old connection <{1}:{2}>, this connector <{3}:{4}>",
                           bridge_->tcp_endpoint_->remote_ip(),
                           bridge_->tcp_endpoint_->remote_port(),
                           endpoint->remote_ip(),
                           endpoint->remote_port());
        endpoint->Shutdown();
        endpoint->Close();
    }
    else
    {
        bridge_->tcp_endpoint_ = endpoint;
        ADK_LOG_INFO_AC_TF("accept tcp connector,",
                           "local addr <{1}:{2}>, remote_port <{3}:{4}>",
                           bridge_->self_ip_,
                           bridge_->self_port_,
                           endpoint->remote_ip(),
                           endpoint->remote_port());
        ADK_BARRIER();
        bridge_->tcp_stat_ = TcpStatus::kConnected;
        AMI_TD_SEND_EVENT("Connect success");
    }
}

/******************************************************************
 *  ConnectHandler function
 */
ADK_LOG_DEFINE(ami::bridge::TcpConnectHandler);
TcpConnectHandler::TcpConnectHandler(AmiBridge* pbridge)
{
    bridge_ = pbridge;
}

void TcpConnectHandler::OnConnect(Endpoint* endpoint, adk::io_engine::Property& ep_props)
{
    struct sockaddr_in sock_name;
    socklen_t addrlen = sizeof(sockaddr);
    getsockname(endpoint->endpoint_id(), (sockaddr*)&sock_name, &addrlen);
    std::string local_ip = inet_ntoa(sock_name.sin_addr);
    uint16_t local_port  = ntohs(sock_name.sin_port);

    ADK_LOG_INFO_AC_TF("connect tcp server,",
                       "local addr <{1}:{2}>, remote_addr <{3}:{4}>",
                       local_ip,
                       local_port,
                       endpoint->remote_ip(),
                       endpoint->remote_port());
    bridge_->tcp_stat_ = TcpStatus::kConnected;
    AMI_TD_SEND_EVENT("Connect success");
}

/******************************************************************
 *  MessageHandler function
 */
ADK_LOG_DEFINE(ami::bridge::TcpMessageHandler);
TcpMessageHandler::TcpMessageHandler(AmiBridge* pbridge)
{
    bridge_ = pbridge;
}

int32_t TcpMessageHandler::HandleSyncMsg(PeerSyncMsg* peer_sync_msg, uint32_t& recv_len)
{
    struct sockaddr_in sock_name;
    socklen_t addrlen = sizeof(sockaddr);
    getsockname(bridge_->tcp_endpoint_->endpoint_id(), (sockaddr*)&sock_name, &addrlen);
    std::string local_ip = inet_ntoa(sock_name.sin_addr);
    uint16_t local_port  = ntohs(sock_name.sin_port);
    ADK_LOG_INFO_AC_TF("receive peer sync msg",
                       "session_id <{1}>, recv_seq <{2}>, send_seq <{3}>",
                       peer_sync_msg->session_id(),
                       peer_sync_msg->recv_packet_sqn(),
                       peer_sync_msg->send_packet_sqn());

    bridge_->MakePeerRxEndPointInfo(peer_sync_msg);
    ///< 1.主题校验
    if (CheckEndpoints(peer_sync_msg) != kSuccess)
    {
        AMI_TD_SEND_EVENT("check endpoints failed");
        ADK_LOG_ERROR_AC_TF("check endpoints config failed", "");
        return kFailure;
    }
    ADK_LOG_INFO_AC_TF("check endpoints config success", "");

    /*    本端重启或对端重启,需要通过TierChannel同步到本集群
    **    1.如果是对端重启，确保本集群维护同一个peer_session_id
    **    2.主备需要根据对端信息对齐断点
    */
    if (bridge_->peer_session_id_ != peer_sync_msg->session_id())
    {
        ADK_LOG_INFO_AC_TF("confirm bridge run_status is restart",
                           "<{1}> is not equal <{2}>",
                           bridge_->peer_session_id_,
                           peer_sync_msg->session_id());

        bridge_->run_type_ = kRestart;

        RepMessage* tier_peer_sync_msg = bridge_->bridge_tier_channel_->NewMessage(peer_sync_msg->total_len());
        memcpy(tier_peer_sync_msg->data(), peer_sync_msg, peer_sync_msg->total_len());
        tier_peer_sync_msg->set_data_len(peer_sync_msg->total_len());
        while (bridge_->bridge_tier_channel_->ReplicateMsg(tier_peer_sync_msg) != kSuccess)
        {
            if (bridge_->is_running())
            {
                continue;
            }
            ADK_LOG_ERROR_AC_TF("Tier channel replicate sync msg failed...", "");
            return kFailure;
        }
    }
    /*    重连分为恢复和主备切换。不需要通过TierChannel同步到集群备节点
    **    1. 恢复场景包括发送端恢复和接收端恢复，恢复成功所有的断点在RecoveryBreakPoint中已经对齐
    **      1.1 作为发送端，收到对端的同步消息，断点只存在已发送队列tcp_package_queue中，需要去重后进行重传。
    **      1.2 作为接受端，通过恢复的断点决定下次接收的消息
    **    2. 主备切换场景包括发送端切换和接收端切换
    **      2.1 作为发送端，存在三种情况。
    **        2.1.1 备节点比主节点快，对端希望接收的消息已经从已发送队列中释放，需要从recorder读取中重传。
    **           2.1.2 备节点比主节点慢，对端希望接收的消息还没有从AMI接收入队，需要接收到断点处，然后续传。
    **           2.1.3 备节点和主节点基本一致，断点存在已发送队列tcp_package_queue中，需要去重后进行重传。
    **      2.2 作为接受端，需要将tcp的断点同接收消息序号对齐，通过当前断点决定下次接收的消息。
    */
    else
    {
        ADK_LOG_INFO_AC_TF("confirm bridge run_status is reconnect",
                           "<{1}> is equal <{2}>",
                           bridge_->peer_session_id_,
                           peer_sync_msg->session_id());

        bridge_->run_type_ = kReconnect;

        AmiMsgPacket packet;
        int32_t pop_num      = peer_sync_msg->recv_packet_sqn() - bridge_->last_recv_ack_seq_;  //重复消息个数
        int32_t queue_length = bridge_->tcp_packets_queue_->length();                           //已发送队列长度

        ///< 发送端对齐
        //2.1.1
        if (pop_num < 0)
        {
            bridge_->recorder_retransmit_begin_ = peer_sync_msg->recv_packet_sqn() + 1;
            bridge_->recorder_retransmit_num_   = abs(pop_num);
            AMI_TD_SEND_EVENT("Backup Fast");
            ADK_LOG_INFO_AC_TF("break point has been pop from tcp_packet_queue",
                               "need retransmit msg from sqn <{1}> to <{2}> in recorder",
                               bridge_->recorder_retransmit_begin_,
                               bridge_->recorder_retransmit_begin_ + abs(pop_num));
        }
        else
        {
            //2.1.3 & 1.1
            ADK_LOG_INFO_AC_TF("break point is still in tcp_packet_queue",
                               "need pop num <{1}> msg from length <{2}> queue",
                               pop_num,
                               queue_length);
            while (pop_num > 0 && queue_length > 0)
            {
                while (kSuccess != bridge_->tcp_packets_queue_->Pop(packet))
                {
                    if (bridge_->is_running())
                    {
                        BRIDGE_IDLE();
                        continue;
                    }
                    else
                    {
                        ADK_LOG_ERROR_AC_TF("pop from packet_queue failed",
                                            "packet_queue len <{1}>",
                                            queue_length);
                        return kFailure;
                    }
                }
                if (packet.compress())
                {
                    delete[] packet.data();
                }
                else
                {
                    bridge_->GetContext()->DeleteMessage((Message*)(packet.data()));
                }
                queue_length--;
                pop_num--;
            }
            // 2.1.2
            while (pop_num > 0 && queue_length == 0)
            {
                static bool is_first = true;
                if (is_first)
                {
                    AMI_TD_SEND_EVENT("Backup Slow");
                    ADK_LOG_INFO_AC_TF("break point is still in ami_msg_queue",
                                       "need pop num <{1}> msg from ami_msg_queue",
                                       pop_num);
                    is_first = false;
                }
                ami::Message* msg                 = nullptr;
                adk::variant::VariantEntry* entry = nullptr;
                while (kSuccess != bridge_->ami_rx_queue_->WaitEntry(&entry))
                {
                    if (bridge_->is_running())
                    {
                        BRIDGE_IDLE();
                        continue;
                    }
                    else
                    {
                        ADK_LOG_ERROR_AC_TF("pop from ami_rx_queue failed",
                                            "packet_queue len <{1}>",
                                            bridge_->ami_rx_queue_->length());
                        return kFailure;
                    }
                }
                char* tmp                = entry->buffer;
                msg                      = *(Message**)tmp;
                bridge_->ami_rx_msg_cnt_ = msg->get_total_order_seq_num();  //从ami上接收的消息
                bridge_->GetContext()->DeleteMessage(msg);
                bridge_->ami_rx_queue_->FreeEntry(entry);
                pop_num--;
            }
        }

        bridge_->tcp_tx_packet_cnt_ = peer_sync_msg->recv_packet_sqn();  //发送消息断点
        bridge_->last_recv_ack_seq_ = peer_sync_msg->recv_packet_sqn();  //ACK消息断点

        ///< 接收端对齐
        // 2.1.2
        bridge_->tcp_rx_packet_cnt_  = bridge_->tier_rx_packet_cnt_;  //接收到的消息断点
        bridge_->tcp_rep_packet_cnt_ = bridge_->tier_rx_packet_cnt_;  //复制给tierchannel的消息断点

        ADK_BARRIER();
        bridge_->peer_sync_finish_ = true;  //通知主线程handshake消息处理完成

        ADK_LOG_INFO_AC_TF("sync msg break point completely",
                           "will send msg <{1}> and expect recv msg <{2}>",
                           bridge_->last_recv_ack_seq_ + 1,
                           bridge_->tcp_rx_packet_cnt_ + 1);

#ifdef TEST_FRAMEWORK_DEBUG
        bridge_->last_break_ = bridge_->tier_rx_packet_cnt_;  //测试参数
#endif
    }

    recv_len += peer_sync_msg->total_len();
    return kSuccess;
}

int32_t TcpMessageHandler::HandleAckMsg(TcpAckMsg* ack_msg, uint32_t& recv_len)
{
    ADK_LOG_TRACE_AC_TF("receive TcpAck msg,",
                        "last recv sqn <{1}>, now recv sqn <{2}>",
                        bridge_->last_recv_ack_seq_,
                        ack_msg->recv_packet_sqn());
    //todo 保证处理该消息应该在同步完成之后
    AmiMsgPacket packet;
    Message::SqnType current_recv_ack_seq = ack_msg->recv_packet_sqn();
    Message::SqnType last_recv_ack_seq    = bridge_->last_recv_ack_seq_;
    int32_t num                           = current_recv_ack_seq - last_recv_ack_seq;
    while (num > 0)
    {
        while (kSuccess != bridge_->tcp_packets_queue_->Pop(packet))
        {
            BRIDGE_IDLE();
        }

        if (packet.compress())
        {
            delete[] packet.data();
        }
        else
        {
            bridge_->GetContext()->DeleteMessage((Message*)(packet.data()));
        }
        num--;
    }
    bridge_->GetContext()->CommitSyncBefore(current_recv_ack_seq);
    bridge_->last_recv_ack_seq_ = current_recv_ack_seq;
    recv_len += sizeof(TcpAckMsg);

    return kSuccess;
}

int32_t TcpMessageHandler::HandleAmiMsg(AmiMsgPacket* tcp_rx_packet, uint32_t& recv_len)
{
    uint64_t recv_sqn = tcp_rx_packet->total_sqn();
    uint32_t ep_id    = tcp_rx_packet->endpoint_id();
    int32_t exp_pno   = tcp_rx_packet->partition_no();
    static adk::log::IntervalLogger interval_logger(3);

    // 1.消息校验
    TxEndpointStuff* txep_stuff = nullptr;
    auto txep                   = bridge_->peer_rxep_map_.find(ep_id);
    if (txep != bridge_->peer_rxep_map_.end())
    {
        txep_stuff = txep->second.local_txep_stuff;
    }
    else
    {
        ADK_INV_LOG_WARN_AC_TF(interval_logger, "check msg error:",
                                "msg endpoint_id ({1}) not exist", ep_id);
        bridge_->tcp_rx_err_cnt_++;
        recv_len += tcp_rx_packet->total_len();
        return kSuccess;
    }

    bridge_->tcp_rx_packet_cnt_ = recv_sqn;  //tcp接收消息统计

    // 2.判断主题负载均衡
    if (txep_stuff->is_loadbalance)
    {
        int32_t pno;
        int32_t exp_pno = tcp_rx_packet->partition_no();
        if (kSuccess == txep_stuff->ep_handler->endpoint_->SelectLoadBalancePartition(pno, exp_pno))
        {
            exp_pno = pno;
        }
        else
        {
            exp_pno = 0;
        }
        tcp_rx_packet->set_partition_no(exp_pno);
    }

    // 3.发送给tierchannel
    uint32_t total_len     = tcp_rx_packet->total_len();
    RepMessage* ami_packet = bridge_->bridge_tier_channel_->NewMessage(total_len);
    memcpy(ami_packet->data(), tcp_rx_packet, total_len);
    ami_packet->set_data_len(total_len);
    while (bridge_->bridge_tier_channel_->ReplicateMsg(ami_packet) != kSuccess)
    {
        if (bridge_->is_running())
        {
            continue;
        }
        else
        {
            ADK_LOG_ERROR_AC_TF("tier_channel replicate msg failed", "");
            return kFailure;
        }
    }

    bridge_->tcp_rep_packet_cnt_ = tcp_rx_packet->total_sqn();  //复制tierchannel消息统计
    recv_len += tcp_rx_packet->total_len();

    return kSuccess;
}

int32_t TcpMessageHandler::CheckEndpoints(PeerSyncMsg* handshake_msg)
{
    if (handshake_msg->ep_info_size() != 0)
    {
        ADK_LOG_ERROR_AC_TF("enpoint check failed", "peer bridge version is too old");
        return kFailure;
    }

    for (auto& endpoint : bridge_->peer_rxep_map_)
    {
        // 本端tx存在对端rx的主题名
        if (endpoint.second.local_txep_stuff == nullptr)
        {
            ADK_LOG_ERROR_AC_TF("enpoint check failed",
                                "cannot find  peer ep_name <{1}> in txep_map_ ",
                                endpoint.second.ep_name);
            return kFailure;
        }

        auto local_ep_id    = endpoint.second.local_txep_stuff->ep_id;
        auto partition_iter = bridge_->tx_info_map_.find(local_ep_id);
        // 本端tx主题的partition_no集合要包含对端rx主题的所有partition_no，只多不少
        if (partition_iter == bridge_->tx_info_map_.end() || endpoint.second.partitions.size() > partition_iter->second.size())
        {
            ADK_LOG_ERROR_AC_TF("endpoint check failed",
                                "number of partition in local ep_id <{1}> is not equal, peer ep_name <{2}>, ep_id <{3}>",
                                local_ep_id,
                                endpoint.second.ep_name,
                                endpoint.second.ep_id);
            return kFailure;
        }
        // 遍历对端rx主题的所有partition_no，能够在本端的相同tx主题中找到
        for (auto& partition : endpoint.second.partitions)
        {
            if (std::find(partition_iter->second.begin(), partition_iter->second.end(), partition) == partition_iter->second.end())
            {
                ADK_LOG_ERROR_AC_TF("endpoint check failed",
                                    "cannot find local ep_id <{1}>, partition <{2}>",
                                    local_ep_id,
                                    partition);
                return kFailure;
            }
            else
            {
                ADK_LOG_TRACE_AC_TF("find partition in tx_map",
                                    "local ep_id <{1}> and partition<{2}>",
                                    local_ep_id,
                                    partition);
                break;
            }
        }
    }
    return kSuccess;
}

int32_t TcpMessageHandler::OnMessage(adk::io_engine::Message* message)
{
    char* msg                 = message->data();
    uint32_t message_data_len = message->data_len();
    uint32_t recv_len         = 0u;
    uint32_t left_len         = message_data_len;

    while (true)
    {
        left_len = message_data_len - recv_len;
        // 当前消息接收完成，退出等待下一条消息
        if (0 == left_len)
            break;
        //消息剩余长度无法判断消息类型
        if (left_len < sizeof(ami::bridge::MsgType))
        {
            message->set_follow_up(recv_len, -1);
            return kFollowUp;
        }
        volatile MsgType* type = (volatile MsgType*)(msg + recv_len);
        switch (*type)
        {
        case MsgType::kPeerSync:
        {
            PeerSyncMsg* peer_sync_msg = (PeerSyncMsg*)(msg + recv_len);
            //包长小于package消息头部或不足一个消息长度
            if (left_len < peer_sync_msg->total_len())
            {
                message->set_follow_up(recv_len, -1);
                return kFollowUp;
            }
            if (kSuccess != HandleSyncMsg(peer_sync_msg, recv_len))
            {
                ADK_LOG_ERROR_AC_TF("handle breakpoint msg failed", "");
                bridge_->StopAmiApp();
                return kSuccess;
            }
            break;
        }
        case MsgType::kTcpAck:
        {
            TcpAckMsg* ack_msg = (TcpAckMsg*)(msg + recv_len);
            if (left_len < ack_msg->total_len())
            {
                message->set_follow_up(recv_len, -1);
                return kFollowUp;
            }
            if (kSuccess != HandleAckMsg(ack_msg, recv_len))
            {
                ADK_LOG_ERROR_AC_TF("handle ack msg failed", "");
                bridge_->StopAmiApp();
                return kSuccess;
            }
            break;
        }
        case MsgType::kAmiPacket:
        {
            AmiMsgPacket* ami_packet_msg = (AmiMsgPacket*)(msg + recv_len);
            //包长小于package消息头部
            if (left_len < ami_packet_msg->header_len())
            {
                message->set_follow_up(recv_len, -1);
                return kFollowUp;
            }
            if (left_len < ami_packet_msg->total_len())
            {
                message->set_follow_up(recv_len, ami_packet_msg->total_len() - left_len);
                return kFollowUp;
            }
            if (kSuccess != HandleAmiMsg(ami_packet_msg, recv_len))
            {
                ADK_LOG_ERROR_AC_TF("handle ami msg failed", "");
                bridge_->StopAmiApp();
                return kSuccess;
            }
            break;
        }
        default:
        {
            MsgType msg_type = static_cast<MsgType>(*type);
            ADK_LOG_ERROR_AC_TF("unkown message type", "type<{1}>,sqn<{2}>", msg_type, bridge_->tcp_rx_packet_cnt_);
            bridge_->StopAmiApp();
            return kSuccess;
        }
        }
    }
    return kSuccess;
}

}
}
