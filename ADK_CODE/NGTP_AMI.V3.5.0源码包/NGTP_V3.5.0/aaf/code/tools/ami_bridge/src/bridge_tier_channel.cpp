/**
 * @author 牛亮亮(niuliangliang@af.local)
 */

#include "ami_bridge.h"
#include "bridge_tier_channel.h"
#include "bridge_base.h"

namespace ami
{
namespace bridge
{

ADK_LOG_DEFINE(ami::bridge::BridgeTierChannelHandler);

void BridgeTierChannelHandler::RoleChange(int32_t tc_role, const Property& role_props)
{
    ADK_LOG_INFO_AC_TF("bridge tierchannel role change to", "<{1}>", tc_role);
    tc_role_ = tc_role;
    if (tc_role_ == kTcRoleError)
    {
        ADK_LOG_ERROR_AC_TF("bridge tierchannel role change to TcRoleError", "");
        bridge_->StopAmiApp();
    }
}

void BridgeTierChannelHandler::OnMessage(RepMessage* const message)
{
    MsgType* type = (MsgType*)(message->data());
    switch (*type)
    {
    case MsgType::kLocalSync:
    {
        LocalSyncMsg* local_sync_msg = (LocalSyncMsg*)(message->data());
        bridge_->local_session_id_   = local_sync_msg->session_id();
        bridge_->local_sync_finish_  = true;
        ADK_LOG_INFO_AC_TF("tier_channel sync local session_id finish",
                           "orig local_session_id <{1}> --> session_id <{2}>",
                           bridge_->local_session_id_,
                           local_sync_msg->session_id());
        break;
    }
    /*    处理对端Bridge发送的同步信息
    **    （只有重启场景下，才收到对端的同步信息）
    **    1.作为发送端，将已发送消息队列进行清空，重新计算发送断点。（备节点不需要清空）
    **    2.作为接收端，根据同步消息确定下一次需要接收的断点。
    **    3.记录本次连接的session_id
    */
    case MsgType::kPeerSync:
    {
        ADK_LOG_INFO_AC_TF("tierchannel receive peer_sync_msg", "");

        PeerSyncMsg* peer_sync_msg = (PeerSyncMsg*)(message->data());

        if (!bridge_->IsLeader())
        {
            // 主机要在tcp的Onmessage中的peer sync时更新好ep_id映射关系，否则会在这里出现时序问题
            bridge_->MakePeerRxEndPointInfo(peer_sync_msg);
        }

        //> 1.发送端(主节点)重新计算接收断点
        if (bridge_->run_type_ == kRestart && bridge_->IsLeader())
        {
            ADK_LOG_INFO_AC_TF("need clear tcp_packets_queue",
                               "length <{1}>",
                               bridge_->tcp_packets_queue_->length());
            //清空已发送消息队列
            AmiMsgPacket packet;
            while (bridge_->tcp_packets_queue_->Pop(packet) == kSuccess)
            {
                if (packet.compress())
                {
                    delete[] packet.data();
                }
                else
                {
                    bridge_->GetContext()->DeleteMessage((Message*)(packet.data()));
                }
            }
            //重新计算发送断点
            bridge_->tcp_tx_packet_cnt_ = bridge_->ami_rx_msg_cnt_;
            bridge_->last_recv_ack_seq_ = bridge_->ami_rx_msg_cnt_;
        }

        //> 2.接收端重新计算接收断点
        /*    如果是对端重启，则对端发送的同步断点都为0，本端接收断点都修改为0
        **    如果是本端重启，则对端发送的同步断点为上次发送的断点，本端将接收断点都对齐
        **    到该位置，表示即将接收断点下一条消息
        */
        bridge_->tcp_rx_err_cnt_     = 0;
        bridge_->tcp_rx_packet_cnt_  = peer_sync_msg->send_packet_sqn();
        bridge_->tcp_rep_packet_cnt_ = peer_sync_msg->send_packet_sqn();
        bridge_->tier_rx_packet_cnt_ = peer_sync_msg->send_packet_sqn();
        bridge_->ami_tx_msg_cnt_     = peer_sync_msg->send_packet_sqn();
        bridge_->last_send_ack_seq_  = peer_sync_msg->send_packet_sqn();

        ADK_LOG_INFO_AC_TF("sync msg break point completely",
                           "will send msg <{1}> and expect msg <{2}>",
                           bridge_->last_recv_ack_seq_ + 1,
                           bridge_->tier_rx_packet_cnt_ + 1);

        ADK_BARRIER();

        //> 3.记录本次连接的session_id
        bridge_->peer_session_id_ = peer_sync_msg->session_id();
        bridge_->WriteSessionFile(bridge_->local_session_id_, bridge_->peer_session_id_);
        ADK_LOG_INFO_AC_TF("write session id into file success", "");
        bridge_->peer_sync_finish_ = true;  //通知主节点主线程handshake消息处理完成
        if (bridge_->IsBackup())
        {
            bridge_->tcp_stat_ = kConnected;  //通知备节点handshake消息处理完成
        }

#ifdef TEST_FRAMEWORK_DEBUG
        bridge_->last_break_ = bridge_->tier_rx_packet_cnt_;  //测试参数
#endif

        break;
    }
    case MsgType::kAmiPacket:
    {
        AmiMsgPacket* rx_packet = (AmiMsgPacket*)(message->data());
        uint32_t head_len       = rx_packet->header_len();
        uint32_t data_len       = rx_packet->data_len();
        uint32_t orig_data_len  = rx_packet->orig_data_len();
        uint32_t total_len      = rx_packet->total_len();
        uint64_t recv_sqn       = rx_packet->total_sqn();
        uint32_t ep_id          = rx_packet->endpoint_id();
        int32_t exp_pno         = rx_packet->partition_no();
        uint64_t ancestor_id    = rx_packet->ancestor_id();
        static adk::log::IntervalLogger inv_logger(3u);

        /// 1.消息校验
        if (recv_sqn != bridge_->tier_rx_packet_cnt_ + 1)
        {
            ADK_LOG_ERROR_AC_TF("recv unexpect msg, maybe lost or resend msg",
                                "recv_sqn({1}),expect_sqn({2})",
                                recv_sqn,
                                bridge_->tier_rx_packet_cnt_ + 1);
            bridge_->StopAmiApp();
            return;
        }

        bridge_->tier_rx_packet_cnt_ = recv_sqn;

        /// 4.将tcp_packet转化成ami_msg
        TxEndpointStuff* txep_stuff = (bridge_->peer_rxep_map_.find(ep_id)->second.local_txep_stuff);
        Message* ami_msg            = aaf::NewMessage(
                                        txep_stuff->ep_handler, orig_data_len);

        if (ami_msg == nullptr)
        {
            ADK_LOG_ERROR_AC_TF("NewMessage failed", "");
            bridge_->StopAmiApp();
            return;
        }

        char* packet = (char*)rx_packet;  //首先转换为字符串，以实现地址偏移
        if (txep_stuff->to_compress)
        {
            try
            {
                bridge_->decompressor_proto_->Decompress(
                            packet + head_len, data_len,
                            orig_data_len, ami_msg);
            }
            catch (const std::system_error& e)
            {
                ADK_INV_LOG_ERROR_AC_TF(inv_logger, "decompress packet failed",
                            "packet <{1}>, code <{2}>, message <{3}>, detail <{4}>",
                            packet, e.code(), e.code().message(), e.what());
                bridge_->GetContext()->DeleteMessage(ami_msg);
                return;
            }
            SAVE_MSG("recvmsg.txt", ami_msg->size(), ami_msg->data());
            AMI_TD_SEND_EVENT_ONCE("decompress ami message");
        }
        else
        {
            ami_msg->append(packet + head_len, data_len);
        }
        bridge_->bridge_rx_bytes_ += total_len;
        bridge_->bridge_rx_cpayload_bytes_ += data_len;
        bridge_->bridge_rx_payload_bytes_ += orig_data_len;

#ifdef TEST_FRAMEWORK_DEBUG
        if (recv_sqn == bridge_->last_break_ + 1)
        {
            AMI_TD_SEND_EVENT("transmit success");
        }
#endif

        /// 5.发送到AMI
        ami_msg->set_persistent_context(bridge_->peer_session_id_, recv_sqn);
        if (exp_pno == 0)
        {
            ADK_INV_LOG_WARN_AC_TF(
                inv_logger, "SelectLoadBalancePartition failed",
                "LoadBalance partition <{1}> not exist", exp_pno);
            // avoid ami memory leaks
            bridge_->GetContext()->DeleteMessage(ami_msg);
            AMI_TD_SEND_EVENT_UNTIL("LoadBalance failed and DeleteMessage", 20000);
            return;
        }
        // 用于轨迹跟踪的ancestor_id
        ami_msg->msg_header.ancestor_id = ancestor_id;

        if (kSuccess != txep_stuff->ep_handler->SendMsg(ami_msg, exp_pno))
        {
            static adk::log::IntervalLogger inv_logger_new(1u);
            ADK_INV_LOG_ERROR_AC_TF(inv_logger_new, "SendMsg failed",
                "endpoint_id <{1}> partition_id <{2}>",
                ep_id, exp_pno);
        }

        bridge_->ami_tx_msg_cnt_ = recv_sqn;
        txep_stuff->msg_cnt++;
        txep_stuff->total_cpayload_bytes += rx_packet->data_len();
        txep_stuff->total_payload_bytes += rx_packet->orig_data_len();
        bridge_->GetContext()->DeleteMessage(ami_msg);

        break;
    }
    default:
        ADK_LOG_ERROR_AC_TF("receive unknow message", "");
        bridge_->StopAmiApp();
    }
}

}
}
