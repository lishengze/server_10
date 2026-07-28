#include <assert.h>

#include <iostream>
#include <string>
#include <stdexcept>

#include <boost/property_tree/json_parser.hpp>

#include <aaf.h>
#include <aaf/sharding_channel.h>
#include <aaf/sharding_shm_manager.h>

#include <adk/util.h>
#include <adk/pipeline.h>
#include <adk/random.h>
#include <adk/monitor/monitor.h>

#include <boost/unordered_map.hpp>
#include <map>

#include "sharding_follow_demo.h"

using namespace aaf;
using namespace boost::interprocess;

ADK_LOG_DECLARE_AC(300000);

// 消息转发的主题关系映射
struct StreamEP
{
    std::string from_ep;
    std::string to_ep;
};

ShmDataManager* g_shm_mgr = nullptr;

class AppStageWorker : public adk::StageWorker<ADK_IO(AppMessage*, AppMessage*)>
{
public:
    AppStageWorker(const std::string& name, bool is_end)
        : adk::StageWorker<ADK_IO(AppMessage*, AppMessage*)>(name)
    {
        stage_name_ = name;
        is_end_ = is_end;
        sharding_index_ = GenericAmiApplication::GetShardingIndex();
        ADK_LOG_INFO_AC_TF("print stage info",
                           "sharding index <{1}>, stage name <{2}>, is end <{3}>",
                           sharding_index_,
                           stage_name_,
                           is_end_);
    }

    ~AppStageWorker()
    {
    }

    virtual void OnMessage(AppMessage*& msg, short dim, short idx);

    struct CountInfo
    {
        uint64_t nr_total;
        uint64_t nr_broadcast;
        uint64_t nr_route;
    };

    uint64_t msg_count_ = 0;                                // 统计总共收到的消息数量
    std::map<std::string, CountInfo> nr_from_ep_msg_map_;    // 主题名<->该主题收到的消息数量映射
    std::map<std::string, uint64_t> nr_to_ep_msg_map_;      // 转发的目标主题名<->转发的消息数量
    uint64_t nr_broadcast_msgs_ = 0;;
    uint64_t nr_route_msgs_ = 0;

    std::map<std::string, EndpointHandler*>* endpoint_map_;
    std::vector<StreamEP>* forward_msg_vec_;
    std::map<std::string, std::string>* orig_forward_msg_map_;

    int32_t  sharding_index_ = 0; // 当前分片进程的索引

private:
    bool is_end_ = false;
    std::string stage_name_;
};

class AmiApp : public GenericAmiApplication 
{
public:
    AmiApp()
    {}

    ~AmiApp()
    {}

    // configure program options
    virtual void SetAmiAppOption()
    {
        AAF_ADDOPT_ACCEPTOR_NARG("enable-ha-context", "enable ha-context", enable_ha_);
        AAF_ADDOPT_ACCEPTOR_NARG("enable-sg-context", "enable singleton-context", enable_sg_);

        // 消息驱动程序的启动参数
        AAF_ADDOPT_ACCEPTOR("forward-msg", "forward msg route; examples: \"A>B,B>C\"", forward_msg_str_, forward_msg_str_);              //消息转发逻辑，如A>B，即从A接收消息发送至B上
    }
    // =======================================================================================

    void OnAmiAppOption(const std::string& option_name)
    {
        if (option_name == "sharding-num")
        {
            sharding_num_ = GetOptionArgument<int32_t>("sharding-num");
        }
    }

    // ================================configure aaf=======================================

    void OnConfigureFramework(ami::Property& fw_props)
    {
        fw_props.SetValue(config::kEnableHighAvailableContext, enable_ha_);
        fw_props.SetValue(config::kEnableSingletonContext, enable_sg_);
        fw_props.SetValue(config::kEnableAppNameCheck, false);
    }

    // ================================AAF Init=======================================
    int32_t OnFrameworkInitBegin()
    {
        app_name_ = GetApplicationName();

        if (sharding_num_ != 0)
        {
            g_shm_mgr = ShmDataManager::GetInstance();
            pbu_seq_lock_ = g_shm_mgr->CreateSeqLock("pbu_seq_lock");
            assert(pbu_seq_lock_);
        }

        return aaf::ErrorCode::kSuccess;
    }

    bool OnCollectIndicator(boost::property_tree::ptree& indicator)
    {
        indicator.put("ha_recv_msgs", ha_msg_recv_count_);
        indicator.put("sg_recv_msgs", sg_msg_recv_count_);
        indicator.put("sg_fwd_msgs", sg_msg_send_count_);
        indicator.put("nr_broadcast_msgs", stage_worker_4_->nr_broadcast_msgs_);
        indicator.put("nr_route_msgs", stage_worker_4_->nr_route_msgs_);

        return true;
    }

    // ================================AAF Init=======================================
    int32_t OnAmiInitBegin() override
    {
        app_name_ = GetApplicationName();
        adk::MonitorOps app_monitor_ops;
        app_monitor_ops.is_collection_indicator = true;
        app_monitor_ops.on_collection_indicator = boost::bind(
                                        &AmiApp::OnCollectIndicator, this, _1);
        adk::Monitor::RegisterObject("AmiAppTest", app_name_, &app_monitor_ops);

        // 保存当前进程的分片信息
        sharding_index_ = GenericAmiApplication::GetShardingIndex();
        // 主题消息转发逻辑，用户传入"A>B"，表示A主题转发给B主题
        // 分片进程下规则：A->B_{sharding_index}_{partition}，partition默认都是1分区
        if (!forward_msg_str_.empty())
        {
            std::vector<std::string> channels;
            boost::split(channels, forward_msg_str_, boost::is_any_of(","), boost::token_compress_on);
            for (const auto& channel : channels)
            {
                std::vector<std::string> from_to;
                boost::split(from_to, channel, boost::is_any_of(">"), boost::token_compress_on);
                if (from_to.size() != 2)
                {
                    ADK_LOG_ERROR_AC_TF("", "forward-msg is error, content: {1}", channel);
                    return ErrorCode::kFailure;
                }
                
                // 根据分片进程信息调整转发的目标主题，例如：B_{sharding_index}_{partition}
                std::string to_ep_name = from_to[1];
                forward_msg_vec_.push_back({from_to[0], to_ep_name});
                orig_forward_msg_map_[from_to[0]] = from_to[1];
                ADK_LOG_INFO_AC_TF("paser forward-msg", "from: {1}, to: {2}", from_to[0], to_ep_name);
            }
        }

        // 开启pipeline
        stage_worker_1_ = new AppStageWorker("stage_worker_1", false);
        stage_worker_2_ = new AppStageWorker("stage_worker_2", false);
        stage_worker_3_ = new AppStageWorker("stage_worker_3", false);
        stage_worker_4_ = new AppStageWorker("stage_worker_4", true);

        // 最后一级流水线才需要做转发
        stage_worker_4_->endpoint_map_ = &endpoint_map_;
        stage_worker_4_->forward_msg_vec_ = &forward_msg_vec_;
        stage_worker_4_->orig_forward_msg_map_ = &orig_forward_msg_map_;

        pipeline_ = new adk::Pipeline(GetApplicationName(), "TradingEngine", 1024);

        pipeline_->Connect<adk::Pipeline::kInplace, AppMessage*>(*stage_worker_1_, *stage_worker_2_);
        pipeline_->Connect<adk::Pipeline::kInplace, AppMessage*>(*stage_worker_2_, *stage_worker_3_);
        pipeline_->Connect<adk::Pipeline::kInplace, AppMessage*>(*stage_worker_3_, *stage_worker_4_);

        pipeline_entrance_ = pipeline_->CreateEntrance<adk::Pipeline::kInplace>(*stage_worker_1_, 0, 1024);

        if (!pipeline_->Start())
        {
            ADK_LOG_ERROR_AC_TF("Pipeline Start fialed", "error msg: <{1}>", pipeline_->GetLastError());
            return aaf::ErrorCode::kFailure;
        }
        return aaf::ErrorCode::kSuccess;
    }

    int32_t OnRxEndpointCreation(const std::string& ep_name, ami::MessageHandler** msg_hdl, bool is_ha_ctx)
    {
        return ErrorCode::kSuccess;
    }

    // =================================AAF Run==============================================

    // \\?param={"RxEndpoint":"xxxx","Partition":"","IsJoin":true}
    std::string JoinLeaveRxEndpoint(std::string const& str)
    {
        ADK_LOG_INFO_AC_TF("", "recv query parameter {1}", str);

        static boost::regex re("\\?param=({.*})");
        boost::smatch sm;
        bool found = boost::regex_match(str, sm, re);
        if (!found)
        {
            ADK_LOG_ERROR_AC_TF("", "invalid query parameter {1}", str);
            return "invalid query parameter";
        }
        ami::Property http_request(sm[1]);
        std::string rx_name = http_request.GetValue("RxEndpoint", std::string(""));
        int32_t partiton = http_request.GetValue("Partition", 0);
        bool is_join = http_request.GetValue("IsJoin", true);

        if (rx_name.empty() || partiton <= 0)
        {
            ADK_LOG_ERROR_AC_TF("", "invalid query parameter {1}, Partition or RxEndpoint error", str);
            return "invalid query parameter, Partition or RxEndpoint error";
        }

        if (is_join)
        {
            GetContext()->JoinMulticastGroup(rx_name, partiton);
        }
        else
        {
            GetContext()->LeaveMulticastGroup(rx_name, partiton);
        }
        return "success";
    }

    std::string HandleHoldOn(std::string const& str)
    {
        ADK_LOG_INFO_AC_TF("", "recv query SetHoldOn");
        is_hold_on_ = true;
        return "success";
    }

    std::string HandleReleaseHoldOn(std::string const& str)
    {
        ADK_LOG_INFO_AC_TF("", "recv query HandleReleaseHoldOn");
        is_hold_on_ = false;
        return "success";
    }

    // CTE/CQE组件的分片进程里面不会调用应用实现的OnRun函数
    int32_t OnRun() override
    {
        if (sharding_num_ != 0)
        {
            // 返回Failure为了验证测试 
            return aaf::ErrorCode::kFailure;
        }

        if (GetContext() != nullptr)
        {
            ADK_LOG_INFO_AC_TF("", "gen http service");
            GetContext()->RegisterGetHttpURL("joinLeaveRxEndpoint([^/]*)", boost::bind(&AmiApp::JoinLeaveRxEndpoint, this, _1));
            GetContext()->RegisterGetHttpURL("setHoldOn", boost::bind(&AmiApp::HandleHoldOn, this, _1));
            GetContext()->RegisterGetHttpURL("releaseHoldOn", boost::bind(&AmiApp::HandleReleaseHoldOn, this, _1));
        }

        while (is_running())
        {
            sleep(1);
        }
        return aaf::ErrorCode::kPassed;
    }

    int32_t DoRoute(ami::Message* msg, const uint32_t sharding_num) override
    {
        assert(sharding_num > 0);
        AppMessage* app_msg = (AppMessage*)msg->const_data();
        // 消息体中标识该消息需要广播
        if (app_msg->is_broadcast_msg)
        {
            return 0;
        }

        // 分别对每个主题做RR路由
        // uint64_t topic_sqn = msg->topic_seq_num();
        uint64_t topic_sqn = app_msg->sqn;
        int32_t sharding_index = int32_t(topic_sqn % sharding_num);
        if (sharding_index == 0)
        {
            return (int32_t)sharding_num;
        }

        return sharding_index;
    }

    void OnMessage(ami::Message* msg) override
    {
        if (is_hold_on_)
        {
            usleep(10ul * 1000ul);
        }

        if (pbu_seq_lock_ != nullptr)
        {
            pbu_seq_lock_->Lock();
        }
        ++ha_msg_recv_count_;
        uint64_t recv_total = total_msg_recv_.fetch_add(1);
        AppMessage* ami_stage_msg = (AppMessage*)msg->data();
    
        AppMessage* stage_msg = new AppMessage();  // 最后一级流水线会delete
        memcpy(stage_msg, ami_stage_msg, sizeof(AppMessage));

        assert(pipeline_entrance_ != nullptr);

        adk::g_pipeline_total_order_seq_num = recv_total;
        stage_msg->total_order_sqn = msg->get_total_order_seq_num();
        pipeline_entrance_->Forward(stage_msg);  // 递给流水线

        if (pbu_seq_lock_ != nullptr)
        {
            if (!stage_msg->is_broadcast_msg)
            {
                pbu_seq_lock_->UnLock();
            }
        }
    }

    // 目前只有CQE组件会拉起单例Context，处理AGW的查询消息，查询消息是负载均衡模式，暂不检查消息内容
    void OnMessageSingleton(ami::Message* msg) override
    {
        uint64_t recv_total = total_msg_recv_.fetch_add(1);
        ++sg_msg_recv_count_;
        AppMessage* app_msg = (AppMessage*)msg->data();

        std::string endpoint_name = app_msg->from_ep_name;
        if (!forward_msg_vec_.empty())
        {
            for (const auto from_to_name : forward_msg_vec_)
            {
                if (endpoint_name != from_to_name.from_ep)
                {
                    continue;
                }

                auto iter = orig_forward_msg_map_.find(endpoint_name);
                if (iter == orig_forward_msg_map_.end())
                {
                    ADK_LOG_INFO_AC_TF("---", "endpoint: {1} is not exist in orig_forward_msg_map", endpoint_name);
                    abort();
                }

                // 根据启动参照，选择Tx进行发送，如AB->BC，即从AB接收消息发送至BC上
                std::string to_name = from_to_name.to_ep;
                auto to_iter = endpoint_map_.find(to_name);
                if (to_iter != endpoint_map_.end())
                {
                    // 由于分片的原因，更新消息内容：来源哪个主题
                    memcpy(app_msg->from_ep_name, iter->second.c_str(), iter->second.length());
                    app_msg->from_ep_name[iter->second.length()] = '\0';

                    auto to_ep_count = nr_to_ep_msg_map_.find(to_name);
                    if (to_ep_count != nr_to_ep_msg_map_.end())
                    {
                        ++to_ep_count->second;
                    }
                    else
                    {
                        nr_to_ep_msg_map_[to_name] = 1;
                    }

                    to_iter->second->SendMsg(app_msg, sizeof(AppMessage));
                    ++sg_msg_send_count_;
                }
                else
                {
                    abort();
                }
            }
        }
        else
        {
            abort();
        }
    }

    int32_t OnTxEndpointCreation(EndpointHandler* ep_hdl, const std::string& ep_name) override
    {
        ADK_LOG_INFO_AC_TF("", "ep_name: {1}", ep_name);
        std::vector<int32_t> partitions;
        GetTxEndpointPartitions(ep_name, partitions);
        if (partitions.empty())
        {
            abort();
        }
        endpoint_map_.insert(std::make_pair(ep_name, ep_hdl));
        return aaf::ErrorCode::kSuccess;
    }

    void OnRoleChangeToLeader() override
    {
        ADK_LOG_INFO_AC_TF("", "change to leader, <{1}>", sharding_index_);
    }

    void OnRoleChangeToMember() override
    {
        ADK_LOG_INFO_AC_TF("", "change to member, <{1}>", sharding_index_);
    }

    void OnRoleChangeToMaster() override
    {
        ADK_LOG_INFO_AC_TF("", "change to master, <{1}>", sharding_index_);
    }

    void OnRecoveryBegin() override
    {
        ADK_LOG_INFO_AC_TF("", "recovery begin, <{1}>", sharding_index_);
    }

    void OnRecoverySuccess() override
    {
        ADK_LOG_INFO_AC_TF("", "recovery success, <{1}>", sharding_index_);
    }

    void OnDiscardMessage(std::string& msg) override
    {
        // #FIXME: todo
    }

    void OnNoReceiver(const std::string& endpoint_name,
                      const std::string& partition) override
    {
        ADK_LOG_INFO_AC_TF("", "no receiver, {1}:{2}, <{3}>", endpoint_name, partition, sharding_index_);
    }

    void OnReceiverUp(const std::string& endpoint_name, const std::string& partition) 
    {
        ADK_LOG_INFO_AC_TF("", "receiver up, {1}:{2}, <{3}>", endpoint_name, partition, sharding_index_);
    }

    // void OnSignal(int sig_num, int value) 
    // {
    //     if (sig_num == 10)
    //     {
    //         if (value == 10)
    //         {
    //             is_hold_on_ = true;
    //             std::cout << "recv hold on signal" << std::endl;
    //         }

    //         if (value == 11)
    //         {
    //             is_hold_on_ = false;
    //             std::cout << "recv release hold on signal" << std::endl;
    //         }
    //     }
    //     GenericApplication::OnSignal(sig_num, value);
    // }

private:
    std::map<std::string, EndpointHandler*> endpoint_map_;
    std::vector<StreamEP> forward_msg_vec_;
    std::map<std::string, std::string> orig_forward_msg_map_;

    std::map<std::string, uint64_t> nr_to_ep_msg_map_;      // 转发的目标主题名<->转发的消息数量
    std::string forward_msg_str_;

    bool enable_ha_ = true;    
    bool enable_sg_ = false;
    bool is_hold_on_ = false;

    uint64_t ha_msg_recv_count_ = 0;     // 高可用Context收到的消息总数
    uint64_t sg_msg_recv_count_ = 0;     // 单例Context收到的消息总数
    uint64_t sg_msg_send_count_ = 0;     // 单例Context发出的消息总数
    std::atomic<uint64_t> total_msg_recv_;

    int32_t  sharding_num_ = 0;   // 分片进程数量，用于消息校验时做跳变检查
    int32_t  sharding_index_ = 0; // 当前分片进程的索引

    // pipeline功能
    adk::Pipeline* pipeline_ = nullptr;
    adk::PipelineEntrance<AppMessage*, adk::Pipeline::kInplace>* pipeline_entrance_ = nullptr;
    AppStageWorker* stage_worker_1_ = nullptr;
    AppStageWorker* stage_worker_2_ = nullptr;
    AppStageWorker* stage_worker_3_ = nullptr;
    AppStageWorker* stage_worker_4_ = nullptr;

    std::string app_name_;

    ShardingSeqLock* pbu_seq_lock_ = nullptr;

    int32_t log_threads_num_ = 0;
    std::vector<boost::thread> log_threads_;

    friend class AppStageWorker;
} g_ami_app;

void AppStageWorker::OnMessage(AppMessage*& msg, short dim, short idx)
{
    // 不可能在OnMesssage中抛出去异常 OnMesssage 已经不在主函数中调用了
    std::string endpoint_name = msg->from_ep_name;

    auto nr_ep_iter = nr_from_ep_msg_map_.find(endpoint_name);
    if (nr_ep_iter != nr_from_ep_msg_map_.end())
    {
        ++nr_ep_iter->second.nr_total;
        if (msg->is_broadcast_msg)
        {
            ++nr_ep_iter->second.nr_broadcast;
        }
        else
        {
            ++nr_ep_iter->second.nr_route;
        }
    }
    else
    {
        CountInfo info = {1, 0, 0};
        if (msg->is_broadcast_msg)
        {
            info.nr_broadcast = 1;
        }
        else
        {
            info.nr_route = 1;
        }
        auto ret = nr_from_ep_msg_map_.insert(std::make_pair(endpoint_name, info));
        nr_ep_iter = ret.first;
        assert(ret.second == true); // 插入成功
    }

    if (msg->is_broadcast_msg)
    {
        ++nr_broadcast_msgs_;
    }
    else
    {
        ++nr_route_msgs_;
    }

    ++msg_count_;

    if (is_end_)  // 最后一级流水线
    {
        if (msg_count_ % 1000 == 0)  // 打印日志
        {
            ADK_LOG_INFO_AC_TF("recv msg", "total: {1}", msg_count_);
            // 接收的消息数量
            for (const auto& ep_item : nr_from_ep_msg_map_)
            {
                ADK_LOG_INFO_AC_TF("recv msg", "endpoint: {1} total: {2}, sharding_index: {3}", 
                                   ep_item.first, 
                                   ep_item.second.nr_total,
                                   g_ami_app.GetShardingIndex());
            }

            // 转发的消息数量
            for (const auto& ep_item : nr_to_ep_msg_map_)
            {
                ADK_LOG_INFO_AC_TF("send msg", "endpoint: {1} total: {2}, sharding_index: {3}", 
                                    ep_item.first, 
                                    ep_item.second,
                                    g_ami_app.GetShardingIndex());
            }
        }

        if (msg->is_core)
        {
            ADK_LOG_ERROR_AC_TF("generate core", "sqn: {1}, from: {2}, total_order_sqn: {3}", 
                                msg->sqn, 
                                msg->from_ep_name,
                                msg->total_order_sqn);
            abort();
        }

        for (const auto from_to_name : *forward_msg_vec_)
        {
            // CQE场景下，高可用Context收到的消息虽然丢进流水线，但是不会做转发
            if (endpoint_name != from_to_name.from_ep)
            {
                continue;
            }

            auto iter = orig_forward_msg_map_->find(endpoint_name);
            if (iter == orig_forward_msg_map_->end())
            {
                ADK_LOG_INFO_AC_TF("---", "endpoint: {1} is not exist in orig_forward_msg_map", endpoint_name);
                abort();
            }

            // 根据启动参照，选择Tx进行发送，如AB->BC，即从AB接收消息发送至BC上
            std::string to_name = from_to_name.to_ep;
            auto to_iter = endpoint_map_->find(to_name);
            if (to_iter != endpoint_map_->end())
            {
                // 由于分片的原因，更新消息内容：来源哪个主题
                memcpy(msg->from_ep_name, iter->second.c_str(), iter->second.length());
                msg->from_ep_name[iter->second.length()] = '\0';

                auto to_ep_count = nr_to_ep_msg_map_.find(to_name);
                if (to_ep_count != nr_to_ep_msg_map_.end())
                {
                    ++to_ep_count->second;
                }
                else
                {
                    nr_to_ep_msg_map_[to_name] = 1;
                }

                to_iter->second->SendMsg(msg, sizeof(AppMessage));
            }
        }
        delete msg;
    }
    else
    {
        Forward(msg);
    }
}
