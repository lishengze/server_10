/**
*  Copyright (c) 2018 Archforce Financial Technology. All rights reserved.
*  Redistribution and use in source and binary forms, with or without modification, are not permitted.
*  For more information about Archforce, welcome to archforce.cn.
**/

#include "adk/timesync_client.h"
#include "adk/entry_wrapper.h"
#include "timesync_common.h"

#include <linux/net_tstamp.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <limits>

#include <adk/util.h>

namespace adk_impl
{

std::shared_ptr<TimeSyncClient> TimeSyncClient::GetTimeSyncUDPClient(std::string const& local_ip,
                                                                     uint16_t local_port,
                                                                     std::string const& server_ip,
                                                                     uint16_t server_port,
                                                                     uint32_t nr_history_windows,
                                                                     uint32_t nr_round_per_seconds,
                                                                     uint32_t nr_probe_msgs_per_window)
{
    int sockfd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
    if (sockfd < 0)
    {
        return nullptr;
    }

    return std::make_shared<TimeSyncClient>(sockfd, local_ip, local_port, server_ip, server_port, nr_history_windows, nr_round_per_seconds, nr_probe_msgs_per_window);
}

std::shared_ptr<TimeSyncClient> TimeSyncClient::GetTimeSyncRDMAClient(std::string const& local_ip,
                                                                      uint16_t local_port,
                                                                      std::string const& server_ip,
                                                                      uint16_t server_port,
                                                                      uint32_t nr_history_windows,
                                                                      uint32_t nr_round_per_seconds,
                                                                      uint32_t nr_probe_msgs_per_window)
{
    return std::make_shared<TimeSyncClient>(local_ip, local_port, server_ip, server_port, nr_history_windows, nr_round_per_seconds, nr_probe_msgs_per_window);
}

TimeSyncClient::TimeSyncClient(int sockfd,
                               std::string const& local_ip,
                               uint16_t local_port,
                               std::string const& server_ip,
                               uint16_t server_port,
                               uint32_t nr_history_windows,
                               uint32_t nr_round_per_seconds,
                               uint32_t nr_probe_msgs_per_window) : nr_history_windows_(nr_history_windows),
                                                                    nr_round_per_seconds_(nr_round_per_seconds),
                                                                    nr_probe_msgs_per_window_(nr_probe_msgs_per_window),
                                                                    shutdown_(false)
{
    channel_ = std::unique_ptr<Node>(new UDPClientNode(sockfd, local_ip, local_port, server_ip, server_port));
}

TimeSyncClient::TimeSyncClient(std::string const& local_ip,
                               uint16_t local_port,
                               std::string const& server_ip,
                               uint16_t server_port,
                               uint32_t nr_history_windows,
                               uint32_t nr_round_per_seconds,
                               uint32_t nr_probe_msgs_per_window) : nr_history_windows_(nr_history_windows),
                                                     nr_round_per_seconds_(nr_round_per_seconds),
                                                     nr_probe_msgs_per_window_(nr_probe_msgs_per_window),
                                                     shutdown_(false)
{
    channel_ = std::unique_ptr<Node>(new RDMAClientNode(local_ip, local_port, server_ip, server_port));
}

int64_t TimeSyncClient::GetDrift(const timespec& timepoint_ts)
{
    int64_t delta  = 0;
    int64_t ts_now = timepoint_ts.tv_sec * (1000 * 1000 * 1000) + timepoint_ts.tv_nsec;
    {
        const auto& delta_tmp = args_delta_[position_ & 1];
        int64_t delta_offset  = delta_tmp.delta_k_ * (ts_now - delta_tmp.ts_base_);  // 计算时钟偏移值
        delta                 = (delta_tmp.delta_base_ + delta_offset) / 1000;  // us级别精度
    }
    return delta;
}

bool TimeSyncClient::Start(const std::string& cpu_list)
{
    loop_ = std_thread("adk-timesync-c", "client polling thread", std::bind(&TimeSyncClient::Polling, this, cpu_list));
    return loop_.get_id() != std::thread::id();
}

void TimeSyncClient::Polling(const std::string& cpu_list)
{
    SetCpuAffinity(cpu_list);

    if (!channel_->Start())
    {
        shutdown_ = true;
    }

    SyncMessage msg;

    int64_t T1 = 0, T2 = 0, T3 = 0, Drift = 0;
    bool need_recvd = false;

    // nr_history_windows_区间[2, 10]
    if (nr_history_windows_ > 10)
    {
        nr_history_windows_ = 10;
    }
    if (nr_history_windows_ < 2)
    {
        nr_history_windows_ = 2;
    }

    bool clear_deque     = false;
    uint32_t clear_count = 0;
    
    // nr_round_per_seconds_区间[1, 10]
    if (nr_round_per_seconds_ < 1)
    {
        nr_round_per_seconds_ = 1;
    }
    if (nr_round_per_seconds_ > 10)
    {
        nr_round_per_seconds_ = 10;
    }
    uint64_t round_temp         = nr_round_per_seconds_;        // 1s 测试几场
    uint64_t per_round_duration = (1000 * 1000) / (round_temp); // 每场的时间间隔

    // nr_probe_msgs_per_window_区间[10, 50]
    if (nr_probe_msgs_per_window_ < 10)
    {
        nr_probe_msgs_per_window_ = 10;
    }
    if (nr_probe_msgs_per_window_ > 50)
    {
        nr_probe_msgs_per_window_ = 50;
    }

    timespec ts_begin, ts_end;
    clock_gettime(CLOCK_REALTIME, &ts_begin);
    ts_end = ts_begin;
    while (!shutdown_)
    {
        // 1s获取一次delta，更新一次时间偏移的计算参数
        if (time_diff(ts_end, ts_begin) > (1000 * 1000 * 1000)) // ts_end - ts_begine > 1s
        {
            const auto modify_pos = position_ + 1;
            auto& modify_delta    = args_delta_[modify_pos & 1];
            // 历史校时delta窗口不满时，直接取最近的delta
            size_t delta_deque_size = delta_deque_.size();
            if (delta_deque_size != nr_history_windows_ && delta_deque_size > 0)
            {
                modify_delta.delta_base_ = delta_deque_.back().delta_;
                ++position_; // position_只在该线程中发生改变，++position_后上述的delta修改才会生效
            }
            // 获取delta_k，delta_base_，ts_base
            else if (delta_deque_size == nr_history_windows_)
            {
                std::deque<Delta> delta_deque_temp = delta_deque_;
                // 将delta_deque_temp按min_ts_从小到大排序
                std::sort(delta_deque_temp.begin(), delta_deque_temp.end(), [](Delta l, Delta r) {
                                                                            return l.min_ts_ < r.min_ts_;
                                                                            }); 

                if (delta_deque_temp[1].min_ts_ != INT64_MAX)   // 当网络压力大情况下，可能出现校时msg全部接收不到的情况，这时就使用之前的delta_k_、delta_base_、ts_base_
                {
                    // 从delta_0，delta_1中获取T2、T3，斜率计算公式： delta_k_ = (T2_1 - T2_0) - (T3_1 - T3_0) / (T3_1 - T3_0)
                    int64_t T2_delta = std::abs(delta_deque_temp[0].delta_t2_ - delta_deque_temp[1].delta_t2_);
                    int64_t T3_delta = std::abs(delta_deque_temp[0].delta_t3_ - delta_deque_temp[1].delta_t3_);
                    double delta_k   = (double)(T2_delta - T3_delta) / T3_delta;
                    if (delta_k_deque_.size() < delta_k_deque_size_)
                    {
                        delta_k_deque_.push_back(delta_k);
                    }
                    else
                    {
                        delta_k_deque_.pop_front();
                        delta_k_deque_.push_back(delta_k);
                    }

                    // 最终的delta_k，由历史delta_k取平均得来
                    assert(delta_k_deque_.size() > 0);
                    double delta_k_temp = boost::accumulate(delta_k_deque_, (double)0);
                    delta_k_temp        = delta_k_temp / delta_k_deque_.size();

                    // 取delta计算所需参数，将(T3 - T1)最小的delta作为delta_base_，该delta的T3作为ts_base_， 最终delta = k(time_now - ts_base_) + delta_base_
                    {
                        modify_delta.delta_base_ = delta_deque_temp[0].delta_;
                        modify_delta.delta_k_    = delta_k_temp;
                        modify_delta.ts_base_    = delta_deque_temp[0].delta_t3_;
                        ++position_; // position_只在该线程中发生改变，++position_后上述的delta修改才会生效
                    }
                }
                else
                {
                    if (delta_deque_temp[0].min_ts_ == INT64_MAX)  // 当最小的两个Delta的min_ts_都为最大值的时候，认为服务端掉线
                    {
                        {
                            modify_delta.delta_base_ = 0;
                            modify_delta.delta_k_    = 0;
                            modify_delta.ts_base_    = 0;
                            ++position_; // position_只在该线程中发生改变，++position_后上述的delta修改才会生效
                        }
                        if (!channel_->CheckConnect())  // 重连校时连接，只在RDMA校时客户端生效，该函数中会轮询校时服务端是否上线
                        {
                            shutdown_ = true;
                        }
                    }
                }
            }
            ts_begin  = ts_end;
        }
        
        // 一场校时产生delta
        usleep(per_round_duration); // 时间间隔放在前面，计算每场的delta越靠近1s的最后，可以提高delta的精度。但是在时间突变的情况下无法快速排出历史校时数据
        Delta delta;
        for (uint64_t msg_count = 0; msg_count < nr_probe_msgs_per_window_; ++msg_count) // 发送指定数量的校时信息，获取T3 - T1最小的delta
        {
            msg.reset();
            struct timespec ts = {0, 0};
            if (need_recvd)
            {
                // 接收指定标签的校时消息，并计算delta
                uint32_t loop_counter = 8192;
                do {
                    if (channel_->RecvMsg(&msg, &ts, send_flag_))
                    {
                        T1             = msg.Get(0);
                        T2             = msg.Get(1);
                        T3             = ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
                        int64_t ts_tmp = T3 - T1;
                        assert(ts_tmp > 0);
                        if (ts_tmp < delta.min_ts_)
                        {
                            Drift           = T2 - (T1 + T3) / 2;  // server - client
                            delta.delta_    = Drift;
                            delta.delta_t2_ = T2;
                            delta.delta_t3_ = T3;
                            delta.min_ts_   = ts_tmp;
                        }
                        break;
                    }
                } while ((--loop_counter) > 0); // break from here, we believe the pong message was lost
                
                need_recvd = false;
                ++send_flag_;
                // one msg ping-pong
            }
            else
            {
                // 发送带标签的校时消息
                msg.Set(SyncMessage::kClientTimestamp, send_flag_);
                while ((!shutdown_) && (!channel_->SendMsg(msg)))
                {          
                    sleep(0);
                }
                need_recvd = true;
                continue;   // send the ping message and wait the pong message
            }
        }

        if (delta_deque_.size() < nr_history_windows_)
        {
            delta_deque_.push_back(delta);
        }
        else
        {
            delta_deque_.pop_front();
            delta_deque_.push_back(delta);
        }

        // 清除delta_deque_中保存的历史delta数据
        if (clear_deque)
        {
            ++clear_count;
            if (clear_count == nr_history_windows_)
            {
                clear_deque = false;
            }
        }

        double delta_k     = 0;
        int32_t delta_base = args_delta_[position_ & 1].delta_base_;
        if (delta_base != 0)
        {
            delta_k = (double)std::abs(delta.delta_ - delta_base) * 10 / delta_base;  // delta的变化率，"* 10"是为了放大变化率
        }
        
        // 通过delta的变化率来改变接下来的校时速率
        bool is_need_init_rate = false;
        if (delta_k > 1)
        {
            round_temp = nr_round_per_seconds_ * delta_k;
            if (round_temp > max_round_)
            {
                round_temp = max_round_;
            }
            clear_deque       = true;
            clear_count       = 0;
            is_need_init_rate = true;
        }
        else
        {
            if (!clear_deque)
            {
                round_temp        = nr_round_per_seconds_;
                is_need_init_rate = true;
            }
        }
        
        if (is_need_init_rate)
        {
            per_round_duration = (1000 * 1000) / (round_temp);
        }
        clock_gettime(CLOCK_REALTIME, &ts_end);
    }
}

void TimeSyncClient::Stop()
{
    shutdown_ = true;
    channel_->Stop();

    if (loop_.joinable())
    {
        loop_.join();
    }
}

TimeSyncClient::~TimeSyncClient()
{
    Stop();
    channel_->Exit();
}

}  // namespace adk_impl