/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */

///< std
#include <thread>

///< boost
#include <boost/filesystem.hpp>

///< adk, ami public
#include <adk/monitor/monitor.h>
#include <adk/singleton.h>
#include <adk/lock_free_msg_queue.h>
#include <adk/mem_pool.h>
#include <adk/lock_free_unbounded_queue_variant.h>

///< ami impl
#include "../ami_message.h"
#include "../log.h"

///< impl
#include <ami/message.h>
#include "../context_impl.h"
#include "../coordinator.h"
#include "../ami_constant.h"
#include "../property_default_value.h"
#include "async_record_client.h"
#include "config_default_value.h"
#include "control_message_key.h"
#include "record_agent.h"
#include "record_channel.h"

#define AMI_CHECHK_MP_CREATION(pool_ptr, context_name, pool_name, block_size, nr_blocks) do {   \
    if (pool_ptr == NULL)   \
    {   \
        LOG_ERROR((boost::format("context <%1%> memory pool <%2%> creation failed, please check share memory limitation and permission, "    \
                                 "otherwise, try to remove files </dev/shm/*%3%*>") \
                  % context_name % pool_name % context_name).str());   \
        return NULL;    \
    }   \
    else    \
    {   \
        LOG_INFO((boost::format("context <%1%> memory pool <%2%> is created successfully, total memory blocks <%3%>, block size <%4%>") \
                 % context_name % pool_name % (nr_blocks) % (block_size)).str());   \
    }   \
} while (false)

namespace ami
{

namespace cc   = config::context;
namespace ccr  = config::context::recorder;
namespace rcdv = recorder::cdv;
namespace bf   = boost::filesystem;
namespace bt   = boost::property_tree;

class MsgCollector
{
public:
    MsgCollector(adk::MPManager *mp_manager) : mp_manager_(mp_manager)
    {
    }

    MsgCollector& operator=(const MsgCollector &log_processor) = delete;

    MsgCollector(MsgCollector &&log_processor) = delete;

    MsgCollector& operator=(MsgCollector &&log_processor) = delete;    

    ~MsgCollector()
    {
        is_running_ = false;
        if (collector_thread_.joinable())
        {
            collector_thread_.join();
        }

        if (msg_queue_)
        {
            adk::variant::SPSCUnboundedQueue<AmiMessage*>::Delete(msg_queue_);
        }
    }

    ErrorCode_def PushMsg(AmiMessage *msg)
    {
        return msg_queue_->Push(msg);
    }

    ErrorCode_def Start()
    {
        msg_queue_ = adk::variant::SPSCUnboundedQueue<AmiMessage*>::Create("AMIMsgQueue");
        if (msg_queue_ == nullptr)
        {
            LOG_ERROR("recorder agent not enough memory, garbage collector create gc channel failed");
            return ErrorCode::kNoMemory;
        }

        collector_thread_ = adk::std_thread("recorder msg collector", "recorder msg collector thread", std::bind(&MsgCollector::Run, this));
        return ErrorCode::kSuccess;
    }

    uint64_t collected_msg_count = 0;
private:
    void Run()
    {
        is_running_ = true;
        AmiMessage *ami_msg;
        while (is_running_)
        {
            while (msg_queue_->length() > 0)
            {
                if (ADK_UNLIKELY(msg_queue_->Pop(ami_msg) != 0))
                {
                    break;
                }
                
                auto app_msg = ami_msg->message();
                if ((0 == app_msg->ref_cnt) && (0 == app_msg->ref_cnt2) && (0 == app_msg->insync_flag))
                {
                    MemoryBuffer* mem_buf = ADK_CONTAINER_OF(ami_msg, MemoryBuffer, data);
                    MessagePool::DeleteMessage(mp_manager_->GetMPByMemBuf(mem_buf), ami_msg, nullptr);
                    ++collected_msg_count;
                }
                else
                {
                    msg_list_.push_back(ami_msg);
                }
            }

            auto iter = msg_list_.begin();
            while (iter != msg_list_.end())
            {
                auto app_msg = (*iter)->message();
                if (app_msg->ref_cnt == 0 && app_msg->ref_cnt2 == 0)
                {
                    MemoryBuffer* mem_buf = ADK_CONTAINER_OF(*iter, MemoryBuffer, data);
                    MessagePool::DeleteMessage(mp_manager_->GetMPByMemBuf(mem_buf), *iter, nullptr);
                    iter = msg_list_.erase(iter);
                    ++collected_msg_count;
                    continue;
                }
                ++iter;
            }

            usleep(0);
        }
    }

    std::thread collector_thread_;
    adk::MPManager* mp_manager_ = nullptr;
    adk::variant::SPSCUnboundedQueue<AmiMessage*> *msg_queue_ = nullptr;
    std::list<AmiMessage*> msg_list_;
    bool is_running_ = false;
    LOG_DECLARE
};

LOG_DEFINE(MsgCollector)

LOG_DEFINE(RecordAgent)

constexpr Message::SqnType RecordAgent::kBegin;
constexpr Message::SqnType RecordAgent::kMostRecent;
constexpr size_t RecordAgent::kRealtimeChannel;
constexpr size_t RecordAgent::kRetransmitChannel;
constexpr size_t RecordAgent::kDefMsgQueueSize;

RecordAgent::RecordAgent()
    : record_client_(new AsyncRecordClient)
{
}

RecordAgent::RecordAgent(ContextImpl* ctx)
    : record_client_(new AsyncRecordClient),
      ctx_(ctx)
{
}

RecordAgent::~RecordAgent()
{
    ReleasePushThread();
    // FIXME: can not monitor recorder agent. tag: (1)
    // adk::Monitor::UnregisterObject(kRecordAgent, ctx_name_);

    LOG_INFO("\n"
             "*leaving*************"
             "\n"
             "{1}\n"
             "*********************",
             *this);

    // record_chnl_vec_.clear();

    if (ctx_)
    {
        ctx_ = nullptr;
    }

    delete record_client_;
    if (tx_msg_pool_ != nullptr)
    {
        delete tx_msg_pool_;
        delete mp_manager_;
        delete msg_collector_;
    }
}

bool RecordAgent::Init(const RecorderId& recorder_id, const Property& props,
                       const OnRcdNotExistType& on_recorder_ne)
{
    ++ref_counter_;
    ctx_name_ = props.GetValue(cc::kName, std::string());
    if (ctx_name_.empty() && ctx_)
    {
        ctx_name_ = ctx_->context_name();
    }

    if (ctx_name_.empty())
    {
        LOG_ERROR("Init failed: need 'ContextName' in props");
        return false;
    }

    recorder_id_ = recorder_id;
    if (recorder_id_.IsVoid())
    {
        LOG_ERROR("empty recorder_id");
        return false;
    }

    if (props.HasValue(ccr::kMsgQueueSize))
    {
        msg_queue_size_ = props.GetValue(ccr::kMsgQueueSize, decltype(msg_queue_size_)::value_type());
    }

    if (props.HasValue(ccr::kUseMsgCRC))
    {
        use_msg_crc_ = props.GetValue(
            ccr::kUseMsgCRC, decltype(use_msg_crc_)::value_type());
    }

    if (props.HasValue(ccr::kIgnoreLostMsg))
    {
        ignore_lost_msg_ = props.GetValue(
            ccr::kIgnoreLostMsg,
            decltype(ignore_lost_msg_)::value_type());
    }

    record_not_exist_cb_ = on_recorder_ne;
    if (kSuccess != record_client_->Init(recorder_id_, props, std::bind(&RecordAgent::OnRecordNotExist, this)))
    {
        return false;
    }

    if (kSuccess != record_client_->Start(data_path_, ctx_name_, props.GetValue(kIsAgentRecovery, false)))
    {
        return false;
    }

    if (props.HasValue(ccr::kReportStatus))
    {
        report_status_ = props.GetBoolValue(ccr::kReportStatus);
    }

    if (report_status_)
    {
        // meaningless, tag : (1)
        // adk::MonitorOps monitor_ops;
        // monitor_ops.is_collection_indicator = true;
        // monitor_ops.on_collection_indicator =
        //     boost::bind(&RecordAgent::OnCollectIndicator, this, _1);
        // adk::Monitor::RegisterObject(kRecordAgent, ctx_name_, &monitor_ops);
    }

    if (props.GetValue("IsUseMsgPool", false))
    {
        const uint32_t mem_pool_block_size = props.GetValue(config::context::kMemPoolBlockSizeBytes, 2048);
        const uint32_t tx_mem_pool_block_size = props.GetValue(config::context::kTxMemPoolBlockSizeBytes, mem_pool_block_size);
        const uint32_t emergent_buffer_ratio = props.GetValue(config::context::kEmergentBufferRatio, config::default_value::kEmergentBufferRatio);
        const uint32_t tx_message_pool_size = std::max<uint32_t>(props.GetValue(config::context::kTxMessagePoolSize, 8192), AMI_MAX_GC_BATCH_MSGS);
        const uint32_t num_emergent_messages = std::max<uint32_t>((tx_message_pool_size * emergent_buffer_ratio) / 100u, AMI_MAX_GC_BATCH_MSGS);

        const std::vector<std::string> shm_name_vec = {MakeMPTableName(GetContextName()), 
                                                       MakeTxMemPoolName(GetContextName()), 
                                                       MakeTxMemPoolNameMax(GetContextName())};

        for (auto &name : shm_name_vec)
        {
            unlink((boost::format("/dev/shm/%1%") % name).str().c_str());
        }

        mp_manager_ = new MPManager();
        mp_manager_->CreateMPTable(MakeMPTableName(GetContextName()));

        string mem_pool_name;
        MemoryPool* tx_mem_pool = mp_manager_->CreateSharedPool((mem_pool_name = MakeTxMemPoolName(GetContextName())),
                                    tx_mem_pool_block_size, tx_message_pool_size, num_emergent_messages);

        AMI_CHECHK_MP_CREATION(tx_mem_pool, GetContextName(),
                               mem_pool_name, tx_mem_pool_block_size,
                               tx_message_pool_size + num_emergent_messages);

        const uint32_t tx_mem_pool_max_blocks = props.GetValue("TxMessagePoolSizeMax", 64);
        MemoryPool* tx_mem_pool_max = mp_manager_->CreateSharedPool(  // 64MB, 1MB buffer
                            (mem_pool_name = MakeTxMemPoolNameMax(GetContextName())),
                            AMI_MAX_MESSAGE_SIZE_INTERNAL, tx_mem_pool_max_blocks, AMI_MAX_GC_BATCH_MSGS);

        AMI_CHECHK_MP_CREATION(tx_mem_pool_max, GetContextName(), 
                               mem_pool_name, AMI_MAX_MESSAGE_SIZE_INTERNAL,
                               tx_mem_pool_max_blocks + AMI_MAX_GC_BATCH_MSGS);

        tx_msg_pool_ = new MessagePool(
                            tx_mem_pool, tx_mem_pool_max,
                            ADK_OFFSET_OF(Message, app_data_begin) + AMI_MAX_OPTION_RESERVE_MEMORY,
                            "tx_pool");

        tx_msg_pool_->Init(boost::bind(&RecordAgent::OnMsgPoolInit, boost::placeholders::_1), 
                           boost::bind<bool>([](adk::MemoryBuffer** mem_buf, uint32_t len){ return false ;}, boost::placeholders::_1, boost::placeholders::_2), 
                           nullptr);

        msg_collector_ = new MsgCollector(mp_manager_);
        if (msg_collector_->Start() != ErrorCode::kSuccess)
        {
            delete tx_msg_pool_;
            delete mp_manager_;
            delete msg_collector_;
            tx_msg_pool_ = nullptr;
            mp_manager_ = nullptr;
            msg_collector_ = nullptr;
            return false;
        }
    }

    return true;
}

AmiMessage* RecordAgent::AllocAMIMessage(const std::size_t len)
{
    if (ADK_LIKELY(tx_msg_pool_))
    {
        auto ami_msg = tx_msg_pool_->NewMessage(len);
        if (ADK_LIKELY(ami_msg != nullptr))
        {
            ++new_msg_count_;
        }

        return ami_msg;
    }
    else
    {
        return nullptr;
    }
}

ErrorCode_def RecordAgent::DeleteMessage(AmiMessage* ami_msg)
{
    if (ADK_LIKELY(msg_collector_))
    {
        return msg_collector_->PushMsg(ami_msg);
    }
    else
    {
        return ErrorCode::kFailure;
    }
}

void RecordAgent::OnMsgPoolInit(AmiMessage* ami_msg)
{
    Message* app_msg = ami_msg->message();
    app_msg->ResetAppMessage();
    const uint32_t extra_header_len = ADK_OFFSET_OF(MemoryBuffer, data) + ADK_OFFSET_OF(AmiMessage, app_message) + ADK_OFFSET_OF(Message, app_data_begin);
    assert(Convertor::ConvertToMemoryBuffer(ami_msg)->mem_buf_size > extra_header_len);
    app_msg->mem_buf_avail = Convertor::ConvertToMemoryBuffer(ami_msg)->mem_buf_size - extra_header_len;
}


void RecordAgent::IndCollect(Property &ind_prop)
{
    ind_prop.SetValue("collected_msg_count", msg_collector_->collected_msg_count);
    ind_prop.SetValue("new_msg_count", new_msg_count_);
}

void RecordAgent::Stop()
{
    if (ref_counter_ == 0)
        return;

    if ((--ref_counter_) == 0)
    {
        if (record_client_ != nullptr)
            record_client_->Stop();
    }
}

std::vector<RxRecordChannel*> RecordAgent::CreateRxChannels(const Property& props,
                                                            Property& id_obj_props)
{
    LOG_INFO("rx recorder props: {1}", props.Dump());
    Property recorder_props = CascadeConfig(props, &id_obj_props);
    if (recorder_props.HasValue("RxMsgQueueSize"))
    {
        LOG_INFO("set MsgQueueSize by RxMsgQueueSize", "");
        uint32_t msg_queue_size = recorder_props.GetValue("RxMsgQueueSize", decltype(msg_queue_size_)::value_type());
        recorder_props.SetValue(config::context::recorder::kMsgQueueSize, msg_queue_size);
    }

    const std::vector<RxRecordChannel*>& chnls =
        record_client_->CreateMergeChannels(GetContextName(), recorder_props);
    if (!chnls.empty())
    {
        record_chnl_vec_.push_back(chnls[kRealtimeChannel]);
        rx_channel_ = chnls[kRealtimeChannel];
        rx_channel_->set_rx_channel_name("realtime_chan");
        record_chnl_vec_.push_back(chnls[kRetransmitChannel]);
        rx_retransmit_channel_ = chnls[kRetransmitChannel];
        rx_retransmit_channel_->set_rx_channel_name("repair_chan");
    }

    return chnls;
}

TxRecordChannel*
RecordAgent::CreateTxChannel(const std::string& transport_name,
                             const Property& props)
{
    boost::mutex::scoped_lock lock_guard(rec_chan_mutex_);

    TxRecordChannel* chnl = record_client_->CreateMessageChannel(
        GetContextName(), transport_name, CascadeConfig(props));
    if (chnl)
    {
        record_chnl_vec_.push_back(chnl);
        tx_channel_map_.emplace(transport_name, chnl);
    }

    return chnl;
}

StRecordChannel*
RecordAgent::CreateAckedSqnChannel(const Property& props)
{
    StRecordChannel* chnl = record_client_->CreateStatusChannel(GetContextName(), CascadeConfig(props));
    if (chnl)
    {
        st_channel_ = chnl;
        record_chnl_vec_.push_back(chnl);
    }

    return chnl;
}

ErrorCode_def
RecordAgent::GetTxHistMessage(const Message::IDType& endpoint_id,
                              int32_t partition_no,
                              const OnAMIMessageType& on_tx_hist_msg,
                              const Message::SqnType& begin,
                              const Message::SqnType& end)
{
    if (!ctx_)
        return kFailure;

    auto transport_name = ctx_->GetTxTransportName(endpoint_id, partition_no);
    if (!transport_name)
        return kFailure;

    return GetTxHistMessage(*transport_name, on_tx_hist_msg, begin, end);
}

ErrorCode_def RecordAgent::GetTxSTRHistMessage(const Message::IDType& endpoint_id,
                                               int32_t partition_no,
                                               const OnAMIMessageType& on_tx_hist_msg,
                                               const MessageHeader::IDType& stream_id,
                                               const Message::SqnType& begin,
                                               const Message::SqnType& end)
{
    if (!ctx_)
        return kFailure;

    auto transport_name = ctx_->GetTxTransportName(endpoint_id, partition_no);
    if (!transport_name)
        return kFailure;

    return GetTxSTRHistMessage(
        *transport_name, on_tx_hist_msg, stream_id, begin, end);
}

TxRecordChannel* RecordAgent::GetTxChannel(const Message::IDType& endpoint_id,
                                           int32_t partition_no) const
{
    if (!ctx_)
        return nullptr;

    auto transport_name = ctx_->GetTxTransportName(endpoint_id, partition_no);

    if (!transport_name)
    {
        return nullptr;
    }

    return GetTxChannel(*transport_name);
}

TxRecordChannel* RecordAgent::GetTxChannel(const std::string endpoint_name,
                                           int32_t partition_no) const
{
    std::string transport_name = endpoint_name;
    transport_name.append("_");
    transport_name.append(std::to_string(partition_no));
    transport_name.append("_");
    transport_name.append(ctx_->tier_name());

    return GetTxChannel(transport_name);
}

int32_t RecordAgent::GetTxTransports(const std::string& endpoint_name,
                                     std::vector<std::string>& txtp_vec) const
{
    assert(ctx_);
    assert(ctx_->coordinator());

    auto* txep = ctx_->coordinator()->GetTxEndpointByName(endpoint_name);
    if (txep == nullptr)
        return kFailure;

    txep->GetTransportList(txtp_vec);
    return kSuccess;
}

int32_t RecordAgent::GetRxEndpointIdByName(const std::string& endpoint_name) const
{
    assert(ctx_);
    assert(ctx_->coordinator());

    auto* rxep = ctx_->coordinator()->GetRxEndpointByName(endpoint_name);
    if (rxep == nullptr)
        return -1;

    return rxep->id();
}

int32_t RecordAgent::GetRxTransportIdListByName(const std::string& endpoint_name,
                                                std::vector<int32_t>& ids) const
{
    assert(ctx_);
    assert(ctx_->coordinator());

    auto* rxep = ctx_->coordinator()->GetRxEndpointByName(endpoint_name);
    if (rxep == nullptr)
        return kFailure;

    rxep->GetTransportIdList(ids);
    return kSuccess;
}

ErrorCode_def RecordAgent::GenTxMD5(const Message::IDType& endpoint_id,
                                    int32_t partition_no, MD5& md5,
                                    const Message::SqnType& begin,
                                    const Message::SqnType& end)
{
    if (!ctx_)
        return kFailure;

    auto transport_name = ctx_->GetTxTransportName(endpoint_id, partition_no);

    if (!transport_name)
    {
        return kFailure;
    }

    return GenTxMD5(*transport_name, md5, begin, end);
}

std::ostream& operator<<(std::ostream& os, const RecordAgent& agent)
{
    if (agent.record_chnl_vec_.empty())
    {
        os << "agent(->Recorder/" << agent.recorder_id_ << ", "
           << "ctx = " << agent.ctx_name_ << ", "
           << "data path = " << agent.data_path_ << ")";
    }
    else
    {
        os << "agent(->Recorder/" << agent.recorder_id_ << ", "
           << "ctx = " << agent.ctx_name_ << ", "
           << "data path = " << agent.data_path_ << "): \n";

        size_t cnt = agent.record_chnl_vec_.size();
        for (const auto chnl : agent.record_chnl_vec_)
        {
            if (1 == cnt--)
            {
                os << *chnl;
            }
            else
            {
                os << *chnl << '\n';
            }
        }
    }

    return os;
}

void RecordAgent::StartMonitor()
{
    adk::MonitorOps monitor_ops;
    monitor_ops.is_collection_indicator = true;
    monitor_ops.on_collection_indicator = boost::bind(&RecordAgent::OnCollectIndicator, this, _1);
    adk::Monitor::RegisterObject("RecordAgent", ctx_name_, &monitor_ops);
}

void RecordAgent::DetachSharedMemory()
{
    record_client_->DetachSharedMemory();
}

int32_t RecordAgent::IncreSyncIdMaps(Property& id_objects)
{
    return record_client_->IncreSyncIdMaps(id_objects);
}

}  // namespace ami
