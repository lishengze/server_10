/**
 * @author 牛亮亮(niuliangliang@af.local)
 */

#ifndef AMI_BRIDGE_MSG_PACKET_H_
#define AMI_BRIDGE_MSG_PACKET_H_

///< cpp std
#include <type_traits>
#include <ostream>
#include <vector>
#include <functional>
#include <system_error>
#include <memory>  //shared_ptr
#include <string>

///< boost
#include <boost/array.hpp>

///< adk, ami public
#include <ami/error_code.h>

///< ami impl
#include "../../../sharding/ami_message.h"

///< impl
#include "bridge_base.h"

namespace ami
{
namespace bridge
{

/******************************************************************************
 * 定义本端同步消息
 */
class LocalSyncMsg
{
public:
    LocalSyncMsg(uint64_t session_id)
        : msg_type_(kLocalSync),
          session_id_(session_id)
    {
    }

    MsgType msg_type()
    {
        return msg_type_;
    }

    uint64_t session_id()
    {
        return session_id_;
    }

    uint32_t total_len()
    {
        return sizeof(LocalSyncMsg);
    }

private:
    MsgType msg_type_;
    uint64_t session_id_;
};

/******************************************************************************
 * 定义对端同步消息
 */
class PeerSyncMsg
{
public:
    static size_t Size() { return offsetof(PeerSyncMsg, ep_info_); }
    static uint32_t get_buff_size(EndpointInfoMapType& endpoint_info);

public:
    void set_data(MsgType msg_type,
                  uint64_t session_id,
                  uint64_t recv_sqn,
                  uint64_t send_sqn,
                  std::string& endpoints_info);
    MsgType msg_type();
    uint64_t session_id();
    uint64_t recv_packet_sqn();
    uint64_t send_packet_sqn();
    uint32_t ep_info_size();
    uint32_t total_len();
    char* endpoint_info();

private:
    MsgType msg_type_         = kErrorMsg;
    uint64_t session_id_      = 0u;
    uint64_t recv_packet_sqn_ = 0u;
    uint64_t send_packet_sqn_ = 0u;
    uint32_t ep_info_size_    = 0u;
    uint32_t ep_info_len_     = 0u;
    char ep_info_[];
};

/******************************************************************************
 * 定义tcp消息接收ack消息
 */
class TcpAckMsg
{
public:
    TcpAckMsg(uint64_t recv_sqn)
        : msg_type_(kTcpAck),
          recv_packet_sqn_(recv_sqn)
    {
    }
    ~TcpAckMsg() = default;

public:
    MsgType msg_type();
    uint64_t recv_packet_sqn();
    uint32_t total_len();

protected:
    MsgType msg_type_;
    uint64_t recv_packet_sqn_;
};

/*********************************************************************
 * 定义tcp传输的ami_message消息包
 */
class AmiMsgPacket
{
public:
    AmiMsgPacket() {}
    AmiMsgPacket(const Message* app_msg, int32_t compress)
        : msg_type_(kAmiPacket),
          compress_(compress),
          endpoint_id_(app_msg->get_endpoint_id()),
          transport_id_(app_msg->get_transport_id()),
          partition_no_(app_msg->get_partition_no()),
          data_len_(app_msg->size()),
          orig_data_len_(app_msg->size()),
          total_sqn_(app_msg->get_total_order_seq_num()),
          ancestor_id_(app_msg->msg_header.ancestor_id),
          data_((char*)app_msg)
    {
    }
    AmiMsgPacket(const Message* app_msg, int32_t compress, const char* compressed_data, size_t compressed_len)
        : msg_type_(kAmiPacket),
          compress_(compress),
          endpoint_id_(app_msg->get_endpoint_id()),
          transport_id_(app_msg->get_transport_id()),
          partition_no_(app_msg->get_partition_no()),
          data_len_(compressed_len),
          orig_data_len_(app_msg->size()),
          total_sqn_(app_msg->get_total_order_seq_num()),
          ancestor_id_(app_msg->msg_header.ancestor_id),
          data_(compressed_data)
    {
    }

public:
    enum MsgType msg_type() const
    {
        return msg_type_;
    }

    int32_t compress() const
    {
        return compress_;
    }

    Message::IDType& endpoint_id()
    {
        return endpoint_id_;
    }

    Message::IDType& transport_id()
    {
        return transport_id_;
    }

    int32_t& partition_no()
    {
        return partition_no_;
    }

    void set_partition_no(int32_t partition_no)
    {
        partition_no_ = partition_no;
    }

    Message::SqnType& total_sqn()
    {
        return total_sqn_;
    }

    Message::SizeType data_len() const
    {
        return data_len_;
    }

    Message::SizeType orig_data_len() const
    {
        return orig_data_len_;
    }

    Message::SizeType total_len() const
    {
        return header_len() + data_len();
    }

    uint64_t ancestor_id() const
    {
        return ancestor_id_;
    }

    static constexpr uint32_t header_len()
    {
        static_assert(sizeof(Message::IDType) == 4u, "size error");
        static_assert(sizeof(Message::SizeType) == 4u, "size error");
        static_assert(sizeof(Message::SqnType) == 8u, "size error");

        return sizeof(MsgType) + sizeof(int32_t) + 2u * sizeof(Message::IDType)
            + 2u * sizeof(int32_t) + 2u * sizeof(Message::SizeType) + sizeof(Message::SqnType) + sizeof(uint64_t);
    }

    const char* data() const
    {
        return data_;
    }

private:
    MsgType msg_type_                = kAmiPacket;
    int32_t compress_                = 0;
    Message::IDType endpoint_id_     = 0;
    Message::IDType transport_id_    = 0;
    int32_t partition_no_            = 0;
    int32_t pad_                     = 0;
    Message::SizeType data_len_      = 0;
    Message::SizeType orig_data_len_ = 0;
    Message::SqnType total_sqn_      = 0;
    uint64_t ancestor_id_            = 0;
    const char* data_                = nullptr;

    friend std::ostream& operator<<(std::ostream&, const AmiMsgPacket&);
};
//*******************************************************************

inline std::ostream& operator<<(std::ostream& os, const AmiMsgPacket& packet)
{
    os << "endpoint_id=" << packet.endpoint_id_ << " "
       << "transport_id=" << packet.transport_id_ << " "
       << "parittion_no=" << packet.partition_no_ << " "
       << "topic_sqn=" << packet.total_sqn_ << " "
       << "data_len=" << packet.data_len_ << " "
       << "orig_data_len=" << packet.orig_data_len_
       ;
    return os;
}

}
}  //namespace ami::bridge

#endif  // AMI_BRIDGE_MSG_PACKET_H_
