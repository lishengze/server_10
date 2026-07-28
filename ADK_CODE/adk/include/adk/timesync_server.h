/**
*  Copyright (c) 2018 Archforce Financial Technology. All rights reserved.
*  Redistribution and use in source and binary forms, with or without modification, are not permitted.
*  For more information about Archforce, welcome to archforce.cn.
**/

#pragma once

#include <memory>
#include <string>
#include <thread>

namespace adk_impl
{

class Node;

// Use UDP or RDMA as underlying transmission mechanism
class TimeSyncServer
{
public:
    static std::shared_ptr<TimeSyncServer> GetTimeSyncUDPServer(std::string const& server_ip,
                                                                uint16_t server_port);  // UDP version

    static std::shared_ptr<TimeSyncServer> GetTimeSyncRDMAServer(std::string const& local_ip,
                                                                 uint16_t local_port);  // RDMA version
    TimeSyncServer(TimeSyncServer const&) = delete;
    TimeSyncServer& operator=(TimeSyncServer const&) = delete;

    bool Start(const std::string& cpu_list);
    void Stop();

    TimeSyncServer(int sockfd, std::string const& local_ip, int port);  // UDP Server
    TimeSyncServer(std::string const&, uint16_t);  // RDMA Server

    ~TimeSyncServer();

private:
    void Polling(const std::string& cpu_list);

    std::unique_ptr<Node> channel_;  // underlying channel
    std::thread loop_;  // send clock sync package in timing fasion
    volatile bool shutdown_;
};

}  // namespace adk_impl