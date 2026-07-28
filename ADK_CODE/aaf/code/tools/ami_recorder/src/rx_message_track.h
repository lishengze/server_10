/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_RX_MESSAGE_TRACK_H_
#define AMI_RX_MESSAGE_TRACK_H_

///< cpp std
#include <unordered_map>

///< impl
#include "message_track.h"
#include "record_reader.h"
#include "../test_driver.h"

namespace ami
{

class RxMessageTrack : public MessageTrack
{
private:
    typedef std::unordered_map<HashMapKeyType, Message::SqnType> TransportSqnMapType;

    static constexpr const char* kRepairQueuePrefix = "repair";

public:
    RxMessageTrack()
    {
        weight_ = kDefaultWeight * kRxWeightMultiply;
        last_msg_sqn_map_.reserve(65536);
    }

    virtual ~RxMessageTrack()
    {
        SyncToFile();
        DePreallocateDataFile();
        // DeleteFileBuffer(ordinal_index_file_buf_);
    }

private:
    virtual ErrorCode_def DoInit(const string& track_path,
                                 const Property& request,
                                 Property& reply,
                                 size_t recorder_worker_idx);

    virtual ErrorCode_def DoInit(const string& track_path, const Property& request, Property& reply)
    {
        IF_ERR_RET(MessageTrack::DoInit(track_path, request, reply));
        reply.SetValue(kOriginQueueIndex, (int)msg_ptr_queue_->index());
        reply.SetValue(kRepairQueueIndex, (int)repair_msg_queue_->index());

        return kSuccess;
    }

    virtual ErrorCode_def DoInit(const adk::ShmPointer& shm_point,
                                 size_t recorder_worker_idx);

    virtual bool IsEqual(const MessageTrack& rhs) const
    {
        const RxMessageTrack& real_rhs =
            dynamic_cast<const RxMessageTrack&>(rhs);
        return (MessageTrack::IsEqual(real_rhs)
                && (repair_msg_queue_ && real_rhs.repair_msg_queue_)
                && (repair_msg_queue_->name() == real_rhs.repair_msg_queue_->name()));
    }

    virtual Message::SizeType GetMessageFixLen() const
    {
        return sizeof(AmiMetaData::IDType) * 2 /*endpoint_id和transport_id*/
            + kAppmsgMetaDataLen;
    }

    virtual bool WriteMessage(std::streambuf& o,
                              const MsgRecord& msg_record,
                              RecordedMsgStuff& fms)
    {
        FileSizeType total_len = GetMessageFixLen() + msg_record.app_data_len;

        if (IsUseCRC())
        {
            crc_ = 0;
            total_len += sizeof(MsgCRCType);
        }

        if ((false == WriteMsgEndpointID(o, msg_record))
            || (false == WriteMsgTransportID(o, msg_record))
            || (false == WriteAppMsg(o, msg_record, fms)))
        {
            return false;
        }

        if (IsUseCRC() && (false == WriteCRC(o)))
        {
            return false;
        }

        // during automatically test.
        // gen corrupted recorder data 
        AMI_TD_JOB_BY_ENV(
                "AMI_TEST_RECORDER_CRC_ERR",
                { 
                    *((char*) msg_record.app_data) = 0;
                }
        );

        // cur_msgdata_filepos_ was used to calc index value
        cur_msgdata_filepos_ += total_len;

        return true;
    }

    virtual bool RecordOneMessage(MsgRecord& msg_record,
                                  MQMsgEntry* entry         = nullptr,
                                  adk::MPSCQueue* msg_queue = nullptr)
    {
        const auto cur_msgdata_filepos = cur_msgdata_filepos_;
        RecordedMsgStuff* fms_ptr      = nullptr;
        try
        {
            if (entry)
            {
                auto& s = PushIntoRecordingMsgQ(this, entry, msg_queue);
                fms_ptr = &s;
            }
            else
            {
                auto& s = PushIntoRecordingMsgQ(this, const_cast<MsgRecord*>(&msg_record));
                fms_ptr = &s;
            }
        }
        catch (const std::system_error&)
        {
            return false;
        }

        RecordedMsgStuff& fms = *fms_ptr;

        msg_record.ex_msg_header.msg_prop &= RecordedMsgProp::kNoFlag;
        msg_record.ex_msg_header.pad1 = 0;

        auto oridnal_index = OrdinalIndex(cur_msgdata_filepos);
        FileWriteBuffer* stream_write_buffer =
            GetFileWriteBuffer(StreamKey(msg_record.msg_header.stream_id),
                               oridnal_index, fms);
        if (stream_write_buffer == nullptr)
        {
            LOG_FATAL("record message record({1}), get stream index buffer failed.", msg_record);
            return false;
        }
        msg_record.ex_msg_header.c_stream_sqn = stream_write_buffer->next_msg_sqn();

        FileWriteBuffer* transport_write_buffer =
            GetFileWriteBuffer(TransportKey(msg_record.transport_id),
                               oridnal_index, fms);
        if (transport_write_buffer == nullptr)
        {
            LOG_FATAL("record message record({1}), get transport index buffer failed.", msg_record);
            return false;
        }
        msg_record.ex_msg_header.c_topic_sqn = transport_write_buffer->next_msg_sqn();

        FileWriteBuffer* endpoint_write_buffer =
            GetFileWriteBuffer(EndpointKey(msg_record.endpoint_id),
                               oridnal_index, fms);
        if (endpoint_write_buffer == nullptr)
        {
            LOG_FATAL("record message record({1}), get endpoint index buffer failed.", msg_record);
            return false;
        }
        msg_record.ex_msg_header.c_endpoint_sqn     = endpoint_write_buffer->next_msg_sqn();
        msg_record.ex_msg_header.ami_user_context_0 = 0;
        msg_record.ex_msg_header.ami_user_context_1 = 0;

        if ((false == WriteMessage(*msg_data_file_buf_, msg_record, fms))
            || (false == WriteMsgOrdinalIndex(*stream_write_buffer, oridnal_index, fms))
            || (false == WriteMsgOrdinalIndex(*transport_write_buffer, oridnal_index, fms))
            || (false == WriteMsgOrdinalIndex(*endpoint_write_buffer, oridnal_index, fms))
            /*ordinal index应该最后写，以使得ordinal index
             *落地成功则该消息的数据和所有索引一定落地成功*/
            || (false == WriteMsgOrdinalIndex(*ordinal_index_file_buf_, oridnal_index, fms)))
        {
            LOG_FATAL("record message record({1}) failed.", msg_record);
            return false;
        }

        if (!PreallocateDataFile())
        {
            return false;
        }

        return true;
    }

    virtual void DoReleaseMessage(AmiMessage& ami_msg) const
    {
        ami_msg.dec_slave_counter_rx();
    }

    virtual ErrorCode_def FilterMessage(const MsgRecord* msg_record,
                                        MQMsgEntry* entry);

    virtual ErrorCode_def
    GetLastMessageLen(const Message::SqnType& last_msg_sqn,
                      Message::SizeType& last_msg_len) const;

    virtual ErrorCode_def FetchThenRecordOneMessage()
    {
        if (repairing_)
        {
            return Repair();
        }
        else
        {
            return MessageTrack::FetchThenRecordOneMessage();
        }
    }

    virtual char GetTypeChar() const
    {
        return 'r';
    }

    virtual ErrorCode_def RecoverIndexDataFiles();
    virtual ErrorCode_def RecoverIndexDataFiles(const LostMsg&);

    virtual void ClearQueueMsgAtRecovery();

    ErrorCode_def Repair();

    std::string GetRepairQName() const
    {
        return (boost::locale::format("{1}_{2}")
                % kRepairQueuePrefix
                % path_)
            .str();
    }

private:
    adk::MPSCQueue* repair_msg_queue_ = nullptr;

    /***************************************************************************
     * 在故障恢复时需要正确复位的状态量
     */

    ///< key: transport_id, mapped value: 该transport已经写到buffer的最后一个消息的序号
    TransportSqnMapType last_msg_sqn_map_;
    /**************************************************************************/

    bool repairing_            = false;  //true: 正在做重传修复
    MQMsgEntry* pending_entry_ = nullptr;
    IntervalLogger inv_logger_;  ///< 一秒打一次的日志对象

    LOG_DECLARE
};

}  // namespace ami

#endif /* AMI_RX_MESSAGE_TRACK_H_ */
