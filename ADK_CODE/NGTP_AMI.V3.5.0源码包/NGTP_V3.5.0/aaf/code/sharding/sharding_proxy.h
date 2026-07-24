#ifndef SHARDING_PROXY_H_
#define SHARDING_PROXY_H_

#include <ami/message.h>
#include <ami/context.h>

#include <aaf.h>

#include <adk/util.h>
#include <adk/arch/generic.h>
#include <adk/lock_free_cont_memory.h>
#include <adk/monitor/monitor.h>
#include <adk/monitor/indicator_writer.h>
#include "protocol.h"
#include "util.h"

#include "constant.h"
#include "ami_message.h"  // for AmiMessage AMI 内部消息头文件
#include <adk/log.h>
#include <adk/util.h>
#include <adk/logger.h>
#include "sharding_agent.h"
#include "sharding_common.h"

namespace sharding
{

class FollowerProxy;
class ShardingProxy;
class ShmDataMgr;

struct FlrProxyLaunchShm
{
    uint64_t nr_msg_deliverd_sqn;   // 最后递交的消息序号
    uint64_t nr_msg_processed_sqn;  // 处理完成的消息序号
    
    uint64_t nr_shm_processed_sqn;  // 所有从共享内存上消费的序号  
                                    // 包含消息和事件，实时进程通过此序号进行跟进处理

    bool is_normal_exit;    // 标记试算节点主动退出，例如发现agent管道破裂
    bool is_on_event;       // 标记是否处于递交事件过程中
    bool is_exception;      // 试算进程收到异常信号
    int32_t sharding_index = -1;

    void Init(int32_t index)
    {
        nr_msg_deliverd_sqn  = 0;
        nr_msg_processed_sqn = 0;
        nr_shm_processed_sqn = 0;
        is_on_event          = false;
        is_exception         = false;
        sharding_index = index;
    }
};

const uint32_t kFlrLauncheSize = ADK_ROUND_UP(sizeof(FlrProxyLaunchShm), ADK_PAGE_SIZE);

class AppMessageHandler : public ami::MessageHandler
{
public:
    AppMessageHandler(bool is_sg = false) 
        : is_rx_stop_(false),
          is_singleton_(is_sg)
    {}

    ~AppMessageHandler() = default;

    void StopRxMessageDeliver()
    {
        is_rx_stop_ = true;
    }

    void OnMessage(ami::Message* msg) override
    {
        if (is_singleton_)
        {
            application_instance_->OnMessageSingleton(msg);
        }
        else
        {
            application_instance_->OnMessage(msg);
        }
    }

protected:
    volatile bool          is_rx_stop_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    aaf::GenericAmiApplication* application_instance_;
    bool is_singleton_ = false;
    friend class FollowerProxy;
    friend class ShardingProxy;
};

class DummyPropertyPath : public ami::PropertyPath
{
public:
    virtual int32_t get(ami::Property*& props)
    {
        return ami::ErrorCode::kSuccess;
    }

    virtual PropertyPath& operator() (const std::string& path_key, const std::string& path_value = "")
    {
        return *this;
    }
};

class DummyContextImpl : public ami::Context
{
public:
    bool IsEnableFollowerContext() override
    {
        return true;
    }

    int32_t DeleteMessage(ami::Message* msg) override
    {
        // clear forward_acquire flags
        msg->msg_flag &= ~(AMI_RESERVE_MESSAGE|AMI_FORWARD_MESSAGE);

        return ami::ErrorCode::kSuccess;
    }

    virtual ami::PropertyPath& PropertyAt(const std::string& path_key, const std::string& path_value = "")
    {
        static DummyPropertyPath* path = new DummyPropertyPath();
        return *path;
    }

    virtual ami::AmiRecordAgent* GetRecordAgent() const
    {
        return nullptr;
    }

    virtual int32_t CommitSync(ami::Message::SqnType sqn)
    {
        return ami::ErrorCode::kSuccess;
    }

    virtual ami::TierChannel* CreateTierChannel(const std::string& tier_channel_name, 
                                                ami::TierChannelHandler& tier_channel_hander, 
                                                int32_t init_mode)
    {
        return nullptr;
    }

    virtual int32_t CommitSyncBefore(ami::Message::SqnType sqn)
    {
        return ami::ErrorCode::kSuccess;
    }

    virtual int32_t CommitSyncBefore(ami::Message::IDType transport_id, 
                                     ami::Message::SqnType topic_sqn, 
                                     ami::Message::SqnType ctx_sqn)
    {
        return ami::ErrorCode::kSuccess;
    }
};

class ShardingTxHandler : public aaf::EndpointHandler
{
public:
    ShardingTxHandler(ShardingProxy* proxy, uint32_t endpoint_id, bool is_singleton)
        : shd_proxy_(proxy),
          endpoint_id_(endpoint_id),
          is_singleton_(is_singleton)
    {
        nr_tx_msgs_ = 0;
        default_record_.data = 0;
    }

    int32_t SendMsg(const void* data, uint32_t len) override
    {
        return SendMsg(data, len, 0, default_record_);
    }

    int32_t SendMsg(const void* data, uint32_t len, int32_t partition_no) override
    {
        return SendMsg(data, len, partition_no, default_record_);
    }

    int32_t SendMsg(const void* data, uint32_t len, ami::TraceRecord record) override
    {
        return SendMsg(data, len, 0, record);
    }

    int32_t SendMsg(const void* data, uint32_t len, int32_t partition_no, ami::TraceRecord record) override;

private:
    ShardingProxy* shd_proxy_;
    uint32_t endpoint_id_;
    uint64_t nr_tx_msgs_;
    bool is_singleton_ = false;
    ami::TraceRecord default_record_;
};


struct ShardingTxEndpointInfo
{
    uint32_t endpoint_id = 0;
    bool is_singleton = false;
    std::vector<int32_t> partitions;
    ShardingTxHandler* shd_tx_ep = nullptr;
};

class ShardingProxy
{
    ADK_LOG_DECLARE_AC(130000);
public:
    class AppLogger : public adk::log::Logger, public adk::IMonitorSinker
    {
    public:
        AppLogger(ShardingProxy* proxy)
        : sharding_proxy_(proxy)
        {}
        ~AppLogger(){}

    private:

        void Receive(adk::IMonitorSinker::Type type,
                            uint64_t query_key,
                            const boost::property_tree::ptree& msg_tree) override;

        void Log(adk::log::LogLevel level,
                 adk::log::LogCode code,
                 const std::string& module_name,
                 const std::string& function_name,
                 uint32_t src_line,
                 const std::string& title,
                 const std::string& message) override;

        ShardingProxy* sharding_proxy_ = nullptr;
        std::ostringstream oss_;

        boost::mutex log_mutex_;
        char* log_name_ = nullptr;
        std::string log_name_current_;
        int log_file_ = -1;

        friend class ShardingProxy;
    };

    enum ProxyLAId
    {
        kWake = 0,
        kWaitTrial,
        kOnMessage,
        kMaxSize,
    };

    ShardingProxy()
    {
        default_msg_handler_ = new AppMessageHandler(false);
        default_msg_handler_sgt_ = new AppMessageHandler(true);
    }

    virtual ~ShardingProxy(){}

    // for application
    int32_t GetTxEndpointPartitions(const std::string& ep_name, std::vector<int32_t>& partitions)
    {
        const auto iter = tx_endpoint_info_map_.find(ep_name);
        if (iter != tx_endpoint_info_map_.end())
        {
            partitions = iter->second->partitions;
            return aaf::ErrorCode::kSuccess;
        }

        return aaf::ErrorCode::kFailure;
    }

    int32_t Init(ShardingAgent* sharding_agent, int32_t sharding_index);

    template <bool IsSingleton>
    int32_t SendMsg(const void* buf,
                    uint32_t len,
                    int32_t partition_no,
                    uint32_t endpoint_id,
                    ami::TraceRecord record);

    int32_t ProcessMessageDone(ShardingCtx::CtxData* ctx_data,
                               uint64_t total_order_sqn);

    int32_t DiscardMessage(uint64_t total_order_sqn);

    int32_t DeliverMessage(ShmRxMessage* shm_rx_msg, ami::AmiMessage* ami_msg)
    {
        ami::MessageHandler* msg_handler = default_msg_handler_;
        uint32_t endpoint_id             = shm_rx_msg->endpoint_id;

        auto iter = rx_endpoint_info_map_.find(endpoint_id);
        if (iter != rx_endpoint_info_map_.end())
        {
            msg_handler = iter->second->msg_handler;
        }

        TransformMessage(shm_rx_msg, ami_msg);

        msg_handler->OnMessage(ami_msg->message());

        return aaf::ErrorCode::kSuccess;
    }

    inline void TransformMessage(ShmRxMessage* shm_rx_msg, ami::AmiMessage* ami_msg)
    {
        ami::Message* msg = ami_msg->message();
        // 只维护目前使用到的接口
        ami_msg->ami_meta_data.ami_recv_sqn = shm_rx_msg->total_order_sqn;
        ami_msg->ami_meta_data.endpoint_id = shm_rx_msg->endpoint_id;
        ami_msg->ami_meta_data.transport_id = shm_rx_msg->transport_id;
        ami_msg->ami_meta_data.c_topic_sqn = shm_rx_msg->cont_topic_sqn;
        ami_msg->ami_meta_data.c_endpoint_sqn = shm_rx_msg->cont_endpoint_sqn;
        // FIXME: new Transport和Endpoinr对象，进行赋值，实现接口功能

        msg->msg_header.ancestor_id = shm_rx_msg->trace_record;
        msg->topic_sqn = shm_rx_msg->topic_sqn;
        msg->app_data = shm_rx_msg->msg_body;
        msg->app_data_len = shm_rx_msg->header.msg_len - sizeof(ShmRxMessage);  // 总长 - 头部

    }

    int32_t ShardingRequest(const void* data, uint32_t size);

    int32_t DeliveEvent(ShmAmiEvent* ami_event);

    int32_t OnRun();

    int32_t InitFramework();

    void ParserEnv();

    bool IsAgentAlive()
    {
        if (!is_running_)
        {
            return false;
        }

        char cmd;
        if (!IsPipeAlive(keep_alive_fd_, cmd))
        {
            if (cmd == kComStop)
            {
                ADK_LOG_INFO_AC_TF("recv agent exit signal",
                                   "sharding index <{1}>",
                                   sharding_index_);
                is_running_ = false;
                return false;
            }

            ADK_LOG_INFO_AC_TF("read pipe from agent error",
                                "errno <{1}>, desc <{2}>, sharding index <{3}>",
                                errno,
                                strerror(errno),
                                aaf_instance_->GetShardingIndex());
            return false;
        }
        return true;
    }

    int32_t AttachShmContMem();

    void PollingMessage(bool is_sg);

    bool OnCollectIndicator(boost::property_tree::ptree& indicator);

    ShardingCtx* sharding_ctx() { return sharding_ctx_; }


    /*******************  Advance Follower Function *******************/

    bool WaitTrialProcess(uint64_t nr_processd_sqn);

    bool IsTrialAlive();

    bool CheckAndFixTrial(uint64_t total_order_sqn);
    
    int32_t OnTrialError();
    
    void HandExceptionSignal(int sig_num);

    int32_t StartTrialProcess();

    void OnSignal(int sig_num, int value);
private:
    bool is_running_ = true;
    ami::AmiMessage* ami_msg_ = nullptr;
    ami::AmiMessage* ami_msg_sg_ = nullptr;

    uint64_t nr_event_cnt_ = 0;

    aaf::GenericAmiApplication* aaf_instance_ = nullptr;  // agent的instance

    std::unordered_map<uint32_t, RxEndpointInfo*>  rx_endpoint_info_map_;
    AppMessageHandler* default_msg_handler_ = nullptr;
    AppMessageHandler* default_msg_handler_sgt_ = nullptr;

    int32_t keep_alive_fd_   = 0;          // realtime <-- trial 

    std::string  share_memory_name_;
    std::string  context_name_;
    std::string  domain_server_;

    int32_t sharding_index_ = -1;
    ShardingAgent* sharding_agent_;
    ShardingCtx* sharding_ctx_ = nullptr;

    ShmDataMgr* shm_data_manager_ = nullptr;
    std::vector<ShmSeqLock*> shm_seq_lock_vec_;

    bool is_advance_follower_ = false;
    bool is_trial_ = false;  // true: 试算进程;  false: 实时进程 或者未启用加强跟跑
    FlrProxyLaunchShm* flr_launch_ = nullptr;
    uint64_t last_discard_msg_sqn_ = 0;
    int32_t trial_fds_[2] = {-1, -1};  // realtime <-- trial
    int32_t nr_start_trial_ = 0;
    pid_t   trial_pid_ = 0;


    // Tx configure
    ami::Property ep_info_props_;
    std::map<std::string, ShardingTxEndpointInfo*> tx_endpoint_info_map_;

    boost::thread rx_threads_[2];  // 0为高可用, 1为单例;

    bool is_polling_ = false;

    LatencyMetric ha_la_metric_;
    LatencyMetric sg_la_metric_;

    adk::LatencyStatistics* sg_latency_[ProxyLAId::kMaxSize];
    adk::LatencyStatistics* ha_latency_[ProxyLAId::kMaxSize];

    std::string sccl_proxy_name_;
    std::string sccl_proxy_name_tri_;
    adk::sccl::Proxy* sccl_proxy_ = nullptr;
    adk::sccl::Proxy* sccl_proxy_tri_ = nullptr;
    AppLogger* proxy_logger_ = nullptr;
    std::string log_file_name_;

    // tx rx transport info
    friend class ShardingTxHandler;
};

}   // end of namespace sharding


#endif  // SHARDING_PROXY_H_