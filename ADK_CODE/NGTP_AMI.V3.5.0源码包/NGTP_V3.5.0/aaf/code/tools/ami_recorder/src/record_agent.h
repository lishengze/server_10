/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_RECORD_AGENT_H_
#define AMI_RECORD_AGENT_H_

///< cpp std
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

///< boost
#include <boost/asio/io_service.hpp>
#include <boost/optional.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/thread/mutex.hpp>

///< ami public
#include <ami/ami_record_agent.h>
#include <ami/config_key.h>
#include <ami/error_code.h>
#include <ami/message.h>
#include <ami/property.h>

///< ami impl
#include "../log.h"

///< impl
#include "config_default_value.h"
#include "recorder_base.h"
#include "recorder_fwd.h"
#include "rx_record_channel.h"
#include "st_record_channel.h"
#include "tx_record_channel.h"

namespace adk
{
class MPManager;
}

namespace ami
{

class ContextImpl;
class MessagePool;
class MsgCollector;

class RecordAgent
{
public:
    typedef std::function<void()> OnRcdNotExistType;
    using OnAMIMessageType = ami::OnAMIMessageType;

    static constexpr size_t kRealtimeChannel      = 0;
    static constexpr size_t kRetransmitChannel    = 1;
    static constexpr size_t kDefMsgQueueSize      = recorder::cdv::kMsgQueueSize;
    static constexpr Message::SqnType kBegin      = AmiRecordAgent::kBegin;
    static constexpr Message::SqnType kMostRecent = AmiRecordAgent::kMostRecent;
    static constexpr const char* kIsAgentRecovery = "IsAgentRecovery";

private:
    typedef std::map<std::string, TxRecordChannel*> TxChannelMapType;
    typedef AmiRecorderBase::MD5CheckCode MD5;

public:
    RecordAgent();
    RecordAgent(ContextImpl* ctx);
    ~RecordAgent();

    /**
     * 初始化
     *
     * @param recorder_id 拟连接的recorder的id
     * @param props 配置
     * @param on_recorder_ne 检测不到recorder的时候调用此回调
     *
     * @return true - 成功，false - 失败，ami应该退出
     */
    //[record_agent_config_detail
    /*`
      [h3 agent的配置]
      domain server中每个context都可以配置自己的record agent配置，实际
      配置时，在指定context配置中增加子配置树"RecordAgent"即可。或者创
      建RecordAgent时在Property中设置配置项。可用的配置项如下：
      
      * config::context::kName - agent关联的context名
      
      * config::context::recorder::kReportStatus - 上报状态。默认为true。
      
      * config::context::recorder::kMsgQueueSize - 同recorder的对应配置项
       
      * config::context::recorder::kUseMsgCRC - 同recorder的对应配置项
       
      * config::context::recorder::kIgnoreLostMsg - 同recorder的对应配置项
      */
    //]
    bool Init(const RecorderId& recorder_id, const Property& props,
              const OnRcdNotExistType& on_recorder_ne = OnRcdNotExistType());

    void Stop();

    /**
     * 释放所有在本agent创建的RecordChannel上调用PushMessage阻塞的线程
     */
    void ReleasePushThread()
    {
        for (auto chnl : record_chnl_vec_)
        {
            chnl->AgentReleased();
        }
    }

    /**
     * 创建接收方向持久化通道组
     *
     * @param props 该通道的配置参数
     *
     * @return 持久化通道数组，ret[kRealtimeChannel]为实时消息通道，
     * ret[kRetransmitChannel]为重传消息通道。如果数组为空表示创建失败。
     *
     * @par 关于重传
     * ami将实时消息放入实时消息通道。如果某一transport上的某topic_sqn消息在实时消息通道中丢失，
     * 则ami应该将其放入重传消息通道
     */
    //[record_agent_channel_config_detail
    /*`
      [heading record_channel的配置]
      创建channel时在Property中设置的配置项如下
      
      * config::context::recorder::kMsgQueueSize - 同recorder的对应配置项
       
      * config::context::recorder::kUseMsgCRC - 同recorder的对应配置项
       
      * config::context::recorder::kIgnoreLostMsg - 同recorder的对应配置项
      */
    //]
    std::vector<RxRecordChannel*> CreateRxChannels(const Property& props,
                                                   Property& id_obj_props);
    /**
     * 已经弃用，被相应新接口替代
     */
    std::vector<RxRecordChannel*> CreateRxChannels(size_t msg_queue_size = kDefMsgQueueSize)
    {
        Property props;
        props.SetValue(config::context::recorder::kMsgQueueSize,
                       msg_queue_size);

        Property props_fake;
        return CreateRxChannels(props, props_fake);
    }

    RxRecordChannel* GetRxChannel() const
    {
        return rx_channel_;
    }

    /**
     * @brief 创建发送方向持久化通道
     *
     * @param transport_name
     * @param props 该通道的配置参数
     *
     * @return nullptr则为创建失败；否则为创建成功
     */
    TxRecordChannel* CreateTxChannel(const std::string& transport_name, const Property& props);
    /**
     * 已经弃用，被相应新接口替代
     */
    TxRecordChannel* CreateTxChannel(const std::string& transport_name,
                                     size_t msg_queue_size = kDefMsgQueueSize)
    {
        Property props;
        props.SetValue(config::context::recorder::kMsgQueueSize,
                       msg_queue_size);

        return CreateTxChannel(transport_name, props);
    }

    TxRecordChannel* GetTxChannel(const std::string& transport_name) const
    {
        if (tx_channel_map_.count(transport_name))
        {
            return tx_channel_map_.at(transport_name);
        }
        else
        {
            return nullptr;
        }
    }

    TxRecordChannel* GetTxChannel(const Message::IDType& endpoint_id, int32_t partition_no) const;

    TxRecordChannel* GetTxChannel(const std::string endpoint_name, int32_t partition_no) const;

    int32_t GetTxTransports(const std::string& endpoint_name,
                            std::vector<std::string>& txtp_vec) const;

    int32_t GetRxEndpointIdByName(const std::string& endpoint_name) const;

    int32_t GetRxTransportIdListByName(const std::string& endpoint_name,
                                       std::vector<int32_t>& ids) const;

    /**
     * @brief 创建应答序列号持久化通道
     *     
     * @param props 该通道的配置参数
     *
     * @return nullptr则为创建失败；否则为创建成功
     */
    StRecordChannel* CreateAckedSqnChannel(const Property& props);
    /**
     * 已经弃用，被相应新接口替代
     */
    StRecordChannel* CreateAckedSqnChannel(size_t msg_queue_size = kDefMsgQueueSize)
    {
        Property props;
        props.SetValue(
            config::context::recorder::kMsgQueueSize, msg_queue_size);

        return CreateAckedSqnChannel(props);
    }

    ErrorCode_def GetRxHistMessage(const OnAMIMessageType& on_rx_hist_msg,
                                   const Message::SqnType& begin = kBegin,
                                   const Message::SqnType& end   = kMostRecent)
    {
        if (rx_channel_)
        {
            return rx_channel_->GetHistMessage(on_rx_hist_msg, begin, end);
        }
        else
        {
            return kFailure;
        }
    }

    ErrorCode_def GetRxTNPHistMessage(const OnAMIMessageType& on_rx_hist_msg,
                                      const AmiMetaData::IDType& transport_id,
                                      const Message::SqnType& begin = kBegin,
                                      const Message::SqnType& end   = kMostRecent)
    {
        if (rx_channel_)
        {
            return rx_channel_->GetTNPHistMessage(on_rx_hist_msg, transport_id, begin, end);
        }
        else
        {
            return kFailure;
        }
    }

    ErrorCode_def GetRxEDPHistMessage(const OnAMIMessageType& on_rx_hist_msg,
                                      const AmiMetaData::IDType& endpoint_id,
                                      const Message::SqnType& begin = kBegin,
                                      const Message::SqnType& end   = kMostRecent)
    {
        if (rx_channel_)
        {
            return rx_channel_->GetEDPHistMessage(on_rx_hist_msg, endpoint_id, begin, end);
        }
        else
        {
            return kFailure;
        }
    }

    ErrorCode_def GetRxSTRHistMessage(const OnAMIMessageType& on_rx_hist_msg,
                                      const MessageHeader::IDType& stream_id,
                                      const Message::SqnType& begin = kBegin,
                                      const Message::SqnType& end   = kMostRecent)
    {
        if (rx_channel_)
        {
            return rx_channel_->GetSTRHistMessage(on_rx_hist_msg, stream_id, begin, end);
        }
        else
        {
            return kFailure;
        }
    }

    ErrorCode_def GenRxMD5(MD5& md5,
                           const Message::SqnType& begin = kBegin,
                           const Message::SqnType& end   = kMostRecent)
    {
        if (rx_channel_)
        {
            return rx_channel_->GenMD5(md5, begin, end);
        }
        else
        {
            return kFailure;
        }
    }

    ErrorCode_def GenRxTNPMD5(MD5& md5, const AmiMetaData::IDType& transport_id,
                              const Message::SqnType& begin = kBegin,
                              const Message::SqnType& end   = kMostRecent)
    {
        if (rx_channel_)
        {
            return rx_channel_->GenTNPMD5(md5, transport_id, begin, end);
        }
        else
        {
            return kFailure;
        }
    }

    ErrorCode_def GenRxEDPMD5(MD5& md5, const AmiMetaData::IDType& endpoint_id,
                              const Message::SqnType& begin = kBegin,
                              const Message::SqnType& end   = kMostRecent)
    {
        if (rx_channel_)
        {
            return rx_channel_->GenEDPMD5(md5, endpoint_id, begin, end);
        }
        else
        {
            return kFailure;
        }
    }

    ErrorCode_def GetTxHistMessage(const std::string& transport_name,
                                   const OnAMIMessageType& on_tx_hist_msg,
                                   const Message::SqnType& begin = kBegin,
                                   const Message::SqnType& end   = kMostRecent)
    {
        if (tx_channel_map_.count(transport_name))
        {
            return tx_channel_map_.at(transport_name)
                ->GetHistMessage(on_tx_hist_msg, begin, end);
        }
        else
        {
            return kFailure;
        }
    }

    ErrorCode_def GetTxHistMessage(const Message::IDType& endpoint_id,
                                   int32_t partition_no,
                                   const OnAMIMessageType& on_tx_hist_msg,
                                   const Message::SqnType& begin = kBegin,
                                   const Message::SqnType& end   = kMostRecent);

    ErrorCode_def GetTxSTRHistMessage(const std::string& transport_name,
                                      const OnAMIMessageType& on_tx_hist_msg,
                                      const MessageHeader::IDType& stream_id,
                                      const Message::SqnType& begin = kBegin,
                                      const Message::SqnType& end   = kMostRecent)
    {
        if (tx_channel_map_.count(transport_name))
        {
            return tx_channel_map_.at(transport_name)
                ->GetSTRHistMessage(on_tx_hist_msg, stream_id, begin, end);
        }
        else
        {
            return kFailure;
        }
    }

    ErrorCode_def GetTxSTRHistMessage(const Message::IDType& endpoint_id,
                                      int32_t partition_no,
                                      const OnAMIMessageType& on_tx_hist_msg,
                                      const MessageHeader::IDType& stream_id,
                                      const Message::SqnType& begin = kBegin,
                                      const Message::SqnType& end   = kMostRecent);

    ErrorCode_def GenTxMD5(const std::string& transport_name,
                           MD5& md5, const Message::SqnType& begin = kBegin,
                           const Message::SqnType& end = kMostRecent)
    {
        if (tx_channel_map_.count(transport_name))
        {
            return tx_channel_map_.at(transport_name)->GenMD5(md5, begin, end);
        }
        else
        {
            return kFailure;
        }
    }

    ErrorCode_def GenTxMD5(const Message::IDType& endpoint_id,
                           int32_t partition_no,
                           MD5& md5, const Message::SqnType& begin = kBegin,
                           const Message::SqnType& end = kMostRecent);

    ErrorCode_def GetStatusMessage(const OnAMIMessageType& on_status_msg)
    {
        if (st_channel_)
        {
            return st_channel_->GetStatusMessage(on_status_msg);
        }
        else
        {
            return kFailure;
        }
    }

    ErrorCode_def GetRxHistMessageCnt(uint64_t& nr_msgs)
    {
        if (rx_channel_)
        {
            nr_msgs = rx_channel_->GetRxHistMessageCnt();
            return kSuccess;
        }
        else
        {
            return kFailure;
        }
    }

    ErrorCode_def GetRxTNPHistMsgCnt(const AmiMetaData::IDType& transport_id, Message::SqnType& sqn)
    {
        if (rx_channel_)
        {
            sqn = rx_channel_->GetTNPHistMsgCnt(transport_id);
            return kSuccess;
        }
        else
        {
            return kFailure;
        }
    }

    ErrorCode_def GetTxTNPHistMsgCnt(const std::string& transport_name, Message::SqnType& sqn)
    {
        if (tx_channel_map_.count(transport_name))
        {
            sqn = tx_channel_map_.at(transport_name)->GetTNPHistMsgCnt();
            return kSuccess;
        }
        else
        {
            return kFailure;
        }
    }

    ErrorCode_def GetTxHistMessageCnt(uint64_t& nr_msgs)
    {
        nr_msgs = 0;
        for (auto& node_pair : tx_channel_map_)
        {
            // FIXME: handler errors
            nr_msgs += node_pair.second->GetTNPHistMsgCnt();
        }
        return kSuccess;
    }

    ErrorCode_def GetTxSTRHistMsgCnt(const std::string& transport_name,
                                     const MessageHeader::IDType& stream_id,
                                     Message::SqnType& sqn)
    {
        auto it = tx_channel_map_.find(transport_name);
        if (it != tx_channel_map_.end())
        {
            sqn = it->second->GetTxSTRHistMsgCnt(stream_id);
            return kSuccess;
        }
        else
        {
            return kFailure;
        }
    }

    bool OnCollectIndicator(boost::property_tree::ptree& indicator)
    {
        boost::mutex::scoped_lock lock_guard(rec_chan_mutex_);
        boost::property_tree::ptree& rec_chan_indi_tree =
            indicator.add_child("record_channels", boost::property_tree::ptree());

        {
            boost::property_tree::ptree& chan_indi_tree = rec_chan_indi_tree.push_back(
                                                                                boost::property_tree::ptree::value_type("", boost::property_tree::ptree()))
                                                              ->second;
            if (rx_channel_ != nullptr)
                rx_channel_->OnCollectIndicator(chan_indi_tree);
        }

        {
            boost::property_tree::ptree& chan_indi_tree = rec_chan_indi_tree.push_back(
                                                                                boost::property_tree::ptree::value_type("", boost::property_tree::ptree()))
                                                              ->second;
            if (rx_retransmit_channel_ != nullptr)
                rx_retransmit_channel_->OnCollectIndicator(chan_indi_tree);
        }

        for (auto& node : tx_channel_map_)
        {
            boost::property_tree::ptree& chan_indi_tree = rec_chan_indi_tree.push_back(
                                                                                boost::property_tree::ptree::value_type("", boost::property_tree::ptree()))
                                                              ->second;
            node.second->OnCollectIndicator(chan_indi_tree);
        }
        return true;
    }

    void StartMonitor();

    void DetachSharedMemory();

    AmiMessage* AllocAMIMessage(const std::size_t len);

    int32_t DeleteMessage(AmiMessage* ami_msg);

    void IndCollect(Property &ind_prop);
    
    int32_t IncreSyncIdMaps(Property& id_objects);
    
private:
    RecordAgent(const RecordAgent&) = delete;
    RecordAgent& operator=(const RecordAgent&) = delete;

    std::string GetDataPathStr() const
    {
        return data_path_;
    }

    std::string GetContextName() const
    {
        return ctx_name_;
    }

    Property CascadeConfig(const Property& props, Property* id_obj_props = nullptr)
    {
        Property res = props;
        if (id_obj_props != nullptr)
        {
            res.OverWriteFrom(*id_obj_props);
        }

        if (!props.HasValue(config::context::recorder::kMsgQueueSize)
            && msg_queue_size_)
        {
            res.SetValue(config::context::recorder::kMsgQueueSize,
                         *msg_queue_size_);
        }

        if (!props.HasValue(config::context::recorder::kUseMsgCRC)
            && use_msg_crc_)
        {
            res.SetValue(config::context::recorder::kUseMsgCRC,
                         *use_msg_crc_);
        }

        if (!props.HasValue(config::context::recorder::kIgnoreLostMsg)
            && ignore_lost_msg_)
        {
            res.SetValue(config::context::recorder::kIgnoreLostMsg,
                         *ignore_lost_msg_);
        }

        return res;
    }

    void OnRecordNotExist()
    {
        for (auto chnl : record_chnl_vec_)
        {
            chnl->RecorderNotExist();
        }

        if (record_not_exist_cb_)
        {
            record_not_exist_cb_();
        }
    }

    void DumpToPtree(boost::property_tree::ptree& status_tree) const
    {
        boost::property_tree::ptree& channels_status_tree =
            status_tree.add_child("channels", boost::property_tree::ptree());

        for (const auto chnl : record_chnl_vec_)
        {
            chnl->DumpToPtree(channels_status_tree);
        }
    }

    // bool OnCollectIndicator(boost::property_tree::ptree& indicator) const
    // {
    //     DumpToPtree(indicator);
    //     return true;
    // }

    static void OnMsgPoolInit(AmiMessage* ami_msg);

    boost::mutex rec_chan_mutex_;

    RecordClient* record_client_;
    std::vector<RecordChannel*> record_chnl_vec_;
    RxRecordChannel* rx_channel_            = nullptr;
    RxRecordChannel* rx_retransmit_channel_ = nullptr;
    TxChannelMapType tx_channel_map_;
    StRecordChannel* st_channel_ = nullptr;
    std::string data_path_;
    std::string ctx_name_;
    RecorderId recorder_id_;
    bool report_status_ = true;
    boost::optional<size_t> msg_queue_size_;
    boost::optional<bool> use_msg_crc_;
    boost::optional<bool> ignore_lost_msg_;
    OnRcdNotExistType record_not_exist_cb_;

    ContextImpl* ctx_ = nullptr;
    
    adk::MPManager* mp_manager_ = nullptr;
    MessagePool* tx_msg_pool_ = nullptr;
    MsgCollector* msg_collector_ = nullptr; 
    uint64_t new_msg_count_ = 0;
    std::atomic<int32_t> ref_counter_ = {0};

    LOG_DECLARE
    friend std::ostream& operator<<(std::ostream&, const RecordAgent&);
};

std::ostream& operator<<(std::ostream&, const RecordAgent&);

}  //namespace ami
namespace fmt
{
template <> 
struct formatter<ami::RecordAgent> : formatter<string_view>
{
    template<typename FormatContext>
    auto format(const ami::RecordAgent& x, FormatContext& ctx) -> decltype(this->formatter<string_view>::format(string_view{}, ctx))
    {
        std::ostringstream os;
        os << x;
        return formatter<string_view>::format(string_view(os.str()), ctx);
    }
};
}
#endif /* AMI_RECORD_AGENT_H_ */
