/**
*  Copyright (c) 2018 Archforce Financial Technology. All rights reserved.
*  Redistribution and use in source and binary forms, with or without modification, are not permitted.
*  For more information about Archforce, welcome to archforce.cn.
**/

#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <thread>
#include <vector>
#include <mutex>

#include <boost/thread/mutex.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/range/numeric.hpp>
namespace adk_impl
{

class Node;

// Use UDP as underlying transmission mechanism
class TimeSyncClient
{
public:
    struct Delta
    {
        int64_t delta_    = 0;
        int64_t delta_t2_ = 0;
        int64_t delta_t3_ = 0;
        int64_t min_ts_   = INT64_MAX;
    };

    struct ArgsDelta
    {
        int64_t delta_base_ = 0;
        double delta_k_     = 0.0;
        int64_t ts_base_    = 0;
    };

    static std::shared_ptr<TimeSyncClient> GetTimeSyncUDPClient(std::string const& local_ip,
                                                                uint16_t local_port,
                                                                std::string const& server_ip,
                                                                uint16_t server_port,
                                                                uint32_t nr_history_windows = 5,
                                                                uint32_t nr_round_per_seconds = 5,
                                                                uint32_t nr_probe_msgs_per_window = 20);

    static std::shared_ptr<TimeSyncClient> GetTimeSyncRDMAClient(std::string const& local_ip,
                                                                 uint16_t local_port,
                                                                 std::string const& server_ip,
                                                                 uint16_t server_port,
                                                                 uint32_t nr_history_windows = 5,
                                                                 uint32_t nr_round_per_seconds = 5,
                                                                 uint32_t nr_probe_msgs_per_window = 20);

    TimeSyncClient(TimeSyncClient const&) = delete;
    TimeSyncClient& operator=(TimeSyncClient const&) = delete;

    bool Start(const std::string& cpu_list);
    void Stop();

    TimeSyncClient(int sockfd,
                   std::string const& local_ip,
                   uint16_t local_port,
                   std::string const& server_ip,
                   uint16_t server_port,
                   uint32_t nr_history_windows = 5,
                   uint32_t nr_round_per_seconds = 5,
                   uint32_t nr_probe_msgs_per_window = 20);

    TimeSyncClient(std::string const& local_ip,
                   uint16_t local_port,
                   std::string const& server_ip,
                   uint16_t server_port,
                   uint32_t nr_history_windows = 5,
                   uint32_t nr_round_per_seconds = 5,
                   uint32_t nr_probe_msgs_per_window = 20);

    ~TimeSyncClient();
    int64_t GetDrift(const timespec& timepoint_ts);

private:
    void Polling(const std::string& cpu_list);

    uint32_t nr_history_windows_       = 5;   // 窗口大小
    uint32_t nr_round_per_seconds_     = 5;   // 1s产生多少场校时
    uint32_t nr_probe_msgs_per_window_ = 20;  // 一场校时发送多少条校时信息

    std::deque<Delta> delta_deque_;  // 存储历史校时场次产生delta相关数据

    uint64_t max_round_  = 100;  // 1s内最多产生100场校时

    uint64_t send_flag_ = 0;    // 校时消息的标签

    std::unique_ptr<Node> channel_;
    std::thread loop_;

    uint64_t position_ = 0;
    ArgsDelta args_delta_[2];
    volatile bool shutdown_;

    std::deque<double> delta_k_deque_;  // 存储历史校时产生delta_k，最终的delta_k由这些delta_k求平均得到
    uint32_t delta_k_deque_size_ = 10;  // delta_k队列的长度，存储历史delta_k的值，最终delta_k由这些元素求平均得到，后续可将这个参数变成可配置项
};

}  // namespace adk_impl