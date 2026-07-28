/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_TX_MESSAGE_TRACK_H_
#define AMI_TX_MESSAGE_TRACK_H_

///< impl
#include "message_track.h"
#include <adk/arch/generic.h>

namespace ami
{

class TxMessageTrack : public MessageTrack
{
public:
    TxMessageTrack()
        : last_msg_sqn_(AmiRecorderBase::kBegin - 1u)
    {
        weight_ = kDefaultWeight;
    }

    virtual ~TxMessageTrack()
    {
        SyncToFile();
        DePreallocateDataFile();
        // DeleteFileBuffer(ordinal_index_file_buf_);
    }

private:
    virtual ErrorCode_def DoInit(const string& track_path,
                                 const Property& request,
                                 Property& reply,
                                 size_t recorder_worker_idx)
    {
        IF_ERR_RET(MessageTrack::DoInit(track_path, request, reply, recorder_worker_idx));
        reply.SetValue(kDataQueueIndex, (int)msg_ptr_queue_->index());

        return kSuccess;
    }

    virtual ErrorCode_def DoInit(const string& track_path,
                                 const Property& request,
                                 Property& reply)
    {
        IF_ERR_RET(MessageTrack::DoInit(track_path, request, reply));
        reply.SetValue(kDataQueueIndex, (int)msg_ptr_queue_->index());

        return kSuccess;
    }

    virtual ErrorCode_def DoInit(const adk::ShmPointer& shm_point,
                                 size_t recorder_worker_idx)
    {
        // create msg data file buffer by shm_point
        // init path_ and data_path_ by shm_point
        // create data file buf and ordinal_index file buffer
        // attach message ptr queue, consistent the producer and consumer side
        IF_ERR_RET(MessageTrack::DoInit(shm_point, recorder_worker_idx));

        // seek index file pos
        // seek data file pos
        // create partial index file, seek partital index file
        // recover the last_msg_sqn_
        // note: no truncate
        IF_ERR_RET(RecoverIndexDataFiles());
        
        // recorded_msg_cnt_ was updated in RecoverIndexDataFiles
        msg_cnt_before_process_mq_ = recorded_msg_cnt_;
        return kSuccess;
    }

    virtual Message::SizeType GetMessageFixLen() const
    {
        return kAppmsgMetaDataLen;
    }

    virtual bool WriteMessage(std::streambuf& o, const MsgRecord& msg_record, RecordedMsgStuff& fms)
    {
        FileSizeType total_len =
            GetMessageFixLen() + msg_record.app_data_len;

        if (IsUseCRC())
        {
            crc_ = 0;
            total_len += sizeof(MsgCRCType);
        }

        if (false == WriteAppMsg(o, msg_record, fms))
        {
            return false;
        }

        if (IsUseCRC() && (false == WriteCRC(o)))
        {
            return false;
        }

        // cur_msgdata_filepos_ was used to calc index value
        // cur_msgdata_filepos_ was used to control the fallocate call
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
                auto& s = PushIntoRecordingMsgQ(
                    this, const_cast<MsgRecord*>(&msg_record));
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

        FileWriteBuffer* stream_write_buffer =
            GetFileWriteBuffer(StreamKey(msg_record.msg_header.stream_id),
                               OrdinalIndex(cur_msgdata_filepos), fms);
        if (stream_write_buffer == nullptr)
        {
            LOG_FATAL("record message record({1}) failed.", msg_record);
            return false;
        }
        msg_record.ex_msg_header.c_stream_sqn   = stream_write_buffer->next_msg_sqn();
        msg_record.ex_msg_header.c_topic_sqn    = ordinal_index_file_buf_->next_msg_sqn();
        msg_record.ex_msg_header.c_endpoint_sqn = 0;

        ami::Message* msg                           = ADK_CONTAINER_OF(msg_record.app_data,
                                             ami::Message, app_data_begin);
        ami::AmiMessage* ami_msg                    = ADK_CONTAINER_OF(msg, ami::AmiMessage, app_message);
        msg_record.ex_msg_header.ami_user_context_0 = ami_msg->ami_meta_data.ami_persistent_context_0;
        msg_record.ex_msg_header.ami_user_context_1 = ami_msg->ami_meta_data.ami_persistent_context_1;

        if ((false == WriteMessage(*msg_data_file_buf_, msg_record, fms))
            || (false == WriteMsgOrdinalIndex(*stream_write_buffer, OrdinalIndex(cur_msgdata_filepos), fms))
            /*ordinal index应该最后写，以使得ordinal index
            *落地成功则该消息的数据和所有索引一定落地成功*/
            || (false == WriteMsgOrdinalIndex(*ordinal_index_file_buf_, OrdinalIndex(cur_msgdata_filepos), fms)))
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
        ami_msg.dec_slave_counter_tx();
    }

    virtual ErrorCode_def FilterMessage(const MsgRecord* msg_record,
                                        MQMsgEntry* entry);

    virtual ErrorCode_def GetLastMessageLen(const Message::SqnType& last_msg_sqn,
                                            Message::SizeType& last_msg_len) const;

    virtual char GetTypeChar() const
    {
        return 't';
    }

    virtual ErrorCode_def RecoverIndexDataFiles();
    virtual ErrorCode_def RecoverIndexDataFiles(const LostMsg&);

    virtual void ClearQueueMsgAtRecovery();

private:
    /***************************************************************************
     * 在故障恢复时需要正确复位的状态量
     */
    Message::SqnType last_msg_sqn_;  ///< 最近一个已经写到buffer的消息的序号
    /**************************************************************************/

    LOG_DECLARE
};

}  // namespace ami

#endif /* AMI_TX_MESSAGE_TRACK_H_ */
