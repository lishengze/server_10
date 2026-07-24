/**
*  Copyright (c) 2018 Archforce Financial Technology. All rights reserved.
*  Redistribution and use in source and binary forms, with or without modification, are not permitted.
*  For more information about Archforce, welcome to archforce.cn.
**/

#include "adk/timesync_server.h"
#include "adk/entry_wrapper.h"
#include "timesync_common.h"

#include <cstring>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <adk/util.h>

namespace adk_impl
{

std::shared_ptr<TimeSyncServer> TimeSyncServer::GetTimeSyncUDPServer(std::string const& server_ip,
                                                                     uint16_t server_port)
{
    int sockfd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sockfd < 0)
    {
        return nullptr;  // LOG_ERROR underlying system
    }
    return std::make_shared<TimeSyncServer>(sockfd, server_ip, server_port);
}

std::shared_ptr<TimeSyncServer> TimeSyncServer::GetTimeSyncRDMAServer(std::string const& server_ip,
                                                                      uint16_t server_port)
{
    return std::make_shared<TimeSyncServer>(server_ip, server_port);
}

TimeSyncServer::TimeSyncServer(int sockfd, std::string const& ip, int port) : shutdown_(false)
{
    channel_ = std::unique_ptr<Node>(new UDPServerNode(sockfd, ip, port));
}

TimeSyncServer::TimeSyncServer(std::string const& server_ip,
                               uint16_t server_port) : shutdown_(false)
{
    channel_ = std::unique_ptr<Node>(new RDMAServerNode(server_ip, server_port));
}

bool TimeSyncServer::Start(const std::string& cpu_list)
{
    if (!channel_->Start())
    {
        return false;
    }

    loop_ = std_thread("adk-timesync-s", "server polling thread", std::bind(&TimeSyncServer::Polling, this, cpu_list));
    return loop_.get_id() != std::thread::id();
}

void TimeSyncServer::Polling(const std::string& cpu_list)
{
    SetCpuAffinity(cpu_list);

    // lockup core, will trigger NMI WatchDog timetout
    // struct sched_param param;
    // param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    // sched_setscheduler(0, SCHED_FIFO, &param);

    SyncMessage msg;
    while (!shutdown_)
    {
        timespec t = {0, 0};
        if (channel_->RecvMsg(&msg, &t))
        {
            uint64_t timestamp = t.tv_sec * 1000 * 1000 * 1000 + t.tv_nsec;

            // set receive timestamp
            msg.Set(SyncMessage::kServerTimestamp, timestamp);

            // sendback
            while (!channel_->SendMsg(msg))
            {
                sleep(0);
            }
        }
    }
}

void TimeSyncServer::Stop()
{
    shutdown_ = true;
    channel_->Stop();

    if (loop_.joinable())
    {
        loop_.join();
    }
}

TimeSyncServer::~TimeSyncServer() 
{
    Stop();
    channel_->Exit();
}

}  // namespace adk_impl