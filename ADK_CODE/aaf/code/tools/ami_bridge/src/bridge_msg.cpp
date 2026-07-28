/**
 * @author 牛亮亮(niuliangliang@af.local)
 */

///< impl
#include "bridge_msg.h"

namespace ami
{
namespace bridge
{

/*********************************************************************
 * PeerSyncMsg
 */
uint32_t PeerSyncMsg::get_buff_size(EndpointInfoMapType& endpoints_info)
{
    uint32_t cnt = 0u;
    for (auto info : endpoints_info)
    {
        cnt += 2;
        cnt += info.second.size();
    }
    return cnt * sizeof(uint32_t);
}

void PeerSyncMsg::set_data(MsgType msg_type,
                           uint64_t session_id,
                           uint64_t recv_sqn,
                           uint64_t send_sqn,
                           std::string& endpoints_info)
{
    msg_type_        = msg_type;
    session_id_      = session_id;
    recv_packet_sqn_ = recv_sqn;
    send_packet_sqn_ = send_sqn;
    ep_info_size_    = 0;  // 0表示通过property传输，否则是老版本的格式
    ep_info_len_     = endpoints_info.length() + 1;
    memcpy(endpoint_info(), endpoints_info.c_str(), ep_info_len_);
}

MsgType PeerSyncMsg::msg_type()
{
    return msg_type_;
}
uint64_t PeerSyncMsg::session_id()
{
    return session_id_;
}
uint64_t PeerSyncMsg::recv_packet_sqn()
{
    return recv_packet_sqn_;
}
uint64_t PeerSyncMsg::send_packet_sqn()
{
    return send_packet_sqn_;
}
uint32_t PeerSyncMsg::ep_info_size()
{
    return ep_info_size_;
}
uint32_t PeerSyncMsg::total_len()
{
    return Size() + ep_info_len_;
}
char* PeerSyncMsg::endpoint_info()
{
    return &ep_info_[0];
}

/*********************************************************************
 * TcpAckMsg
 */
MsgType TcpAckMsg::msg_type()
{
    return msg_type_;
}
uint64_t TcpAckMsg::recv_packet_sqn()
{
    return recv_packet_sqn_;
}
uint32_t TcpAckMsg::total_len()
{
    return sizeof(TcpAckMsg);
}

}
}  //namespace ami::bridge
