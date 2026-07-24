/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_RX_RECORD_CHANNEL_H_
#define AMI_RX_RECORD_CHANNEL_H_

#include "../ami_constant.h"
#include "record_channel.h"
#include <assert.h>

namespace ami
{

using Convertor::ConvertToMemoryBuffer;

class RxRecordChannel : public RecordChannel
{
public:
    typedef RecordIterator<RxTag> RecordIteratorType;
    typedef KeyindexRecordIterator<RxTag, TransportKey> RecordTNPIteratorType;
    typedef KeyindexRecordIterator<RxTag, EndpointKey> RecordEDPIteratorType;
    typedef KeyindexRecordIterator<RxTag, StreamKey> RecordSTRIteratorType;

public:
    RxRecordChannel(const RxRecordChannel& rhs)
        : RecordChannel(rhs)
    {
    }

    ErrorCode_def GetHistMessage(const OnAMIMessageType& on_hist_msg,
                                 const Message::SqnType& begin = kBegin,
                                 const Message::SqnType& end   = kMostRecent)
    {
        boost::mutex::scoped_lock lock_guard(reader_lock_);
        return reader_->ReadRxHistMessage(
            boost::filesystem::path(RecordDataRoot(data_root_)) / path_,
            begin,
            end,
            on_hist_msg);
    }

    ErrorCode_def GetTNPHistMessage(const OnAMIMessageType& on_hist_msg,
                                    const AmiMetaData::IDType& transport_id,
                                    const Message::SqnType& begin = kBegin,
                                    const Message::SqnType& end   = kMostRecent)
    {
        boost::mutex::scoped_lock lock_guard(reader_lock_);
        return reader_->ReadRxTNPHistMessage(
            boost::filesystem::path(RecordDataRoot(data_root_)) / path_,
            transport_id,
            begin,
            end,
            on_hist_msg);
    }

    ErrorCode_def GetEDPHistMessage(const OnAMIMessageType& on_hist_msg,
                                    const AmiMetaData::IDType& endpoint_id,
                                    const Message::SqnType& begin = kBegin,
                                    const Message::SqnType& end   = kMostRecent)
    {
        boost::mutex::scoped_lock lock_guard(reader_lock_);
        return reader_->ReadRxEDPHistMessage(
            boost::filesystem::path(RecordDataRoot(data_root_)) / path_,
            endpoint_id,
            begin,
            end,
            on_hist_msg);
    }

    ErrorCode_def GetSTRHistMessage(const OnAMIMessageType& on_hist_msg,
                                    const MessageHeader::IDType& stream_id,
                                    const Message::SqnType& begin = kBegin,
                                    const Message::SqnType& end   = kMostRecent)
    {
        boost::mutex::scoped_lock lock_guard(reader_lock_);
        return reader_->ReadRxSTRHistMessage(
            boost::filesystem::path(RecordDataRoot(data_root_)) / path_,
            stream_id,
            begin,
            end,
            on_hist_msg);
    }

    ErrorCode_def GenMD5(MD5& md5,
                         const Message::SqnType& begin = kBegin,
                         const Message::SqnType& end   = kMostRecent)
    {
        MD5Calculator md5_c;
        auto ret = GetHistMessage([&md5_c](AmiMessage* ami_msg) {
            return md5_c(ami_msg);
        },
                                  begin, end);

        if (kFailure == ret)
        {
            return kFailure;
        }

        md5_c.GenMD5(md5);
        return kSuccess;
    }

    ErrorCode_def GenTNPMD5(MD5& md5,
                            const AmiMetaData::IDType& transport_id,
                            const Message::SqnType& begin = kBegin,
                            const Message::SqnType& end   = kMostRecent)
    {
        MD5Calculator md5_c;
        auto ret = GetTNPHistMessage([&md5_c](AmiMessage* ami_msg) {
            return md5_c(ami_msg);
        },
                                     transport_id, begin, end);

        if (kFailure == ret)
        {
            return kFailure;
        }

        md5_c.GenMD5(md5);
        return kSuccess;
    }

    ErrorCode_def GenEDPMD5(MD5& md5,
                            const AmiMetaData::IDType& endpoint_id,
                            const Message::SqnType& begin = kBegin,
                            const Message::SqnType& end   = kMostRecent)
    {
        MD5Calculator md5_c;
        auto ret = GetEDPHistMessage([&md5_c](AmiMessage* ami_msg) {
            return md5_c(ami_msg);
        },
                                     endpoint_id, begin, end);

        if (kFailure == ret)
        {
            return kFailure;
        }

        md5_c.GenMD5(md5);
        return kSuccess;
    }

    RecordIteratorType GetHistMessageIt(const Message::SqnType& sqn)
    {
        return RecordIteratorType(
            boost::filesystem::path(RecordDataRoot(data_root_)) / path_,
            sqn);
    }

    RecordTNPIteratorType GetTNPHistMessageIt(const AmiMetaData::IDType& transport_id,
                                              const Message::SqnType& sqn)
    {
        return RecordTNPIteratorType(
            boost::filesystem::path(RecordDataRoot(data_root_)) / path_,
            transport_id, sqn);
    }

    RecordEDPIteratorType GetEDPHistMessageIt(const AmiMetaData::IDType& endpoint_id,
                                              const Message::SqnType& sqn)
    {
        return RecordEDPIteratorType(
            boost::filesystem::path(RecordDataRoot(data_root_)) / path_,
            endpoint_id, sqn);
    }

    RecordSTRIteratorType GetSTRHistMessageIt(const MessageHeader::IDType& stream_id,
                                              const Message::SqnType& sqn)
    {
        return RecordSTRIteratorType(
            boost::filesystem::path(RecordDataRoot(data_root_)) / path_,
            stream_id, sqn);
    }

    ErrorCode_def PushMessageFast(AmiMessage* ami_message)
    {
        assert(nullptr != ami_message);
        assert(!(ami_message->ami_meta_data.ami_flags & AMI_MESSAGE_PRIVATE));

        ami_message->inc_slave_counter_rx();

        do
        {
            if (msg_ptr_queue_->EmplacePush<MQMsgEntry>(*ami_message)
                == kSuccess)
            {
                return kSuccess;
            }

            ++nr_push_block_;
            if (HasError())
            {
                break;
            }

            // usleep(1);
            ADK_PAUSE();
        } while (true);

        ami_message->dec_slave_counter_rx();
        ++discarded_msg_cnt_;
        return kSuccess;
    }

    /**
     * 通知recorder使用占位消息补充recorder正在重传队列上等待的缺失消息
     *
     * @param range_len - 需要使用占位消息补齐的连续序号的消息数量
     *                    （recorder正在重传队列上等待的待重传消息的
     *                    transport是确定的，因此这些连续序号的缺失消
     *                    息都应该是该transport上的消息）
     */
    ErrorCode_def RepairWithPlaceHolder(Message::SqnType range_len)
    {
        do
        {
            if (kSuccess == msg_ptr_queue_->EmplacePush<MQMsgEntry>(MsgRecord::kRepairWithPlaceHolder, range_len))
            {
                return kSuccess;
            }

            if (HasError())
            {
                break;
            }

            usleep(1);
        } while (true);

        return kSuccess;
    }

    Message::SqnType GetTNPHistMsgCnt(const AmiMetaData::IDType& transport_id)
    {
        return reader_->GetRxTNPHistMsgCnt(
            boost::filesystem::path(RecordDataRoot(data_root_)) / path_,
            transport_id);
    }

    ErrorCode_def GetRxHistMessageCnt()
    {
        return reader_->GetRxHistMessageCnt(
            boost::filesystem::path(RecordDataRoot(data_root_)) / path_);
    }

    void OnCollectIndicator(boost::property_tree::ptree& indicator)
    {
        indicator.put("rx_rec_chann", rx_channel_name_);
        indicator.put("nr_push_block", nr_push_block_);
        indicator.put("discarded_msg_cnt", discarded_msg_cnt_);

        msg_ptr_queue_->Dump(indicator);
    }

    void set_rx_channel_name(const std::string& rx_channel_name)
    {
        rx_channel_name_ = rx_channel_name;
    }

private:
    RxRecordChannel(const std::string data_root,
                    const std::string& path,
                    adk::MPSCQueue* msg_ptr_queue,
                    const std::string& tag = std::string())
        : RecordChannel(data_root, path, msg_ptr_queue, tag)
    {
    }

    uint32_t nr_push_block_ = 0;
    std::string rx_channel_name_;

    virtual void IncMessageCounter(AmiMessage* ami_message)
    {
        ami_message->inc_slave_counter_rx();
    }

    virtual void DecMessageCounter(AmiMessage* ami_message)
    {
        ami_message->dec_slave_counter_rx();
    }

    friend class AsyncRecordClient;
};

}  //namespace ami

#endif /* AMI_RX_RECORD_CHANNEL_H_ */
