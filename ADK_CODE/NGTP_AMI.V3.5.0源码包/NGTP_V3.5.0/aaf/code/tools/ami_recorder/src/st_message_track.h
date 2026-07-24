/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_ST_MESSAGE_TRACK_H_
#define AMI_ST_MESSAGE_TRACK_H_

///< cpp std
#include <ctime>

///< boost
#include <boost/filesystem.hpp>

///< impl
#include "message_track.h"
#include "../test_driver.h"

namespace ami
{

class StMessageTrack : public MessageTrack
{
private:
    typedef std::chrono::system_clock ClockType;
    typedef ClockType::time_point TimepointType;

public:
    StMessageTrack()
    {
        weight_ = kDefaultWeight;
    }

    virtual ~StMessageTrack()
    {
        SyncToFile();
        // DeleteFileBuffer(msg_data_file_buf_);
    }

private:
    virtual ErrorCode_def DoInit(const string& track_path,
                                 const Property& request,
                                 Property& reply,
                                 size_t recorder_worker_idx)
    {
        IF_ERR_RET(MessageTrack::DoInit(track_path, request, reply, recorder_worker_idx));
        reply.SetValue(kStatusQueueIndex, (int)msg_ptr_queue_->index());

        return kSuccess;
    }

    virtual ErrorCode_def DoInit(const string& track_path,
                                 const Property& request,
                                 Property& reply)
    {
        IF_ERR_RET(MessageTrack::DoInit(track_path, request, reply));
        reply.SetValue(kStatusQueueIndex, (int)msg_ptr_queue_->index());

        return kSuccess;
    }

    virtual ErrorCode_def DoInit(const adk::ShmPointer& shm_point,
                                 size_t recorder_worker_idx)
    {
        return MessageTrack::DoInit(shm_point, recorder_worker_idx);
    }

    virtual void DoDump(std::ostream& os) const;
    virtual void DoDumpToPtree(boost::property_tree::ptree& status_tree) const;

    virtual Message::SizeType GetMessageFixLen() const
    {
        return kAppmsgMetaDataLen;
    }

    virtual bool WriteMessage(std::streambuf& o, const MsgRecord& msg_record, RecordedMsgStuff& fms)
    {
        ///每次都从头开始写，只保留最新的消息
        if (msg_data_file_buf_->GetFilebuf().pubseekpos(
                msg_data_file_header_.HeaderLength(), std::ios_base::out)
            < 0)
        {
            return false;
        }

        if (IsUseCRC())
        {
            crc_ = 0;
        }

        if (false == WriteAppMsg(o, msg_record, fms))
        {
            return false;
        }

        if (IsUseCRC() && (false == WriteCRC(o)))
        {
            return false;
        }

        /// 每个消息都直接刷到磁盘
        SyncToFile();

        return true;
    }

    virtual bool RecordOneMessage(MsgRecord& msg_record,
                                  MQMsgEntry* entry         = nullptr,
                                  adk::MPSCQueue* msg_queue = nullptr)
    {
        cur_msgdata_filepos_ = 0;

        RecordedMsgStuff* fms_ptr = nullptr;
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
        if (false == WriteMessage(*msg_data_file_buf_, msg_record, fms))
        {
            LOG_FATAL("record ack message({1}) failed.", msg_record);
            return false;
        }

        AMI_TD_JOB_BY_ENV(
            "AMI_TEST_RECORDER_ST_SLOW", 
            {
                sleep(2);
            });

        AMI_TD_JOB_BY_ENV(
            "AMI_TEST_RECORDER_ST_ULTRA_SLOW", 
            {
                sleep(1000);
            });

        FreeAllMessages();
        last_update_tp_ = ClockType::now();

        return true;
    }

    virtual void DoReleaseMessage(AmiMessage& ami_msg) const
    {
        ami_msg.dec_slave_counter_tx();
    }

    virtual bool DoSyncToFile()
    {
        if (!msg_data_file_buf_)
        {
            return true;
        }
        return msg_data_file_buf_->Sync();
    }

    virtual ErrorCode_def FilterMessage(const MsgRecord* msg_record,
                                        MQMsgEntry* entry)
    {
        return kSuccess;
    }

    virtual char GetTypeChar() const
    {
        return 's';
    }

    virtual ErrorCode_def GetLastMessageLen(const Message::SqnType& last_msg_sqn,
                                            Message::SizeType& last_msg_len) const
    {
        return kFailure;
    }

    virtual ErrorCode_def RecoverIndexDataFiles(const LostMsg&)
    {
        return kSuccess;
        ;
    }

    virtual ErrorCode_def OpenIndexDataFiles();

    virtual void ClearQueueMsgAtRecovery();

    std::string FormatLastUpdateTP() const
    {
        std::time_t last_update_tp = ClockType::to_time_t(last_update_tp_);
        char tp_output[128];
        const size_t tp_str_len =
            std::strftime(tp_output, sizeof(tp_output),
                          "%H:%M:%S", std::localtime(&last_update_tp));
        return std::string(tp_output, tp_str_len);
    }

    TimepointType last_update_tp_;
    LOG_DECLARE
};

}  // namespace ami

#endif /* AMI_TX_MESSAGE_TRACK_H_ */
