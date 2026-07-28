#pragma once

#include <unistd.h>
#include <adk/util.h>
#include <adk/arch/generic.h>
#include <adk/lock_free_cont_memory.h>
#include <boost/property_tree/ptree.hpp>

#include <ami/handler.h>
#include "constant.h"


namespace sharding
{


struct ShardingCtx
{
    struct CtxData
    {
        int* futex_wait = nullptr;
        adk::ContinueMemory* rx_cont_shm = nullptr;
        uint64_t nr_shm_processed_sqn = 0;   // 表示已经处理的消息数（包含事件）
                                            // 试算进程异常时，由于跳过异常消息，此序号也会增加
        uint64_t nr_msg_delivered = 0;      // proxy->AppOnMessage
        uint64_t nr_msg_processed = 0;      // AppOnMessage return
        uint64_t last_deliver_msg_sqn = 0;  // 记录当前递交的消息序号，在AppOnMessage之前设置
                                            // 该序号为ami消息的 total_order_sqn，在多分片场景下不连续

        ///> TX  
        boost::detail::spinlock tx_spinlock = BOOST_DETAIL_SPINLOCK_INIT;
        adk::ContinueMemory* tx_cont_shm = nullptr;
        uint64_t nr_tx_msgs = 0;  // proxy->tx_shm
    };

    int32_t sharding_index = kInvalidShardingIndex;

    int32_t proxy_to_agent_fds[2] = {-1, -1};  // sharding_agent <-- Sharding proxy. 0 read, 1 write
    int32_t agent_to_proxy_fds[2] = {-1, -1};  // sharding_agent --> Sharding proxy. 0 read, 1 write
    pid_t sharding_pid_ = -1;

    void ClosePipe()
    {
        if (proxy_to_agent_fds[0] > 0)
        {
            close(proxy_to_agent_fds[0]);
        }
        if (proxy_to_agent_fds[1] > 0)
        {
            close(proxy_to_agent_fds[1]);
        }
        if (agent_to_proxy_fds[0] > 0)
        {
            close(agent_to_proxy_fds[0]);
        }
        if (agent_to_proxy_fds[1] > 0)
        {
            close(agent_to_proxy_fds[1]);
        }
    }

    struct CtxData ha_ctx_data __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    ADK_EMPTY_CACHE_LINE;

    // singleton context
    // 跟跑试算的资源复用此变量进行保存
    // fork后 试算进程中会用 sgt_ctx_data 覆盖 ha_ctx_data，实现统一的处理逻辑
    struct CtxData sgt_ctx_data __attribute__((aligned(ADK_CACHE_LINE_SIZE)));

    adk::ContinueMemory* trial_rx_cont_shm()
    {
        return sgt_ctx_data.rx_cont_shm;
    }

    inline CtxData& GetTrialCtxData()
    {
        // call on realtime process
        return sgt_ctx_data;
    }

    inline CtxData& GetCtxData(bool is_singleton)
    {
        if (is_singleton)
        {
            return sgt_ctx_data;
        }
        else
        {

            return ha_ctx_data;
        }
    }

    // 跟跑fork后，试算节点处理逻辑
    void TrialFlush()
    {
        ha_ctx_data.futex_wait = sgt_ctx_data.futex_wait;
        ha_ctx_data.rx_cont_shm = sgt_ctx_data.rx_cont_shm;
        ha_ctx_data.tx_cont_shm = sgt_ctx_data.tx_cont_shm;
    }

};

struct RxEndpointInfo
{
    uint32_t    endpoint_id = 0;
    ami::MessageHandler*    msg_handler = nullptr;
    std::string endpoint_name;
    bool is_singleton = false;
};

using boost_ptree = boost::property_tree::ptree;
class LatencyMetric
{
public:
    struct MetricElem
    {
        std::string name;
        adk::LatencyStatistics* latency;
    };

    adk::LatencyStatistics* AddLatencySpan(const std::string& name, uint64_t capacity)
    {
        adk::LatencyStatistics* latency = new adk::LatencyStatistics(capacity);
        
        MetricElem metric;
        metric.name = name;
        metric.latency = latency;

        metric_vec_.emplace_back(metric);

        return latency;
    }

    void CollectIndicator(boost_ptree& indicator, const std::string& name)
    {
        if (!metric_vec_.size())
        {
            return;
        }

        boost_ptree& latency_tree = indicator.add_child(name, boost_ptree());

        for (auto& metric : metric_vec_)
        {
            auto& ind = latency_tree.push_back(boost_ptree::value_type("", boost_ptree()))->second;
            ind.put("metric_name", metric.name);
            if (metric.latency->Calculate())
            {
                ind.put("avg", metric.latency->GetAvg());
                ind.put("max", metric.latency->GetMax());
                ind.put("min", metric.latency->GetMin());
                ind.put("p90", metric.latency->GetPercentNumber(0.9));
                ind.put("count", metric.latency->GetNumberRecords());
            }
            else
            {
                ind.put("count", 0);
            }
        }
    }

    std::vector<MetricElem> metric_vec_;
};

}   // end of namespace sharding
