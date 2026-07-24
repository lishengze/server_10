#ifndef ADK_IMPL_RDMA_RAW_PACKET_H_
#define ADK_IMPL_RDMA_RAW_PACKET_H_

#include "rdma_exp.h"

namespace adk_impl
{

namespace rdma
{

class RawPktEndpoint;

struct EthHeader
{
    /** Destination MAC */
    uint8_t dst_mac[6];
    /** Source MAC */
    uint8_t src_mac[6];
    /** Ethernet protocol */
    uint16_t protocol;
};

struct Ipv4Header
{
    /** IP header length, has the value of 5 for IPv4 */
    /** IP version number, has the value of 4 for IPv4 */
    uint8_t header_ver_len;
    uint8_t service_type;
    uint16_t total_len;
    uint16_t ip_identification;
    uint16_t fragment_offset;
    uint8_t time_to_live;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
};

struct UdpHeader
{
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint16_t checksum;
};

constexpr int32_t kEthHeaderSize = 14;
constexpr int32_t kIpv4HeaderSize = 20;
constexpr int32_t kUdpHeaderSize = 8;
constexpr int32_t kMessageHeaderSize = kEthHeaderSize + kIpv4HeaderSize + kUdpHeaderSize;

constexpr uint8_t kHeaderVerLen = 0x45;
constexpr uint8_t kServiceType = 0;
constexpr uint16_t kFragmentOffset = 0x0040;
constexpr uint8_t kTimeToLive = 64;
constexpr uint8_t kProtocolUdp = 17;

struct TxRawEntry : TxNodeEntry
{
    TxRawEntry(uint32_t mr_lkey) : TxNodeEntry(mr_lkey)
    {
    }

    uint32_t app_buffer_size() const
    {
        return sge.length - kMessageHeaderSize;
    }

    void set_app_buffer_size(uint32_t size)
    {
        sge.length = size + kMessageHeaderSize;
    }

    const char* app_const_buffer() const
    {
        return reinterpret_cast<char*>(sge.addr) + kMessageHeaderSize;
    }

    char* app_buffer() const
    {
        return reinterpret_cast<char*>(sge.addr) + kMessageHeaderSize;
    }

    struct ibv_sge* sg_list()
    {
        return &sge;
    }
};

class RawDestHandler
{
public:
    typedef struct
    {
        EthHeader  eth_header;
        Ipv4Header ipv4_header;
        UdpHeader  udp_header;
    } __attribute__((packed)) RawPktHeader;

    void set_pkt_header(uint8_t* pkt, uint16_t len)
    {
        memcpy(&((RawPktHeader*)pkt)->eth_header, &(message_header_.eth_header), kEthHeaderSize);

        uint32_t ip_checksum = 0;
        uint32_t udp_checksum = 0;

        ((RawPktHeader*)pkt)->ipv4_header.header_ver_len = kHeaderVerLen;
        ((RawPktHeader*)pkt)->ipv4_header.service_type = kServiceType;
        ip_checksum += (kHeaderVerLen + (kServiceType << 8));

        const auto ipv4_total_len = htons(len - kEthHeaderSize);
        ((RawPktHeader*)pkt)->ipv4_header.total_len = ipv4_total_len;
        ip_checksum += ipv4_total_len;

        const auto ip_id_generic = ip_id_generic_++;
        ((RawPktHeader*)pkt)->ipv4_header.ip_identification = ip_id_generic;
        ip_checksum += ip_id_generic;

        ((RawPktHeader*)pkt)->ipv4_header.fragment_offset = kFragmentOffset;
        ip_checksum += kFragmentOffset;

        ((RawPktHeader*)pkt)->ipv4_header.time_to_live = kTimeToLive;
        ((RawPktHeader*)pkt)->ipv4_header.protocol = kProtocolUdp;
        ip_checksum += (kTimeToLive + (kProtocolUdp << 8));
        udp_checksum += (kProtocolUdp << 8);

        ((RawPktHeader*)pkt)->ipv4_header.src_ip = message_header_.ipv4_header.src_ip;
        const auto src_ip_crc = (message_header_.ipv4_header.src_ip >> 16) 
                              + (uint16_t)(message_header_.ipv4_header.src_ip);
        ip_checksum += src_ip_crc;
        udp_checksum += src_ip_crc;

        ((RawPktHeader*)pkt)->ipv4_header.dst_ip = message_header_.ipv4_header.dst_ip;
        const auto dst_ip_crc = (message_header_.ipv4_header.dst_ip >> 16)
                              + (uint16_t)(message_header_.ipv4_header.dst_ip);
        ip_checksum += dst_ip_crc;
        udp_checksum += dst_ip_crc;

        ip_checksum = (ip_checksum >> 16) + (ip_checksum & 0xFFFF);
        ((RawPktHeader*)pkt)->ipv4_header.checksum = (uint16_t)(~ip_checksum);

        const auto udp_total_len = htons(len - kEthHeaderSize - kIpv4HeaderSize);
        udp_checksum += udp_total_len;

        ((RawPktHeader*)pkt)->udp_header.src_port = message_header_.udp_header.src_port;
        udp_checksum += message_header_.udp_header.src_port;

        ((RawPktHeader*)pkt)->udp_header.dst_port = message_header_.udp_header.dst_port;
        udp_checksum += message_header_.udp_header.dst_port;

        ((RawPktHeader*)pkt)->udp_header.len = udp_total_len;
        udp_checksum += udp_total_len;

        ((RawPktHeader*)pkt)->udp_header.checksum = pkt_checksum(udp_checksum, 
                                                                 pkt + kMessageHeaderSize, 
                                                                 len - kMessageHeaderSize);
    }

private:

    uint16_t pkt_checksum(uint32_t checksum, uint8_t* pkt, uint32_t len)
    {
        uint16_t* buf = (uint16_t*)pkt;
        while (len > 1)
        {
            checksum += *(buf++);
            checksum = (checksum >> 16) + (checksum & 0xFFFF);
            len -= 2;
        }

        if (len > 0)
        {
            checksum += *((uint8_t*)buf);
            checksum = (checksum >> 16) + (checksum & 0xFFFF);
        }

        return (uint16_t)(~checksum);
    }

    uint16_t ip_id_generic_;
    RawPktHeader message_header_;
    friend class RawPktEndpoint;
};

class RawPktEndpoint
{
public:
    enum RawPktFlow
    {
        kTxOnly = 0,
        kSniffer,
        kDstToThis
    };

    /**
     * @brief    创建Rdma raw packet endpoint
     *
     * @param    host_ip     指定主机地址，用于确定网卡
     * @param    host_port   指定本地端口，用于做UDP发送时SRC PORT(TX)
     * @param    eth_to_this 用于消息抓取时是否只抓取发往本网卡的数据包(RX)
     *
     * @return   成功返回对象指针 / 失败返回 nullptr
     */
    static RawPktEndpoint* Create(const std::string& host_ip, uint16_t host_port = 0, RawPktFlow flow = kDstToThis);

    /**
     * @brief    创建地址句柄，用于进行消息发送
     *
     * @param    dest_ip   目标IP
     * @param    dest_port 目标端口
     *
     * @return   成功返回对应的地址句柄 / 失败返回 nullptr
     *           失败主要原因可能是由于ARP表中不存在dest_ip的相关信息
     */
    RawDestHandler* CreateDestHandler(const std::string& dest_ip, uint16_t dest_port);

    /**
     * @brief    获取硬件信息
     */
    std::string GetHWInfo();

    /**
     * @brief   创建待发送消息
     */
    inline struct TxRawEntry* NewTxMessage()
    {
        assert(tx_message_pool_);
        variant::VariantEntry* entry_ptr;
        if (ErrorCode::kSuccess == tx_message_pool_->TryWaitEntry(&entry_ptr))
        {
        entry_assign:
            char* const buffer = entry_ptr->buffer;
            TxNodeEntry* node_entry = *(TxNodeEntry**)buffer;
            node_entry->queue_sqn = entry_ptr->pos;
            node_entry->set_buffer_size(tx_entry_size_);

            tx_message_pool_->FreeEntry(entry_ptr);
            return (TxRawEntry*)node_entry;
        }
        else
        {
            if ((ErrorCode::kSuccess == RecycleTxEntries()) 
                && (ErrorCode::kSuccess == tx_message_pool_->TryWaitEntry(&entry_ptr)))
            {
                goto entry_assign;
            }
        }

        return nullptr;
    }

    inline int32_t RecycleTxEntries()
    {
        const auto poll_res = ibv_poll_cq(send_cq_, 
                                          constant::kMaxTxRecycleSize, 
                                          send_wcs_);
        if (poll_res > 0)
        {
            struct ibv_wc& wc = send_wcs_[poll_res - 1];
            assert(IBV_WC_SUCCESS == wc.status);

            struct TxNodeEntry* node_entry = reinterpret_cast<struct TxNodeEntry*>(wc.wr_id);
            assert(node_entry);

            tx_message_pool_->UnsafeRecoveryBack(node_entry->queue_sqn);
            return ErrorCode::kSuccess;
        }

        return ErrorCode::kFailure;
    }

    inline int32_t SendMsg(struct TxRawEntry* node_entry, RawDestHandler* dest_handler)
    {
        const auto inline_bits = ((uint32_t)(node_entry->buffer_size() < max_inline_data_))
                                    << constant::kIbvSendInlineBits;
        const auto tx_batch_bits = ((uint32_t)(!(++tx_msg_counter_ & constant::kTxSignalBatchSizeMask)))
                                    << constant::kIbvSendSignaledBits;

        send_wr_.send_flags = inline_bits | tx_batch_bits;
        send_wr_.sg_list = node_entry->sg_list();
        send_wr_.wr_id = reinterpret_cast<uint64_t>(node_entry);
   
        memcpy(node_entry->buffer(), &(dest_handler->message_header_), kMessageHeaderSize);
        dest_handler->set_pkt_header((uint8_t*)(node_entry->buffer()), 
                                  (uint16_t)(node_entry->buffer_size()));

        struct ibv_send_wr *bad_wr;
        if (ADK_UNLIKELY(0 != ibv_post_send(cma_id_->qp, &send_wr_, &bad_wr)))
        {
            return ErrorCode::kFailure;
        }

        return ErrorCode::kSuccess;
    }

    template<typename OnMsgFunc>
    inline int32_t RecvMsg(const OnMsgFunc& on_msg_func)
    {
        int32_t index;
        const auto msg_size = ibv_poll_cq(recv_cq_,
                                          constant::kMaxRxBatchSize,
                                          recv_wcs_);

        try
        {
            for (index = 0; index < msg_size; ++index)
            {
                struct ibv_wc& wc = recv_wcs_[index];
                if (ADK_UNLIKELY(IBV_WC_SUCCESS != wc.status))
                {
                    return ErrorCode::kFailure;
                }

                RxNodeEntry* const node_entry = RxNodeEntry::GetNodeEntry(wc.wr_id);
                assert(node_entry);

                on_msg_func(node_entry->buffer(), wc.byte_len);

                node_entry->set_buffer_size(rx_entry_size_);
                ADK_UNUSED const auto post_ec = PostRxEntry(node_entry);
                assert(ErrorCode::kSuccess == post_ec);
            }
        }
        catch (...)
        {
            for (; index < msg_size; ++index)
            {
                struct ibv_wc& wc = recv_wcs_[index];
                RxNodeEntry* const node_entry = RxNodeEntry::GetNodeEntry(wc.wr_id);
                assert(node_entry);

                node_entry->set_buffer_size(rx_entry_size_);
                ADK_UNUSED const auto post_ec = PostRxEntry(node_entry);
                assert(ErrorCode::kSuccess == post_ec);
            }

            return ErrorCode::kFailure;
        }

        return ErrorCode::kSuccess;
    }

    const char* last_error() const;

private:
    RawPktEndpoint();
    ~RawPktEndpoint();

    using TxMessagePool = variant::SPSCQueue<TxRawEntry*>;

    int32_t Init(RawPktFlow flow);

    void Exit();

    inline int32_t PostRxEntry(RxNodeEntry* node_entry)
    {
        struct ibv_recv_wr* bad_wr;
        if (ADK_UNLIKELY(0 != ibv_post_recv(cma_id_->qp, &(node_entry->recv_wr), &bad_wr)))
        {
            return ErrorCode::kFailure;
        }

        return ErrorCode::kSuccess;
    }

    int32_t            sk_tool_;
    uint8_t            nic_mac_[6];
    std::string        host_ip_;
    uint16_t           host_port_;
    std::string        nic_name_;

    uint32_t           active_mtu_;
    uint32_t           max_inline_data_;

    struct rdma_cm_id* cma_id_;
    struct ibv_pd*     pd_;

    TxMessagePool*     tx_message_pool_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint32_t           tx_entry_size_;
    uint32_t           tx_entry_node_size_;
    uint64_t           tx_msg_counter_;

    struct ibv_send_wr send_wr_;
    struct ibv_cq*     send_cq_;
    struct ibv_mr*     send_mr_;
    void*              send_mem_;
    struct ibv_wc      send_wcs_[constant::kMaxTxRecycleSize];

    ADK_EMPTY_CACHE_LINE;

    uint32_t           rx_entry_size_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint32_t           rx_entry_node_size_;

    struct ibv_flow*   eth_flow_;
    struct ibv_cq*     recv_cq_;
    struct ibv_mr*     recv_mr_;
    void*              recv_mem_;
    struct ibv_wc      recv_wcs_[constant::kMaxRxBatchSize];

    ADK_EMPTY_CACHE_LINE;
};

}

}

#endif
