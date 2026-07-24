/**
*  Copyright (c) 2018 Archforce Financial Technology. All rights reserved.
*  Redistribution and use in source and binary forms, with or without modification, are not permitted.
*  For more information about Archforce, welcome to archforce.cn.
**/

#pragma once

#include <cassert>
#include <cstdint>

#include <adk/rdma/rdma_exp.h>

namespace adk_impl
{

using RdmaContext    = rdma::Context;
using RdmaUcEndpoint = rdma::UcEndpoint;
using RdmaMcEndpoint = rdma::McEndpoint;
using RdmaDH         = rdma::DestHandler;

using rdma::TxNodeEntry;

class SyncMessage
{
public:
    SyncMessage() { reset(); }

    char* data() { return reinterpret_cast<char*>(data_); }
    const char* data() const { return reinterpret_cast<const char*>(data_); }

    std::size_t length() const { return sizeof(data_); }
    void reset() { data_[0] = data_[1] = 0; }

    void Set(int i, uint64_t d)
    {
        assert(i >= 0 && i < static_cast<int>(sizeof(data_) / sizeof(data_[0])));
        data_[i] = d;
    }

    uint64_t Get(int i)
    {
        assert(i >= 0 && i < static_cast<int>(sizeof(data_) / sizeof(data_[0])));
        return data_[i];
    }

    enum StorageLayout
    {
        kClientTimestamp,
        kServerTimestamp,
        kClientCurrentDiff,
        kMaxIndex
    };

private:
    // microseconds
    // data_[0]: sender timestamp (client fill)
    // data_[1]: receiver timestamp (server fill)
    // data_[2]: client's view of clock diff
    uint64_t data_[kMaxIndex];
    bool filled;
};

class Node
{
public:
    virtual ~Node() = default;

    virtual bool Start() = 0;
    virtual bool Stop()  = 0;
    virtual void Exit()  = 0;

    virtual bool SendMsg(SyncMessage const& msg)                                   = 0;
    virtual bool RecvMsg(SyncMessage* msg, struct timespec* ts, uint64_t flag = 0) = 0;
    virtual bool CheckConnect() = 0;
};

class UDPServerNode : public Node
{
public:
    UDPServerNode(int sockfd, std::string const& ip, uint16_t port) : sockfd_(sockfd),
                                                                      server_ip_(ip),
                                                                      server_port_(port),
                                                                      addr_len_(sizeof(peer_addr_))
    {
        memset(&peer_addr_, 0, sizeof(peer_addr_));
    }

    bool Start() override;
    bool Stop() override;
    void Exit() override;

    bool SendMsg(SyncMessage const& msg) override;
    bool RecvMsg(SyncMessage* msg, struct timespec* ts, uint64_t flag = 0) override;
    bool CheckConnect() override;

    ~UDPServerNode() = default;

private:
    int sockfd_;
    std::string server_ip_;
    uint16_t server_port_;

    // peer address, cached from last received package
    struct sockaddr peer_addr_;
    socklen_t addr_len_;
};

class UDPClientNode : public Node
{
public:
    UDPClientNode(int sockfd,
                  std::string const& local_ip,
                  uint16_t local_port,
                  std::string const& server_ip,
                  uint16_t server_port) : sockfd_(sockfd),
                                          local_ip_(local_ip),
                                          local_port_(local_port),
                                          server_ip_(server_ip),
                                          server_port_(server_port)
    {
        memset(&send_ts_, 0, sizeof(send_ts_));
    }

    bool Start() override;
    bool Stop() override;
    void Exit() override;

    bool SendMsg(SyncMessage const& msg) override;
    bool RecvMsg(SyncMessage* msg, struct timespec* ts, uint64_t flag = 0) override;
    bool CheckConnect() override;

    ~UDPClientNode() = default;

private:
    int sockfd_;

    std::string local_ip_;
    uint16_t local_port_;

    std::string server_ip_;
    uint16_t server_port_;
    struct timespec send_ts_;
};

class RDMAServerNode : public Node
{
public:
    RDMAServerNode(std::string ip, uint16_t port) : server_ip_(ip), server_port_(port) {}

    bool Start() override;
    bool Stop() override;
    void Exit() override;

    bool SendMsg(SyncMessage const& msg) override;
    bool RecvMsg(SyncMessage* msg, struct timespec* ts, uint64_t flag = 0) override;
    bool CheckConnect() override;

    ~RDMAServerNode() = default;

private:
    RdmaContext* rdma_context_     = nullptr;
    RdmaUcEndpoint* rdma_endpoint_ = nullptr;
    RdmaDH* peer_dh_               = nullptr;

    std::string server_ip_;
    uint16_t server_port_;

    RdmaDH* client_dh_ = nullptr;  // should cache the latest client
};

class RDMAClientNode : public Node
{
public:
    RDMAClientNode(std::string local_ip,
                   uint16_t local_port,
                   std::string server_ip,
                   uint16_t server_port) : local_ip_(local_ip),
                                           local_port_(local_port),
                                           server_ip_(server_ip),
                                           server_port_(server_port)
    {
        memset(&send_ts_, 0, sizeof(send_ts_));
    }

    bool Start() override;
    bool Stop() override;
    void Exit() override;

    bool SendMsg(SyncMessage const& msg) override;
    bool RecvMsg(SyncMessage* msg, struct timespec* ts, uint64_t flag = 0) override;
    bool CheckConnect() override;

    ~RDMAClientNode() = default;

private:
    RdmaContext* rdma_context_     = nullptr;
    RdmaUcEndpoint* rdma_endpoint_ = nullptr;
    RdmaDH* peer_dh_               = nullptr;

    std::string local_ip_;
    uint16_t local_port_;

    std::string server_ip_;
    uint16_t server_port_;

    timespec send_ts_;

    RdmaDH* server_dh_ = nullptr;  // should cache the latest client

    bool is_running_ = false;
    
    // uint32_t wait_time_ = 15;  // 客户端等待服务端连接的超时时间，单位为s，默认为15s
};

}  // namespace adk_impl