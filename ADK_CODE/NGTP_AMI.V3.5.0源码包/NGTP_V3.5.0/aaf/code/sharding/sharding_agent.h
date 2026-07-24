#pragma once

#include "constant.h"
#include "protocol.h"
#include "util.h"

#include <aaf.h>
#include <aaf/sharding_channel.h>
#include <adk/util.h>
#include <adk/entry_wrapper.h>
#include <adk/arch/generic.h>
#include <adk/lock_free_cont_memory.h>
#include <adk/monitor/monitor.h>
#include <adk/arch/synchronize.h>
#include <adk/shm_cont_channel.h>
#include <adk/lock_free_queue_variant.h>

#include <boost/format.hpp>

#include <time.h>
#include <fstream>
#include <algorithm>

#include "sharding_proxy.h"
#include "sharding_common.h"
#include "reorder_buffer.h"
#include "seq_lock.h"
#include "aaf/sharding_shm_manager.h"

namespace aaf
{
    extern ShardingChannel* g_sharding_channel;
}

using boost::interprocess::managed_shared_memory;
using boost::interprocess::shared_memory_object;

namespace sharding
{

struct SortElem 
{
    uint64_t total_sqn;         // 递交消息时的全局序
    int32_t sharding_index;     // 记录该消息递交的分片，
                                // 0 表示递交给所有分片，发送时按照分片序号依次处理
    int32_t pending;
};


/**
 * @实时进程：
 *          seq_lock： kShmDataAddrBegin 
 *                  -> $USER_$ctx_name_seq_lock;
 *          app_data： kShmDataAddrBegin + kSeqLockShmSize + kShmReserveSize
 *                  -> $USER_$ctx_name_app_data;
 * 
 *          trial_seq_lock： app_data_end + kShmReserveSize
 *                  -> $USER_$ctx_name_seq_lock_trial;
 *          trial_app_data： app_data_end + kSeqLockShmSize + kShmReserveSize
 *                  -> $USER_$ctx_name_app_data_trial;
 *      
 * @试算进程：
 *          seq_lock： kShmDataAddrBegin 
 *                  -> $USER_$ctx_name_seq_lock_trial;
 *          app_data： kShmDataAddrBegin + kSeqLockShmSize + kShmReserveSize
 *                  -> $USER_$ctx_name_app_data_trial;
 * 
 *  试算进程中，将 app_data_trial 共享内存映射到和实时进程 app_data 相同的虚拟地址上
 *  当共享内存被破坏时，实时进程将 app_data 中的内容直接拷贝到 _app_data_trial 上
 *  保证数据结构中保存的指针可以继续访问对应的 app_data_trial 共享内存中的对象
 */
class ShmDataMgr
{
    ADK_LOG_DECLARE_AC(510000);

    const uint64_t kShmDataAddrBegin = 0x600000000000;
    const uint64_t kShmReserveSize = 0x1000000; // 每个共享内存块之间的间隔，1MB

    const uint64_t kSeqLockShmSize = 64UL * 1024UL * 1024UL; // 64MB
    const uint64_t kAppDataShmSize; // 512MB

public:
    ShmDataMgr(uint64_t app_data_shm_size = 512UL * 1024UL * 1024UL)
        : kAppDataShmSize(app_data_shm_size)
    {
    }

    // agent create all shm
    int32_t InitShmData(const std::string& context_name, bool is_adv_follower)
    {
        ADK_LOG_INFO_AC_TF("", "AppDataShmSize: <{1}>", kAppDataShmSize);
        lock_shm_name_ = MakeShmNamePrefix(context_name) + "seq_lock";
        app_shm_name_  = MakeShmNamePrefix(context_name) + "app_data";

        std::string trial_lock_shm_name = lock_shm_name_ + "_trial";
        std::string trial_app_shm_name  = app_shm_name_ + "_trial";
        shared_memory_object::remove(lock_shm_name_.c_str());
        shared_memory_object::remove(app_shm_name_.c_str());
        shared_memory_object::remove(trial_lock_shm_name.c_str());
        shared_memory_object::remove(trial_app_shm_name.c_str());

        uint64_t addr = kShmDataAddrBegin;
        seq_lock_shm_ = new managed_shared_memory(boost::interprocess::open_or_create,
                                                  lock_shm_name_.c_str(),
                                                  kSeqLockShmSize,
                                                  (void*)addr);

        addr += kSeqLockShmSize + kShmReserveSize;
        app_data_shm_ = new managed_shared_memory(boost::interprocess::open_or_create,
                                                  app_shm_name_.c_str(),
                                                  kAppDataShmSize,
                                                  (void*)addr);
        assert(seq_lock_shm_ && app_data_shm_);    

        if (!is_adv_follower)
        {
            return aaf::ErrorCode::kSuccess;
        }

        addr += kAppDataShmSize + kShmReserveSize;
        trial_seq_lock_shm_ = new managed_shared_memory(boost::interprocess::open_or_create,
                                                        trial_lock_shm_name.c_str(),
                                                        kSeqLockShmSize,
                                                        (void*)addr);

        addr += kSeqLockShmSize + kShmReserveSize;
        trial_app_data_shm_ = new managed_shared_memory(boost::interprocess::open_or_create,
                                                        trial_app_shm_name.c_str(),
                                                        kAppDataShmSize,
                                                        (void*)addr);

        assert(trial_seq_lock_shm_ && trial_app_data_shm_);
        return aaf::ErrorCode::kSuccess;
    }

    void DeatchAll()
    {
        if (seq_lock_shm_)
        {
            delete seq_lock_shm_;
            seq_lock_shm_ = nullptr;
        }

        if (app_data_shm_)
        {
            delete app_data_shm_;
            app_data_shm_ = nullptr;
        }

        if (trial_seq_lock_shm_)
        {
            delete trial_seq_lock_shm_;
            trial_seq_lock_shm_ = nullptr;
        }

        if (trial_app_data_shm_)
        {
            delete trial_app_data_shm_;
            trial_app_data_shm_ = nullptr;
        }
    }

    int32_t TrialAttach()
    {
        std::string trial_app_shm_name  = app_shm_name_ + "_trial";
        std::string trial_lock_shm_name = lock_shm_name_ + "_trial";

        seq_lock_shm_ = new managed_shared_memory(boost::interprocess::open_only,
                                                  trial_lock_shm_name.c_str(),
                                                  (void*)kShmDataAddrBegin);

        uint64_t addr = kShmDataAddrBegin + kSeqLockShmSize + kShmReserveSize;
        app_data_shm_ = new managed_shared_memory(boost::interprocess::open_only,
                                                  trial_app_shm_name.c_str(),
                                                  (void*)addr);

        assert(seq_lock_shm_ && app_data_shm_);

        return aaf::ErrorCode::kSuccess;
    }

    // 实时进程将 app_data 中的内容直接拷贝到 _app_data_trial 上
    void OverWriteTrialShmData()
    {
        assert(seq_lock_shm_ && trial_seq_lock_shm_);
        assert(app_data_shm_ && trial_app_data_shm_);

        uint64_t trial_shm_data_begin = kShmDataAddrBegin + kSeqLockShmSize
            + kShmReserveSize + kAppDataShmSize + kShmReserveSize;

        uint64_t app_data_offset = kSeqLockShmSize + kShmReserveSize;
        std::memcpy((void*)trial_shm_data_begin, (void*)kShmDataAddrBegin, kSeqLockShmSize);

        std::memcpy((void*)(trial_shm_data_begin + app_data_offset),
                    (void*)(kShmDataAddrBegin + app_data_offset),
                    kAppDataShmSize);
    }

    managed_shared_memory* seq_lock_shm()
    {
        return seq_lock_shm_;
    }

    managed_shared_memory* trial_seq_lock_shm()
    {
        return trial_seq_lock_shm_;
    }

    managed_shared_memory* app_data_shm()
    {
        return app_data_shm_;
    }


private:
    std::string app_shm_name_;
    std::string lock_shm_name_;

    // app shm data
    managed_shared_memory* seq_lock_shm_ = nullptr;
    managed_shared_memory* app_data_shm_ = nullptr;
    managed_shared_memory* trial_seq_lock_shm_ = nullptr;
    managed_shared_memory* trial_app_data_shm_ = nullptr;

    std::map<std::string, aaf::ShardingSeqLock*> sharding_lock_map_;
    std::map<std::string, ShmSeqLock*> trial_shm_lock_map_;

};



class ShardingAgent : public aaf::GenericAmiApplication
{
    ADK_LOG_DECLARE_AC(510000);
public:
    enum AgentLAId
    {
        kDoRoute = 0,
        kMessageSort,
        kMaxSize,
    };

    ShardingAgent() : aaf::GenericAmiApplication(true)
    {
        memset(ctx_ind_, 0, sizeof(ctx_ind_));
        ParserEnv();
    }

    ~ShardingAgent()
    {}

    void ParserEnv();

    int32_t InitAgent(aaf::GenericAmiApplication* app_instance,
                      ami::Property& ha_ctx_props,
                      ami::Property& sg_ctx_props);

    int32_t StartProxy();

    int32_t OnAmiInitBegin() override;

    int32_t OnTxEndpointCreationBegin() override;

    int32_t OnRxEndpointCreationBegin() override
    {
        return aaf_instance_->OnRxEndpointCreationBegin();
    }

    int32_t OnTxEndpointCreation(aaf::EndpointHandler* ep_hdl, const std::string& ep_name) override;

    int32_t OnAmiInitEnd() override
    {
        ADK_LOG_INFO_AC_TF("recv callback", "");
        return aaf::ErrorCode::kSuccess;
    }

    void OnAmiExitBegin() override
    {
        ADK_LOG_INFO_AC_TF("recv callback", "");
    }

    template<typename Callable>
    void foreach_sharding_ctx(const Callable& cb)
    {
        for (uint32_t index = 1; index <= sharding_num_; ++index)
        {
            cb(sharding_ctx_vec_[index]);
        }
    }

    void OnMessage(ami::Message* msg) override
    {
        int64_t ts_begin;
        if (is_calc_msg_latency_)
        {
            ts_begin = adk::timespec_now();
        }
        
        int32_t sharding_dst = aaf_instance_->DoRoute(msg, sharding_num_);  // 调用用户自定义分片逻辑
        if (sharding_dst > sharding_num_ || sharding_dst < 0)  // 异常分片
        {
            ++(ctx_ind_[0].nr_route_failed);
            return;
        }

        if (ADK_LIKELY(sharding_dst > 0))  // (0, sharding_num_]区间分片
        {
            if (is_advance_follower_)
            {
                DeliverFlrMessage(msg, sharding_ctx_vec_[sharding_dst], ts_begin);
            }
            else
            {
                DeliverMessage<false>(msg, sharding_ctx_vec_[sharding_dst], ts_begin);
            }
            ++(ctx_ind_[0].nr_message_received);
            return;
        }

        if (sharding_dst == 0)  // 0分片
        {
            for (int sharding_index = 1; sharding_index <= sharding_num_; ++sharding_index)
            {
                if (is_advance_follower_)
                {
                    DeliverFlrMessage(msg, sharding_ctx_vec_[sharding_index], ts_begin);
                }
                else
                {
                    DeliverMessage<false>(msg, sharding_ctx_vec_[sharding_index], ts_begin);
                }
            }
            ++(ctx_ind_[0].nr_message_received);
            return;
        }
    }

    void OnMessageSingleton(ami::Message* msg) override
    {
        int64_t ts_begin;
        if (is_calc_msg_latency_)
        {
            ts_begin = adk::timespec_now();
        }

        // ADK_LOG_ERROR_AC_TF("recv msg", "sqn: {1}", msg->get_total_order_seq_num());
        // singleton 的消息要通过 rx_cont_shm_sgt 递交，proxy起两个线程 独立递交 
        int32_t sharding_dst = aaf_instance_->DoRoute(msg, sharding_num_);  // 调用用户自定义分片逻辑
        if (sharding_dst > sharding_num_ || sharding_dst < 0)  // 异常分片
        {
            ++(ctx_ind_[1].nr_route_failed);
            return;
        }

        if (ADK_LIKELY(sharding_dst > 0))  // (0, sharding_num_]区间分片
        {
            DeliverMessage<true>(msg, sharding_ctx_vec_[sharding_dst], ts_begin);
            ++(ctx_ind_[1].nr_message_received);
            return;
        }

        if (sharding_dst == 0)  // 0分片
        {
            for (int sharding_index = 1; sharding_index <= sharding_num_; ++sharding_index)
            {
                DeliverMessage<true>(msg, sharding_ctx_vec_[sharding_index], ts_begin);
            }
            ++(ctx_ind_[1].nr_message_received);
            return;
        }
    }

    int32_t OnRun() override;

    void OnConfigureContextProperty(const std::string& context_name, 
                                    bool is_ha_ctx,
                                    ami::Property& props)
    {

    }

    void OnRoleChangeToLeader() override
    {
        PushEvent(ShmEventType::kOnRoleChangeToLeader);
    }

    void OnRoleChangeToMember() override
    {
        PushEvent(ShmEventType::kOnRoleChangeToMember);
    }

    void OnRoleChangeToMaster() override
    {
        PushEvent(ShmEventType::kOnRoleChangeToMaster);
    }

    void OnRecoveryBegin() override
    {
        PushEvent(ShmEventType::kOnRecoveryBegin);
    }

    void OnRecoverySuccess() override
    {
        PushEvent(ShmEventType::kOnRecoverySuccess);
        WaitConsumeComplete<false>();  //  保证应用处理完recovery success事件后加组
    }

    void OnDiscardMessage(std::string& msg) override
    {
        PushEvent(ShmEventType::kOnDiscardMessage, msg);
    }

    void OnNoReceiver(const std::string& endpoint_name, const std::string& partition) override
    {
        ami::Property props;
        props.SetValue("endpoint_name", endpoint_name);
        props.SetValue("partition", partition);
        PushEvent(ShmEventType::kOnNoReceiver, props.Dump());
    }

    void OnReceiverUp(const std::string& endpoint_name, const std::string& partition) override
    {
        ami::Property props;
        props.SetValue("endpoint_name", endpoint_name);
        props.SetValue("partition", partition);
        PushEvent(ShmEventType::kOnReceiverUp, props.Dump());
    }

    void OnDiscardMessageTotalOrderSqn(uint64_t discard_msg_sqn) override
    {
        for (auto iter : sharding_lock_map_)
        {
            iter.second->shm_seq_lock_->UnLock(static_cast<int64_t>(discard_msg_sqn),
                                               sharding_index_);
        }
    }

    void EventConvert(ShmEventType type, const std::string& prop_str, struct ShmAmiEvent* event);

    void PushEvent(ShmEventType type, const std::string& prop_str = std::string());

    int32_t ShardingPost(const void* data, uint32_t size, int32_t sharding_index);

    aaf::ShardingSeqLock* CreateSeqLock(const std::string& name, uint32_t cache_size);

    void ShmDataFlush();

    void OnExit() override
    {
        ADK_LOG_INFO_AC_TF("sharding agent exit", "");
    }

    void OnSignal(int sig_num, int value);

    // proxy子进程中可以调用此接口
    int32_t sharding_index() const
    {
        return sharding_index_;
    }

    aaf::GenericAmiApplication* aaf_instance()
    {
        return aaf_instance_;
    }

    ShardingProxy* GetProxy() { return proxy_; }

    template <bool IsSingleton>
    bool WaitConsumeComplete()
    {
        while (is_running())
        {
            bool is_complete = true;
            for (int32_t index = 1; index <= sharding_num_; ++index)
            {
                auto& ctx_data = sharding_ctx_vec_[index]->GetCtxData(IsSingleton);
                auto nr_consume = ctx_data.rx_cont_shm->GetConsumeNR();
                auto nr_produce = ctx_data.rx_cont_shm->GetProduceNR();
                if (nr_produce != nr_consume)
                {
                    is_complete = false;
                    break;
                }
            }

            if (is_complete)
            {
                break;
            }
            usleep(0);
        }
        return is_running();
    }

private:
    void ProxyLogAgent();

    int32_t CreateShmContMem(const std::string& share_memory_name);

    int32_t AllocShmContMem(ShardingCtx* sharding_ctx);

    int32_t StartShardingProxy(ShardingCtx* sharding_ctx);

    int32_t GetMasterEndpointList(ami::Context* ami_context, bool is_ha, std::vector<ami::Property>& ep_infos);

    bool OnCollectIndicator(bool is_singleton, boost::property_tree::ptree& indicator);

    void MessageConvert(const ami::Message* ami_msg, struct ShmRxMessage* shm_rxmsg)
    {
        shm_rxmsg->header.msg_type = ShmMsgType::kAmiRxMsg;
        shm_rxmsg->header.msg_len = sizeof(struct ShmRxMessage) + ami_msg->size();
        shm_rxmsg->endpoint_id = ami_msg->get_endpoint_id();
        shm_rxmsg->partition_no = ami_msg->get_partition_no();
        shm_rxmsg->transport_id = ami_msg->get_transport_id();
        shm_rxmsg->msg_tag = ami_msg->get_tag();
        shm_rxmsg->trace_record = ami_msg->get_trace_record().data;
        shm_rxmsg->total_order_sqn = ami_msg->get_total_order_seq_num();
        shm_rxmsg->topic_sqn = ami_msg->topic_seq_num();
        shm_rxmsg->cont_topic_sqn = ami_msg->get_cont_topic_seq_num();
        shm_rxmsg->cont_endpoint_sqn = ami_msg->get_cont_endpoint_seq_num();
        char* const msg_body = shm_rxmsg->msg_body;
        memcpy(msg_body, ami_msg->const_data(), ami_msg->size());
    }

    template <bool IsSingleton>
    void DeliverMessage(ami::Message* msg, ShardingCtx* sharding_ctx, int64_t ts_begin);

    // follower agent push msg to realtime process and trial process
    void DeliverFlrMessage(ami::Message* msg, ShardingCtx* sharding_ctx, int64_t ts_begin);

    void OnShmTxMessage(ami::Context* context, struct ShmTxMessage* tx_message);

    void ProcessTxMsg(ShardingCtx* sharding_ctx);

    template <bool IsSingleton>
    void RecvActor();

    void SortRecvActor();

    int32_t PipeWrite(const char* buff, uint32_t length);

    void CloseOtherPipes(int32_t cur_sharding_idx)
    {
        for (int sharding_index = 1; sharding_index <= sharding_num_; ++sharding_index)
        {
            if (sharding_index == cur_sharding_idx)
            {
                continue;   // or break
            }
            
            auto* sharding_ctx = sharding_ctx_vec_[sharding_index];
            if (sharding_ctx)
            {
                sharding_ctx->ClosePipe();
            }
        }
    }

private:
    struct CtxInd
    {
        uint64_t nr_message_received = 0;
        uint64_t nr_route_failed = 0;
        uint64_t nr_tx_message_ = 0;
    };
    CtxInd ctx_ind_[2];  // 0为高可用, 1为单例;
    uint64_t event_nr_ = 0;

    struct timespec     last_time_;

    boost::detail::spinlock rx_spinlock_ = BOOST_DETAIL_SPINLOCK_INIT;  // 高可用消息和事件锁
    boost::detail::spinlock rx_sgt_spinlock_ = BOOST_DETAIL_SPINLOCK_INIT;  // 单例锁，目前只存在消息递交，暂时不使用

    aaf::GenericAmiApplication* aaf_instance_ = nullptr;

    bool is_low_latency_ = true;
    const uint64_t idle_loop_count_limit_ = 32;
    const uint64_t idle_delay_microsec_ = 100;

    bool is_advance_follower_ = false;
    ami::Context* ami_context_ = nullptr;
    ami::Context* ami_context_sgt_ = nullptr;
    uint64_t nr_tx_msgs_ = 0;
    uint64_t last_process_msg_done_ = 0;
    ReorderBuffer  reorder_buff_;

    adk::variant::SPSCQueue<SortElem>* sort_queue_;
    uint64_t sort_queue_max_qlen_ = 0;

    std::vector<aaf::EndpointHandler*> txeps_vec_;

    std::string  share_memory_name_;
    std::string  sccl_name_;
    std::string  host_name_;
    std::string  context_name_;
    std::string  domain_server_;

    // app shm data
    ShmDataMgr* shm_data_manager_ = nullptr;
    uint64_t shm_data_size_ = 512ul * 1024ul * 1024ul;

    std::map<std::string, aaf::ShardingSeqLock*> sharding_lock_map_;

    adk::ShmContMemManager* memory_manager_ = nullptr;
    //[0] 不使用，以便更好对齐到业务层
    ShardingCtx* sharding_ctx_vec_[kMaxShardingNum + 1] = {nullptr};

    int32_t sharding_num_ = 0;
    int32_t sharding_index_ = 0;    // 0: agent, > 0: proxy
    pid_t sharding_pids_[kMaxShardingNum] = {0};

    boost::thread recv_actor_thd_;
    boost::thread recv_actor_thd_sg_;

    ami::Property ha_ctx_props_;
    ami::Property sg_ctx_props_;
    ami::Property ep_info_props_;
    std::unordered_map<std::string, uint32_t> tx_ep_map_;
    ShardingProxy* proxy_ = nullptr;

    std::string core_file_path_;
    std::fstream core_sqn_file_;

    void (*stop_save_point_)() = nullptr;

    bool is_calc_msg_latency_ = false;
    LatencyMetric ha_la_metric_;
    LatencyMetric sg_la_metric_;

    adk::LatencyStatistics* sg_latency_[AgentLAId::kMaxSize];
    adk::LatencyStatistics* ha_latency_[AgentLAId::kMaxSize];
    uint64_t latency_size_ = 4ul * 1024ul * 1024ul;

    adk::sccl::AgentEventHandler* sccl_handle_ = nullptr;
    adk::sccl::Agent* sccl_agent_ = nullptr;
    boost::thread proxy_log_thread_;

    const std::string log_date_format_ = "%Y-%m-%d";

    const std::string log_file_format_ = "%1%/log_%2%_%3%.log";
    std::string log_file_name_;

    const std::string log_file_format_tri_ = "%1%/log_%2%_trial_%3%.log";
    std::string log_file_name_tri_;
    std::string log_file_name_tri_current_;
    std::fstream log_file_tri_;

    int64_t log_rotate_check_time_ = 0;
    std::string current_date_;

    char* log_name_shm_ = nullptr;
    char* log_name_tri_shm_ = nullptr;
    const uint64_t kLogNameLenMax = 512;

    adk::EventChannel* event_channel_ = nullptr;

    friend ShardingProxy;
};


}   // end of namespace sharding
