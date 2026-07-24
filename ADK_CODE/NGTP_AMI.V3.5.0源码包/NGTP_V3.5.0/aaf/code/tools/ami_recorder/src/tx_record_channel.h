/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_TX_RECORD_CHANNEL_H_
#define AMI_TX_RECORD_CHANNEL_H_

///< impl
#include "../ami_constant.h"
#include "record_channel.h"
#include "recorder_base.h"

#include "assert.h"
#include <boost/property_tree/ptree.hpp>

namespace ami
{
using Convertor::ConvertToMemoryBuffer;

class TxRecordChannel : public RecordChannel
{
public:
    typedef RecordIterator<TxTag> RecordIteratorType;
    typedef KeyindexRecordIterator<TxTag, StreamKey> RecordSTRIteratorType;

public:
    TxRecordChannel(const TxRecordChannel& rhs)
        : RecordChannel(rhs),
          transport_name_(rhs.transport_name_)
    {
    }

    ErrorCode_def GetHistMessage(const OnAMIMessageType& on_hist_msg,
                                 const Message::SqnType& begin = kBegin,
                                 const Message::SqnType& end   = kMostRecent)
    {
        boost::mutex::scoped_lock lock_guard(reader_lock_);
        return reader_->ReadTxHistMessage(
            boost::filesystem::path(RecordDataRoot(data_root_)) / path_,
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
        return reader_->ReadTxSTRHistMessage(
            boost::filesystem::path(RecordDataRoot(data_root_)) / path_,
            stream_id,
            begin,
            end,
            on_hist_msg);
    }

    ErrorCode_def GenMD5(MD5& md5, const Message::SqnType& begin = kBegin,
                         const Message::SqnType& end = kMostRecent)
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

    RecordIteratorType GetHistMessageIt(const Message::SqnType& sqn)
    {
        return RecordIteratorType(
            boost::filesystem::path(RecordDataRoot(data_root_)) / path_, sqn);
    }

    ErrorCode_def PushMessageFast(AmiMessage* ami_message)
    {
        assert(nullptr != ami_message);
        assert(!(ami_message->ami_meta_data.ami_flags & AMI_MESSAGE_PRIVATE));

        ami_message->inc_slave_counter_tx();

        do
        {
            if (adk::kSuccess == msg_ptr_queue_->EmplacePush<MQMsgEntry>(*ami_message))
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

        ami_message->dec_slave_counter_tx();
        ++discarded_msg_cnt_;
        return kSuccess;
    }

    Message::SqnType GetTNPHistMsgCnt() const
    {
        return reader_->GetHistMsgCnt(
            boost::filesystem::path(RecordDataRoot(data_root_)) / path_);
    }

    Message::SqnType GetTxSTRHistMsgCnt(const MessageHeader::IDType& stream_id)
    {
        return reader_->GetTxSTRHistMsgCnt(
            boost::filesystem::path(RecordDataRoot(data_root_)) / path_,
            stream_id);
    }

    void OnCollectIndicator(boost::property_tree::ptree& indicator)
    {
        indicator.put("tx_rec_chan", transport_name_);
        indicator.put("nr_push_block", nr_push_block_);
        indicator.put("discarded_msg_cnt", discarded_msg_cnt_);

        msg_ptr_queue_->Dump(indicator);
    }

private:
    TxRecordChannel(const std::string data_root,
                    const std::string& path,
                    adk::MPSCQueue* msg_ptr_queue,
                    const std::string& tag = std::string())
        : RecordChannel(data_root, path, msg_ptr_queue, tag),
          transport_name_(TransportName(path))
    {
    }

    virtual void IncMessageCounter(AmiMessage* ami_message)
    {
        ami_message->inc_slave_counter_tx();
    }

    virtual void DecMessageCounter(AmiMessage* ami_message)
    {
        ami_message->dec_slave_counter_tx();
    }

    uint64_t nr_push_block_ = 0;
    std::string transport_name_;

    friend class AsyncRecordClient;
};

}  //namespace ami

#endif /* AMI_TX_RECORD_CHANNEL_H_ */
