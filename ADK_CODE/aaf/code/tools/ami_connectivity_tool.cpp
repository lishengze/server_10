// Copyright (c) 2018 Archforce Financial Technology. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are not permitted.
// For more information about Archforce, welcome to archforce.cn.

#include <aaf.h>
#include <adk/property.h>
#include <adk/simple_rate_controller.h>
#include <adk/util.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <signal.h>
#include <string>
#include <thread>
#include <time.h>
#include <vector>

#include <boost/algorithm/string.hpp>

using namespace aaf;

class AmiReceiver : public GenericAmiApplication
{
    ADK_LOG_DECLARE_AC(300000);

public:
    class EndpointInfo
    {
    public:
        EndpointInfo() = default;
        EndpointInfo(EndpointHandler *ep_hdl, std::vector<int32_t> &partitions, std::vector<int32_t> &lbgroups, bool isloadbalance, std::string content)
            : ep_hdl_(ep_hdl),
              partitions_(partitions),
              lb_groups_(lbgroups),
              content(content),
              is_loadbalance_(isloadbalance),
              nr_send_msgs_(0)
        {
        }

        EndpointInfo(const EndpointInfo &other)
        {
            ep_hdl_ = other.ep_hdl_;
            nr_send_msgs_ = other.nr_send_msgs_;
            partitions_ = other.partitions_;
            lb_groups_ = other.lb_groups_;
            is_loadbalance_ = other.is_loadbalance_;
            content = other.content;
        }

        EndpointHandler *ep_hdl_;
        std::vector<int32_t> partitions_;           // 该主题的 patitions
        std::vector<int32_t> lb_groups_;            // 该主题负载均衡组
        std::string content;                        // 该主题要发送的 message. 由发送端 transport 组成
        bool is_loadbalance_;                       // 当前主题是否设置了负载均衡
        uint64_t nr_send_msgs_;
    };

    AmiReceiver()
        : rate_("1/1")
    {
    }

    ~AmiReceiver()
    {
    }

    virtual void SetAmiAppOption()
    {
        AddOptionWithAcceptor("nmsgs-before-suspend",
                              "the number of messages to send before suspend",
                              uint64_t(0),
                              nmsg_before_suspend_);
    }

    virtual void OnConfigureFramework(ami::Property &fw_props)
    {
        fw_props.SetValue(config::kEnableHighAvailableContext, true);
        fw_props.SetValue(config::kEnableSingletonContext, false);
        fw_props.SetValue(config::kEnableAppNameCheck, false);
    }

    static void SignalHandler(int sig, siginfo_t *info, void *)
    {
        if (sig == SIGCONT)
        {
            AmiReceiver::start_ = true;
        }
    }

    int RegisterSignal()
    {
        struct sigaction sa;
        sa.sa_sigaction = &SignalHandler;

        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART | SA_SIGINFO;

        if (sigaction(SIGCONT, &sa, nullptr) < 0)
        {
            return -1;
        }

        return 0;
    }

    // create TxEndpoints
    virtual int32_t OnTxEndpointCreation(EndpointHandler *ep_hdl, const std::string &ep_name)
    {
        ami_context_ = GetContext();
        // 判断当前主题是否设置了负载均衡
        bool isLoadBalance = false;
        // clang-format off
        ami_context_->PropertyAt(ami::config::context::kTxEndpoint, ep_name)
                                (ami::config::endpoint::kLoadBalance)
                                .GetValue(isLoadBalance);
        // clang-format on
        std::vector<int32_t> lb_groups;
        if (isLoadBalance)
        {
            // 读取每个主题配置的负载均衡组数量，将其更新到 EndpointInfo 中
            if (GetTxEndpointLBGs(ep_name, lb_groups) != ErrorCode::kSuccess)
            {
                return ErrorCode::kFailure;
            }
        }

        // 构造发送消息：将发送端的 TxTransport 写入到消息体中
        /*
         * 因为跨 Bridge 的消息，从接收端解析出来的 TxTransport 不是真实的发送端 Transport，而是 Bridge 作为发送端的 Transport.
         *
         * 比如有如下场景：
         *                A(Sender) ---> B(Bridge1) ---> C(Bridge2) --->D(Receiver)
         *
         * 在配置上，从 D 的 Rx 角度来看，RxTransport 有两个，一个是 A-->D 的 RxTransport，另一个是 C-->D 的 RxTransport。
         * 但是由于 Bridge 的 context 不是通过该收发程序启动的，所以无法计算发送和接收的数量。所以只能对比 A 向 A--->D 的 TxTransport 发送的消息量和 D 从 A--->D 的 RxTransport 接收到的消息量是否相等来确定 A--->D 之间是否连通。
         *
         * 在 OnMessage 接收消息时，D 是从 C(Bridge2)-->D 的 RxTransport 上接收消息的。而该 RxTransport 需要转换成 A-->D 的 RxTransport，Bridge 需要对用户隐藏。
         * 所以在这里将 TxTransport 写入到消息体中，在 OnMessage 接收到消息时，解析出对应的 Endpoint 和 partition，然后找到 A--->D 的 RxTransport，将接收到的消息记录到该 RxTransport 上。
         *
         * 如此，就可以将校验 A--->D 的 TxTransport 和 A--->D 的 RxTransport 的消息数量是否相等了。
         */
        std::string content;
        auto &rx_transport_id_set = GetTxStreamIDs();
        for (auto tp_id : rx_transport_id_set)
        {
            auto *tp_info = GetTransportInfo(tp_id);
            assert(tp_info != NULL);
            auto &transport_info = *tp_info;
            content.append(transport_info.transport_name);
            content.append(",");
        }

        std::vector<int32_t> partitions;
        GetTxEndpointPartitions(ep_name, partitions);
        tx_endpoint_map_.insert(std::make_pair(ep_name, EndpointInfo(ep_hdl, partitions, lb_groups, isLoadBalance, content)));
        return ErrorCode::kSuccess;
    }

    static std::string ParseEndpointStr(const std::string &input, std::vector<int32_t> &partition_list)
    {
        std::vector<std::string> splits;
        boost::split(splits, input, boost::is_any_of(":,"), boost::token_compress_on);

        if (splits.empty())
        {
            return std::string();
        }

        for (size_t i = 1; i < splits.size(); ++i)
        {
            try
            {
                partition_list.push_back(boost::lexical_cast<int32_t>(splits[i]));
            }
            catch (...)
            {
            }
        }

        if (splits.size() == 1)
        {
            partition_list.push_back(1);
        }

        return splits[0];
    }

    // 获取 context_name 的 mastercontext 的 RxTransport
    void getRxTransport(ami::DomainServerClient *ds_client, std::string &context_name, const std::string &TierName, const TransportInfo *transport_info)
    {
        ami::Property prop;
        // 获取 context 的属性
        ds_client->GetConfig("Context/" + context_name, prop);

        // 读取 master context
        std::vector<std::string> master_vec;
        master_vec = prop.GetValue(ami::config::context::kMasterContext, master_vec);

        std::string transportName;
        ami::Property master_prop;
        ds_client->GetConfig("Context/" + master_vec[0], master_prop);

        // 读取 master context 的 rx endpoint
        std::vector<std::string> master_rx_endpoints = master_prop.GetValue(ami::config::context::kRxEndpoints, std::vector<std::string>());

        for (auto &ep_name_str : master_rx_endpoints)
        {
            std::string tier_name;
            std::vector<int32_t> partitions;
            std::string ep_name = ParseEndpointStr(ep_name_str, partitions);

            ami::Property endpoint_prop;
            ds_client->GetConfig("Endpoint/" + ep_name, endpoint_prop);

            // 读取每个 rx endpoint 上的 senders
            std::vector<std::string> tier_names = endpoint_prop.GetValue(ami::config::endpoint::kSenders, std::vector<std::string>());

            // FIXME: 对于 Bridge + 主从集群的场景，endpoint 的 “Senders” 会有多个：发送端的 Sender 和 bridge 的 Sender
            // 目前这种场景暂由控制脚本来做区分
            for (auto &tier_name : tier_names)
            {
                std::vector<std::string> tierVec;
                boost::split(tierVec, tier_name, boost::is_any_of(":"), boost::token_compress_on);

                for (auto &partition : partitions)
                {
                    transportName = ep_name + "_" + std::to_string(partition) + "_" + tierVec[0];

                    // 建立 TransportName 与 接收消息数量 的映射
                    rx_transport_count_map_.emplace(transportName, 0);
                    // 建立 TransportName 与 endpoint 的映射
                    transport_ep_map_.emplace(transportName, ep_name);
                    // 建立 TransportName 与 TierName 的映射
                    transport_TierName_map_.emplace(transportName, TierName);
                    // 建立 TransportName 与发送端 TierName 的映射
                    /*
                     * 这里需要注意的是，作为从集群的节点或者灾备集群的节点，这里记录的发送端 tierName 可能不对。
                     * 但是对于这些节点，控制脚本并不会解析发送端 tiername，控制脚本只关注主集群节点的 tiername。
                     * 这里保存发送端 tiername 完全是为了在写文件的时候少用一次条件判断
                     **/
                    transport_SenderTierName_map_.emplace(transportName, transport_info->tier_name);
                }
            }
        }
    }

    virtual int32_t OnAmiInitEnd()
    {
        // 读取当前 Context 的名字
        context_name_ = GetApplicationName();

        // 通过 Http 接口连接 domain server 获取相应配置。
        std::string ds_addr = GetOptionArgument<std::string>("domain-server");
        ds_addr.erase(ds_addr.begin(), ds_addr.begin() + 1);
        ds_addr.erase(ds_addr.end() - 1, ds_addr.end());
        ami::DomainServerClient *ds_client = ami::DomainServerClient::Get(ds_addr);

        ami::Property context_prop;
        ds_client->GetConfig("Context/" + context_name_, context_prop);
        // 判断是否是灾备集群的从集群
        // 灾备集群和灾备集群的从集群之间有一个特殊的属性，需要根据这个特殊属性判断是灾备集群还是灾备集群的从集群
        bool is_disaster_slave_context_ = context_prop.GetValue(ami::config::context::kIsDisasterContext, false);
        // 判断是否是灾备集群或者灾备集群的从集群（灾备集群和灾备集群的从集群都有 kIsDisasterBackup 这个属性）
        bool is_disaster_ = context_prop.GetValue(ami::config::context::kIsDisasterBackup, false);
        // 读取当前节点的 tierName
        std::string tierName = context_prop.GetValue(ami::config::context::kTierName, "");

        // 读取发送端 TxTransportName，建立 transportName 与收发消息数量的映射关系
        // 如果相同的 transportName 的收发数量一致，就认为对应的节点是连通的
        auto &transport_id_set = GetTxStreamIDs();
        for (auto tp_id : transport_id_set)
        {
            auto *tp_info = GetTransportInfo(tp_id);
            assert(tp_info != NULL);
            auto &transport_info = *tp_info;

            // 如果是灾备集群，不统计 TxTransport（灾备集群向 TxTransport 发送会被 AMI 过滤，对端无法接收到该消息）
            if (is_disaster_)
            {
                break;
            }

            // 判断当前 endpoint 是否设置了 LoadBalance
            auto it = tx_endpoint_map_.find(transport_info.endpoint_name);
            if (it == tx_endpoint_map_.end())
            {
                continue;
            }

            // 对于设置了负载均衡组的 endpoint，需要重新构建 TxTransport
            /*
             * 由于需要统计向每一个 TxTransport 发送的消息数量，但是对于开启了负载均衡的主题来说，发送到哪一个分区是由 AMI RR 的，应用程序无法得知发向哪一个 Transport，所以在这里需要重新构建 TxTransport，可以在发送消息的时候统计出来发送数量。
             *
             * 负载均衡分为两种情况：1. 设置了负载均衡但是没有设置负载均衡组；2. 设置了负载均衡组。
             * 对于第一种情况，无法得知发送向哪一个 partition，所以干脆对这两种情况的统计发送数量的统计逻辑进行重新设计：
             *
             * 这里将 TxTransport 进行了更改，由原本的 endpoint_partition_tierName 改为了 endpoint_groupNo_tierName。如果设置了负载均衡但是没有设置负载均衡组，将 groupNo 设置为 1。
             * 对于这种新构建出来的 Transport，就需要保存额外的信息对其进行标识，比如 partition_members，控制脚本在消息校验的时候统计对应的 RxTransport 上接收到的消息数量。
             *
             * 对于每一个设置了负载均衡的 endpoint，将记录对应的 partition_members，控制脚本在做消息校验的时候可以读取发送端 TxTransport 的发送消息数量，并解析该 TxTransport 上的 partition_members，用 partition_members 里面的 partition 替换原有的 TxTransport 中的 partition，获取到真实的多个 RxTransport。在做消息校验的时候根据 TxTransport 和 RxTransport 的收发数量关系进行判断。
             *
             * 对于这种设置了负载均衡的 endpoint 的消息的校验，分为两步：
             * 1. 如果 RxTransport 上接收到的数量之和小于 TxTransport 上发送的数量，则认为发送端和所有负载均衡的接收端都出现了连通性问题
             * 2. 如果 RxTransport 上接收到的数量之和等于 TxTransport 上发送的数量，则需要找到接收消息数量最多的 RxTransport，对比其他的 RxTransport，其他 RxTransport 接收到的消息数量如果小于 接收消息数量最多的那个 RxTransport 接收到的数量，就认为该 RxTransport 的节点出现了连通性问题。
             * */
            if (tx_endpoint_map_[transport_info.endpoint_name].is_loadbalance_)
            {
                // 如果没有设置负载均衡组，则默认为组1
                int16_t group_no = 1;
                // 当前 partition 属于哪一个 LoadBalanceGroup
                ami::Property prop;
                ds_client->GetConfig("Endpoint/" + transport_info.endpoint_name, prop);

                auto load_balance_groups_vec = prop.GetValue(ami::config::endpoint::kLoadBalanceGroups, std::vector<ami::Property>());
                for (auto &item : load_balance_groups_vec)
                {
                    auto pt_vec = item.GetValue(ami::config::endpoint::kPartitions, std::vector<int32_t>());
                    if (std::find(pt_vec.begin(), pt_vec.end(), transport_info.transport_partition) != pt_vec.end())
                    {
                        group_no = item.GetValue(ami::config::endpoint::kGroupNo, 1);
                        break;
                    }
                }

                // endpoint + loadbalanceGroup + tier_name 得到新的 transportName
                std::string transportName = transport_info.endpoint_name + "_" + std::to_string(group_no) + "_" + transport_info.tier_name;

                // 建立 transportName 和包含的 partition members 的映射关系
                // 检查该 transportName 是否已经在 transport_partition_map_ 中存在
                auto it = transport_partition_map_.find(transportName);
                if (it == transport_partition_map_.end())
                {
                    transport_partition_map_.emplace(transportName, std::to_string(transport_info.transport_partition));
                }
                else
                {
                    // 如果该 transportName 已经存在，则将当前 partition 追加到原来的 partition 后面，并以 - 为分隔符
                    it->second.append("-" + std::to_string(transport_info.transport_partition));
                }

                // 建立 transport 与 endpoint 的映射
                transport_ep_map_.emplace(transportName, transport_info.endpoint_name);
                // 建立 transportName 和发送数量的映射
                tx_transport_count_map_.emplace(transportName, 0);
                // 建立 TransportName 与 TierName 的映射
                transport_TierName_map_.emplace(transportName, tierName);
                // 建立 TransportName 与发送端 TierName 的映射
                transport_SenderTierName_map_.emplace(transportName, transport_info.tier_name);
            }
            else
            {
                transport_ep_map_.emplace(transport_info.transport_name, transport_info.endpoint_name);
                transport_TierName_map_.emplace(transport_info.transport_name, tierName);
                transport_SenderTierName_map_.emplace(transport_info.transport_name, transport_info.tier_name);
                tx_transport_count_map_.emplace(transport_info.transport_name, 0);
            }
        }

        // 统计接收端的 RxTransport
        /* 对于接收端的 RxTransport，需要处理跨 Bridge 以及从集群、灾备集群这些特殊的场景。跨 Bridge 的场景以上已经介绍过，下面单独详细介绍一下对于从集群和灾备集群的处理逻辑
         * 比如有如下场景：
         *                                       |---> C(slave Receiver2)
         *    A(Sender) ---> B(master Receiver1) |
         *                                       |---> D(disaster Receiver3) ---> E(disaster slave Receiver4)
         *
         * A 是发送端，B 是接收端主，C 是 B 的从集群；D 是 B 的灾备节点，E 是 D 的从集群。
         *
         * 对于 B 来说，其 RxTransport 和 A ---> B 的 TxTransport 相同，所以两者的消息校验可以通过对比这两个 transport 的收发消息数量是否一致来判断是否连通。
         *
         * 对 C 节点来说，C 在 OnMessage 中解析出来的 transport 是 A ---> B 的 transport。对于 A ---> C 之间的消息流，是 A 发送给 B，然后由 B 自动转发给 C，B 转发给 C 的这一动作是由 AMI 内部完成的，无法进行统计计数。而对于用户来说，C 看到的消息流就是 A ---> C 之间的，所以 B 的转发作用应该对用户不可见。
         * 所以对于 C 来说，RxTransport 应该是其主节点 B 的 RxTransport，所以 C 节点需要获取到其 Master context(B) 的 RxTransport，在接收到消息时应该统计到这个 RxTransport 上，进行消息检验的时候校验 B 和 C 的 RxTransport 接收到的消息数量是否一致。
         *
         * 但是，这里需要注意的是，最后展示给用户的连通性信息中 B C 之间的 transport 不能展示统计计数时候的 RxTransport，而应该展示的是 B C 之间进行消息转发的 B_FW_1_B 的 transport。所以还需要记录额外的信息，以辅助控制脚本在消息校验时给出正确的结果。
         *
         * 同样的，D 节点的处理方式和 C 节点类似，不过展示给用户的 B D 之间的 transport 是 B_UFW_1_B 的 transport
         *
         * 对于 E 节点来说，RxTransport 应该也是 A ---> B 的 transport，但是不同于 C D 的是，E 节点需要获取到其 master context(D) 的 master context(B) 的 RxTransport，并将统计消息记录到该 RxTransport 上。
         **/
        auto &rx_transport_id_set = GetRxStreamIDs();
        for (auto tp_id : rx_transport_id_set)
        {
            auto *tp_info = GetTransportInfo(tp_id);
            assert(tp_info != NULL);
            auto &transport_info = *tp_info;

            std::string master_list;
            std::vector<std::string> master_vec;
            // 读取 kMasterContext 属性，根据该属性判断是否是灾备集群或从集群
            master_vec = context_prop.GetValue(ami::config::context::kMasterContext, master_vec);

            // 如果 kMasterContext 属性不为空，则可能是灾备集群或者从集群
            if (!master_vec.empty())
            {
                // 灾备集群的从集群节点：记录主集群节点的 RxTransport
                if (is_disaster_slave_context_)
                {
                    getRxTransport(ds_client, master_vec[0], tierName, tp_info);
                }
                else
                {
                    // 灾备：记录主集群节点的 RxTransport
                    if (is_disaster_)
                    {
                        getRxTransport(ds_client, context_name_, tierName, tp_info);

                        // 这里记录了灾备节点，区分灾备集群和从集群
                        disaster_vec_.emplace_back(context_name_);
                    }
                    // 主从集群：记录主集群节点的 RxTransport
                    else
                    {
                        getRxTransport(ds_client, context_name_, tierName, tp_info);
                    }
                }

                // 如果是灾备集群或者从集群，这里需要记录的是主集群的节点，控制脚本需要根据主集群的节点找到对应的 RxTransport，判断接收消息数量是否一致
                for (auto &master_context : master_vec)
                {
                    master_list.append(master_context);
                    master_list.append("-");
                }

                master_slave_map_.emplace(context_name_, master_list);
            }
            else
            {
                transport_ep_map_.emplace(transport_info.transport_name, transport_info.endpoint_name);
                transport_TierName_map_.emplace(transport_info.transport_name, tierName);
                transport_SenderTierName_map_.emplace(transport_info.transport_name, transport_info.tier_name);
                rx_transport_count_map_.emplace(transport_info.transport_name, 0);
            }
        }

        // 注册开始信号，当所有节点接收到信号之后再开始收发消息
        int ret = RegisterSignal();
        if (ret != 0)
        {
            ADK_LOG_ERROR_AC_TF("RegisterSignal failed!", "");
            return ErrorCode::kFailure;
        }

        // 等待控制端发送信号
        while (!AmiReceiver::start_)
        {
            sleep(1);
        }

        /*
         * 这里加延时的作用是考虑到生产环境中 context 数量比较多，而控制脚本需要逐个启动 context，这就导致了发送端和接收端启动产生了时间差。
         * 如果取消该延时，有可能出现发送端已经开始发送消息了，但是接收端启动太慢，窗口被推进，导致消息丢失的情况。
         * 增加 60 秒延时，保证控制脚本可以给所有 context 发送完信号，保证所有 context 全部接收到信号才开始收发
         * */
        sleep(60);

        return ErrorCode::kSuccess;
    }

    virtual void OnMessage(ami::Message *msg)
    {
        std::string stats;
        ++nr_message_received_;
        auto tp_id = msg->get_transport_id();

        // 获取 endpoint_partition_no，和消息体中的 transportName 做对比，找到对应的发送端 transportName
        auto ep_name = msg->get_endpoint_name();
        auto partition_no = msg->get_partition_no();
        std::string transportName;
        std::string message = msg->data();
        std::vector<std::string> transportVec;
        boost::split(transportVec, message, boost::is_any_of(","), boost::token_compress_on);
        for (const auto &transport : transportVec)
        {
            if (boost::algorithm::starts_with(transport, std::string(ep_name + "_" + std::to_string(partition_no))))
            {
                transportName = transport;
                break;
            }
        }

        // clang-format off
        stats = (boost::format("\n=====================================================\n"
                               "total messages : %1%, message rate : 1\n") % nr_message_received_).str();

        stats.append((boost::format("\nEndpoint: <%1%>, Partition <%2%>, Transport <%3%>, TransportName <%4%>\n")
                    % ep_name % partition_no % tp_id % transportName).str());

        stats.append((boost::format("\nReceive message: <%1%>") % message).str());
        // clang-format on

        stats.append("\n=====================================================\n");
        ADK_LOG_INFO_AC_TF("stats", stats);

        // 更新 rx_transport_count_map_
        auto it = rx_transport_count_map_.find(transportName);
        if (it != rx_transport_count_map_.end())
        {
            ++(it->second);
        }
    }

    virtual int32_t OnRun()
    {
        std::string stats;
        adk::SimpleVariableRateCtrl *rate_change_ctrl = new adk::SimpleVariableRateCtrl(1, 1);

        auto it_begin = tx_endpoint_map_.begin();
        auto it_end = tx_endpoint_map_.end();
        auto it_cur = it_begin;

        uint64_t total_send = 0;
        while (is_running())
        {
            while (nmsg_before_suspend_)
            {
                nmsg_before_suspend_--;
                it_cur = it_begin;
                while (it_cur != it_end)
                {
                    std::string transportName;
                    std::string tierName;
                    std::vector<ami::Property> trp_props;
                    ami_context_->PropertyAt(ami::config::context::kTransportInfoList).GetValue(trp_props);

                    for (auto &tp_info : trp_props)
                    {
                        const auto direction = tp_info.GetValue(ami::config::context::kTransportDirection, 0);
                        if (1 == direction || 2 == direction)
                        {
                            tierName = tp_info.GetValue(ami::config::context::kTierName, "");
                        }
                    }

                    auto &tx_ep_info = it_cur->second;
                    // 对于设置了负载均衡的主题的发送数量统计的特殊处理
                    if (tx_ep_info.is_loadbalance_)
                    {
                        // 如果设置了负载均衡，但是没有设置负载均衡组，发送端的 transport 为 endpoint_1_tierName
                        if (tx_ep_info.lb_groups_.empty())
                        {
                            // 更新 tx_transport_count_map_ 中相应 transportName 的发送消息数量
                            transportName = it_cur->first + "_1_" + tierName;
                            auto it = tx_transport_count_map_.find(transportName);
                            if (it != tx_transport_count_map_.end())
                            {
                                ++(it->second);
                            }

                            tx_ep_info.ep_hdl_->SendMsg(it_cur->second.content);
                            ++tx_ep_info.nr_send_msgs_;

                            ++total_send;
                            // clang-format off
                            stats = (boost::format("\n=====================================================\n"
                                                "total messages : %1%, message rate : 0\n") % total_send).str();

                            stats.append((boost::format("\nendpoint: <%1%>, lbgroup: <%2%>, tx messages: %3%, transport name: %4%\n")
                                        % it_cur->first % std::to_string(1) % tx_ep_info.nr_send_msgs_ % transportName).str());

                            stats.append((boost::format("\nsend message: <%1%>") % it_cur->second.content).str());
                            // clang-format on

                            stats.append("\n=====================================================\n");
                            ADK_LOG_INFO_AC_TF("stats", stats);
                        }
                        else
                        {
                            // 设置了负载均衡组，则 TxTransport 为 endpoint_groupNo_tierName
                            for (auto lbgroup : tx_ep_info.lb_groups_)
                            {
                                // 更新 tx_transport_count_map_ 中相应 transportName 的发送消息数量
                                transportName = it_cur->first + "_" + std::to_string(lbgroup) + "_" + tierName;
                                auto it = tx_transport_count_map_.find(transportName);
                                if (it != tx_transport_count_map_.end())
                                {
                                    ++(it->second);
                                }

                                tx_ep_info.ep_hdl_->SendMsg(it_cur->second.content, lbgroup);
                                ++tx_ep_info.nr_send_msgs_;

                                ++total_send;
                                // clang-format off
                                stats = (boost::format("\n=====================================================\n"
                                                    "total messages : %1%, message rate : 0\n") % total_send).str();

                                stats.append((boost::format("\nendpoint: <%1%>, lbgroup: <%2%>, tx messages: %3%, transport name: %4%\n")
                                            % it_cur->first % std::to_string(1) % tx_ep_info.nr_send_msgs_ % transportName).str());

                                stats.append((boost::format("\nsend message: <%1%>") % it_cur->second.content).str());
                                // clang-format on

                                stats.append("\n=====================================================\n");
                                ADK_LOG_INFO_AC_TF("stats", stats);
                            }
                        }
                    }
                    else
                    {
                        // 如果没有设置负载均衡，则 transport 为 endpoint_partition_tierName
                        for (auto partition : tx_ep_info.partitions_)
                        {
                            transportName = it_cur->first + "_" + std::to_string(partition) + "_" + tierName;
                            auto it = tx_transport_count_map_.find(transportName);
                            if (it != tx_transport_count_map_.end())
                            {
                                ++(it->second);
                            }

                            tx_ep_info.ep_hdl_->SendMsg(it_cur->second.content, partition);
                            ++tx_ep_info.nr_send_msgs_;

                            ++total_send;
                            // clang-format off
                            stats = (boost::format("\n=====================================================\n"
                                                "total messages : %1%, message rate : 0\n") % total_send).str();

                            stats.append((boost::format("\nendpoint: <%1%>, lbgroup: <%2%>, tx messages: %3%, transport name: %4%\n")
                                        % it_cur->first % std::to_string(1) % tx_ep_info.nr_send_msgs_ % transportName).str());

                            stats.append((boost::format("\nsend message: <%1%>") % it_cur->second.content).str());
                            // clang-format on

                            stats.append("\n=====================================================\n");
                            ADK_LOG_INFO_AC_TF("stats", stats);
                        }
                    }

                    ++it_cur;
                }

                rate_change_ctrl->Wait();
            }
        }

        GenericAmiApplication::StopAmiApp();
        return ErrorCode::kSuccess;
    }

    virtual void OnIdle()
    {
        ::usleep(1);
    }

    void OnAmiExitBegin()
    {
        // 将 tx_transport_count_map_ rx_transport_count_map_ 写入到文件中，文件名以 context name 为前缀
        std::string output_file_;
        std::ofstream output_stream_;

        output_file_ = context_name_ + ".json";
        output_stream_.open(output_file_, std::ios_base::out | std::ios_base::app);
        if (!output_stream_.is_open())
        {
            fprintf(stderr, "Open output file stream error\n");
            return;
        }

        // 将 tx_transport_count_map_ 写入文件的时候，需要判断 tx_transport_count_map_ 中的每一个 transportName 是否在 transport_partition_map_ 中存在
        // 如果存在，说明该 transportName 上对应的主题是负载均衡的，需要将 isLoadBalance = true 和 partition_members 同时写入到文件
        for (auto it = tx_transport_count_map_.begin(); it != tx_transport_count_map_.end(); ++it)
        {
            adk::Property prop;
            auto iterator = transport_partition_map_.find(it->first);
            if (iterator == transport_partition_map_.end())
            {
                // clang-format off
                prop("tx", std::to_string(it->second))
                    ("context", context_name_)
                    ("Endpoint", transport_ep_map_[it->first])
                    ("TierName", transport_TierName_map_[it->first])
                    ("sender_tier_name", transport_SenderTierName_map_[it->first])
                    ("isloadbalance", "false");
                // clang-format on
            }
            else
            {
                // clang-format off
                // 如果是设置了负载均衡的 TxTransport，则还需要保存 partition_members 这个属性
                prop("tx", std::to_string(it->second))
                    ("context", context_name_)
                    ("Endpoint", transport_ep_map_[it->first])
                    ("TierName", transport_TierName_map_[it->first])
                    ("sender_tier_name", transport_SenderTierName_map_[it->first])
                    ("partition_members", iterator->second)
                    ("isloadbalance", "true");
                // clang-format on
            }

            std::string transportInfo = "transportId/" + it->first + " " + prop.Dump();
            output_stream_ << transportInfo << std::endl;
        }

        for (auto it = rx_transport_count_map_.begin(); it != rx_transport_count_map_.end(); ++it)
        {
            // clang-format off
            adk::Property prop;
            auto iterator = master_slave_map_.find(context_name_);
            // 主集群的节点，既不是从集群、也不是灾备集群，就不用保存 is_disaster 和 MasterContext 这两个属性
            if (iterator == master_slave_map_.end())
            {
                prop("rx", std::to_string(it->second))
                    ("context", context_name_)
                    ("Endpoint", transport_ep_map_[it->first])
                    ("TierName", transport_TierName_map_[it->first])
                    ("sender_tier_name", transport_SenderTierName_map_[it->first]);
            }
            else
            {
                if (std::find(disaster_vec_.begin(), disaster_vec_.end(), context_name_) != disaster_vec_.end())
                {
                    prop("rx", std::to_string(it->second))
                        ("context", context_name_)
                        ("Endpoint", transport_ep_map_[it->first])
                        ("TierName", transport_TierName_map_[it->first])
                        ("sender_tier_name", transport_SenderTierName_map_[it->first])
                        ("is_disaster", "true")
                        ("MasterContext", master_slave_map_[context_name_]);
                }
                else
                {
                    prop("rx", std::to_string(it->second))
                        ("context", context_name_)
                        ("Endpoint", transport_ep_map_[it->first])
                        ("TierName", transport_TierName_map_[it->first])
                        ("sender_tier_name", transport_SenderTierName_map_[it->first])
                        ("is_disaster", "false")
                        ("MasterContext", master_slave_map_[context_name_]);
                }
            }
            // clang-format on

            std::string transportInfo = "transportId/" + it->first + " " + prop.Dump();
            output_stream_ << transportInfo << std::endl;
        }

        if (output_stream_.is_open())
        {
            output_stream_.close();
        }
    }

private:
    std::map<std::string, EndpointInfo> tx_endpoint_map_;
    std::string rate_;              // 发送消息的速率
    uint64_t nmsg_before_suspend_;  // 发送消息的数量

    std::map<std::string, uint64_t> rx_transport_count_map_;          // 接收端 transportName 和消息数量的映射
    std::map<std::string, uint64_t> tx_transport_count_map_;          // 发送端 transportName 和消息数量的映射
    std::map<std::string, std::string> transport_partition_map_;      // transportName 和 partition 的映射
    std::map<std::string, std::string> transport_TierName_map_;       // transportName 与 TierName 的映射
    std::map<std::string, std::string> transport_SenderTierName_map_; // transport 与发送端集群名 tiername 的映射
    std::map<std::string, std::string> transport_ep_map_;             // transportName 与 endpoint 的映射
    std::map<std::string, std::string> master_slave_map_;             // 主从集群的映射
    std::vector<std::string> disaster_vec_;                           // 灾备集群的节点
    std::string context_name_;                                        // context name
    ami::Context *ami_context_;
    static bool start_;
    static bool stop_;

    uint64_t nr_message_received_; // 接收到的消息数量
} g_ami_receiver;

bool AmiReceiver::start_ = false;
bool AmiReceiver::stop_ = false;

ADK_LOG_DEFINE(AmiReceiver);
