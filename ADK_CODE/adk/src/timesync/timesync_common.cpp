/**
*  Copyright (c) 2018 Archforce Financial Technology. All rights reserved.
*  Redistribution and use in source and binary forms, with or without modification, are not permitted.
*  For more information about Archforce, welcome to archforce.cn.
**/

#include "timesync_common.h"

#include <arpa/inet.h>
#include <cassert>
#include <linux/net_tstamp.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/ioctl.h>

namespace adk_impl
{

bool UDPServerNode::Start()
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));

    // bind to INADDY_ANY, accept all incoming UDP packages from valid NIC
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  // TODO
    addr.sin_port        = htons(server_port_);

    if (::bind(sockfd_, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        return false;  // LOG_ERROR underlying system error
    }
    return true;
}

bool UDPServerNode::Stop()
{
    ::close(sockfd_);
    return true;
}

void UDPServerNode::Exit() {}

bool UDPServerNode::SendMsg(SyncMessage const& msg)
{
    int nw = ::sendto(sockfd_, msg.data(), msg.length(), 0, &peer_addr_, addr_len_);
    return nw == static_cast<int>(msg.length());
}

bool UDPServerNode::RecvMsg(SyncMessage* msg, struct timespec* ts, uint64_t flag)
{
    addr_len_ = sizeof(struct sockaddr);  // avoid first ip address be zero
    int nr    = ::recvfrom(sockfd_, msg->data(), msg->length(), 0, &peer_addr_, &addr_len_);

    if (nr == static_cast<int>(msg->length()) && addr_len_ != 0)
    {
        if (nr == static_cast<int>(msg->length()))
        {
            // get user-level timestamp to supply
            ::clock_gettime(CLOCK_REALTIME, ts);  // ::ioctl(sockfd_, SIOCGSTAMP, ts);
        }
    }
    return nr == static_cast<int>(msg->length());
}
bool UDPServerNode::CheckConnect() {return true;}

bool UDPClientNode::Start()
{
    // software timestamp flag
    int flags = SOF_TIMESTAMPING_TX_SOFTWARE | SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE;

    int err = setsockopt(sockfd_, SOL_SOCKET, SO_TIMESTAMPING, &flags, sizeof(flags));
    if (err < 0)
    {
        return false;
    }

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(struct sockaddr_in));

    if (!local_ip_.empty() && local_port_ != 0)
    {
        // bind to local_ip
        local_addr.sin_family = AF_INET;
        if (::inet_pton(AF_INET, local_ip_.c_str(), &local_addr.sin_addr) < 0)
        {
            return false;  // LOG_ERROR
        }
        local_addr.sin_port = htons(local_port_);

        if (bind(sockfd_, reinterpret_cast<const struct sockaddr*>(&local_addr), sizeof(local_addr)) < 0)
        {
            return false;  // LOG_ERROR underlying system
        }
    }

    struct sockaddr_in serveraddr;
    serveraddr.sin_family = AF_INET;
    if (inet_pton(AF_INET, server_ip_.c_str(), &serveraddr.sin_addr) < 0)
    {
        return false;  // LOG_ERROR
    }
    serveraddr.sin_port = htons(server_port_);

    // set default address
    if (connect(sockfd_, reinterpret_cast<const struct sockaddr*>(&serveraddr), sizeof(serveraddr)) < 0)
    {
        return false;  // LOG_ERROR
    }
    return true;
}

bool UDPClientNode::Stop()
{
    ::close(sockfd_);
    return true;
}

void UDPClientNode::Exit() {}

bool UDPClientNode::SendMsg(SyncMessage const& msg)
{
    ssize_t nw = ::write(sockfd_, msg.data(), msg.length());
    ::clock_gettime(CLOCK_REALTIME, &send_ts_);

    return nw == static_cast<int>(msg.length());
}

bool UDPClientNode::RecvMsg(SyncMessage* msg, struct timespec* ts, uint64_t flag)
{
    struct msghdr msgheader;
    memset(&msgheader, 0, sizeof(msgheader));

    struct iovec entry;
    entry.iov_base       = msg->data();
    entry.iov_len        = msg->length();
    msgheader.msg_iov    = &entry;
    msgheader.msg_iovlen = 1;

    char data[256];
    msgheader.msg_control    = data;
    msgheader.msg_controllen = sizeof(data);

    // poll ?
    ssize_t nr  = recvmsg(sockfd_, &msgheader, 0);

    if (msg->Get(SyncMessage::kClientTimestamp) != flag)
    {
        return false;
    }

    if (nr == static_cast<int>(msg->length()))
    {
        msg->Set(0, send_ts_.tv_sec * 1000 * 1000 * 1000 + send_ts_.tv_nsec);
        ::clock_gettime(CLOCK_REALTIME, ts);
        return true;
    }
    return false;
}
bool UDPClientNode::CheckConnect() {return true;}

bool RDMAServerNode::Start()
{
    rdma_context_ = RdmaContext::NewContext(server_ip_);
    if (nullptr == rdma_context_)
    {
        return false;
    }

    rdma_endpoint_ = rdma_context_->CreateUcEndpoint(server_port_);
    if (nullptr == rdma_endpoint_)
    {
        return false;
    }

    return true;
}

bool RDMAServerNode::Stop()
{
    return true;
}

void RDMAServerNode::Exit()
{
    // endpoint 释放前会将其下的 DestHandler 资源释放
    if (nullptr != rdma_endpoint_)
    {
        assert(rdma_context_);
        rdma_context_->DestroyEndpoint(rdma_endpoint_);
        rdma_endpoint_ = nullptr;
    }

    if (nullptr != rdma_context_)
    {
        RdmaContext::DeleteContext(rdma_context_);
        rdma_context_ = nullptr;
    }
}

inline bool sendMsgHelper(RdmaUcEndpoint* rdma_endpoint, RdmaDH* dest, SyncMessage const& msg, struct timespec* ts = nullptr)
{
    struct TxNodeEntry* node_entry = rdma_endpoint->NewTxMessage();
    if (ADK_UNLIKELY(nullptr == node_entry))
    {
        rdma_endpoint->RecycleTxEntries();
        return false;
    }
    node_entry->set_buffer_size(msg.length());
    memcpy(node_entry->buffer(), msg.data(), msg.length());

    // client_dh_ cached the peer endpoint
    int ret = rdma_endpoint->SendMsg(node_entry, dest);
    if (ts != nullptr)
    {
        ::clock_gettime(CLOCK_REALTIME, ts);
    }

    return ret == ErrorCode::kSuccess;
}

// for server
inline bool recvMsgHelper(RdmaUcEndpoint* rdma_endpoint, SyncMessage* msg, struct timespec* ts, RdmaDH** client_dh = nullptr)
{
    static int fail_cnt = 0;
    bool filled         = false;

    int ret = 0;
    if (client_dh != nullptr)
    {
        // for server
        ret = rdma_endpoint->RecvMMsgDh([&](char* msg_buf, uint32_t msg_size, RdmaDH* dh) {
            ::clock_gettime(CLOCK_REALTIME, ts);

            assert(msg_size == msg->length());
            memcpy(msg->data(), msg_buf, msg_size);
            filled = true;

            *client_dh = dh;  // only difference with the following
        });
    }
    else
    {
        // for client
        ret = rdma_endpoint->RecvMMsg([&](char* msg_buf, uint32_t msg_size) {
            ::clock_gettime(CLOCK_REALTIME, ts);

            assert(msg_size == msg->length());
            memcpy(msg->data(), msg_buf, msg_size);
            filled = true;
        });
    }

    fail_cnt += ret;
    if (fail_cnt > 10)
    {
        rdma_endpoint->RecycleTxEntries();
        fail_cnt = 0;
    }
    return ret == ErrorCode::kSuccess && filled;
}

bool RDMAServerNode::SendMsg(SyncMessage const& msg)
{
    return sendMsgHelper(rdma_endpoint_, client_dh_, msg, nullptr);
}

bool RDMAServerNode::RecvMsg(SyncMessage* msg, struct timespec* ts, uint64_t flag)
{
    return recvMsgHelper(rdma_endpoint_, msg, ts, &client_dh_);
}
bool RDMAServerNode::CheckConnect() {return true;}

bool RDMAClientNode::Start()
{
    is_running_ = true;
    rdma_context_ = RdmaContext::NewContext(local_ip_);
    if (nullptr == rdma_context_)
    {
        return false;
    }

    rdma_endpoint_ = rdma_context_->CreateUcEndpoint(local_port_);
    if (nullptr == rdma_endpoint_)
    {
        return false;
    }

    return CheckConnect();
}

bool RDMAClientNode::Stop()
{
    is_running_ = false;
    return true;
}

void RDMAClientNode::Exit()
{
    // endpoint 释放前会将其下的 DestHandler 资源释放
    if (nullptr != rdma_endpoint_)
    {
        assert(rdma_context_);
        rdma_context_->DestroyEndpoint(rdma_endpoint_);
        rdma_endpoint_ = nullptr;
    }

    if (nullptr != rdma_context_)
    {
        RdmaContext::DeleteContext(rdma_context_);
        rdma_context_ = nullptr;
    }
}

bool RDMAClientNode::SendMsg(SyncMessage const& msg)
{
    return sendMsgHelper(rdma_endpoint_, server_dh_, msg, &send_ts_);
}

bool RDMAClientNode::CheckConnect()
{
    server_dh_ = rdma_endpoint_->CreateDestHandler(server_ip_, server_port_);
    if (server_dh_ == nullptr)
    {
        return false;
    }
    while (is_running_ && (RdmaDH::Status::kIniting == server_dh_->status))
    {
        usleep(1000);
    }
    if (!is_running_)
    {
        return false;
    }
    return server_dh_->is_ready();
}

bool RDMAClientNode::RecvMsg(SyncMessage* msg, struct timespec* ts, uint64_t flag)
{
    bool ret = recvMsgHelper(rdma_endpoint_, msg, ts, nullptr);
    if (msg->Get(SyncMessage::kClientTimestamp) != flag)
    {
        return false;
    }
    msg->Set(SyncMessage::kClientTimestamp, send_ts_.tv_sec * 1000 * 1000 * 1000 + send_ts_.tv_nsec);  // ns级别的校时
    return ret;
}

}  // namespace adk_impl