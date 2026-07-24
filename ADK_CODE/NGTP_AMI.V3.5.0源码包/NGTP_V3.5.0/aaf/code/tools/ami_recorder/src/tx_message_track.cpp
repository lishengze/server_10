/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */

///< boost
#include <boost/filesystem.hpp>

///< impl
#include "record_reader.h"
#include "tx_message_track.h"
#include "../test_driver.h"

namespace ami
{

namespace bl = boost::locale;
namespace bf = boost::filesystem;
namespace bs = boost::system;

LOG_DEFINE(ami::TxMessageTrack)

ErrorCode_def
TxMessageTrack::FilterMessage(const MsgRecord* msg_record,
                              MQMsgEntry* entry)
{
    if (msg_record->topic_sqn > last_msg_sqn_ + 1u)
    {
        if (!ignore_lost_msg_)
        {
            LOG_FATAL("data path {4}, there is a hole on tx side, range [{1},{2}) : {3}",
                      last_msg_sqn_ + 1, msg_record->topic_sqn, *msg_record, data_path_);
            return kFailure;
        }
        else
        {
            LOG_INFO("data path {4}, there is a hole on tx side, range [{1},{2}),"
                     "{3} (can be ingored 'kIgnoreLostMsg=true')",
                     last_msg_sqn_ + 1, msg_record->topic_sqn, *msg_record, data_path_);

            while (last_msg_sqn_ + 1 < msg_record->topic_sqn)
            {
                MsgRecord& place_holder = *NewPlaceHolderMsgRecord(*msg_record);
                if (false == RecordOneMessage(place_holder))
                {
                    return kFailure;
                }

                last_msg_sqn_++;
            }
        }
    }

    if (msg_record->topic_sqn <= last_msg_sqn_)
    {
        AMI_TD_LOG_INFO_RATELIMITED_VERY_LOW(
                        "filter the dup tx messages, topic_sqn = {1}, last_msg_sqn = {2}",
                        msg_record->topic_sqn, last_msg_sqn_);
        try
        {
            PushIntoRecordingMsgQ(this, entry, msg_ptr_queue_, false);
        }
        catch (const std::system_error&)
        {
            return kFailure;
        }

        filtered_msg_cnt_++;
        return kCanBeIgnored;
    }

    last_msg_sqn_ = msg_record->topic_sqn;
    return kSuccess;
}

ErrorCode_def
TxMessageTrack::GetLastMessageLen(const Message::SqnType& last_msg_sqn,
                                  Message::SizeType& last_msg_len) const
{
    RecordReader reader;
    Message::SizeType lml;
    IF_ERR_RET(reader.ReadTxHistMessage(GetTrackDataPath(), last_msg_sqn, last_msg_sqn + 1,
                                        [&lml, this](AmiMessage* ami_msg) {
                                            Message& app_msg = *(ami_msg->message());
                                            lml              = GetMessageFixLen() + app_msg.app_data_len;
                                            if (IsUseCRC())
                                            {
                                                lml += sizeof(MsgCRCType);
                                            }

                                            return kSuccess;
                                        }));
    last_msg_len = lml;
    return kSuccess;
}

ErrorCode_def
TxMessageTrack::RecoverIndexDataFiles()
{
    // recovery the ordinal index file 
    // recovery the data file
    IF_ERR_RET(MessageTrack::RecoverIndexDataFiles());

    // message loss cannot happen at the tx side
    // using the number recorded msg as the last received msg sqn;
    last_msg_sqn_ = recorded_msg_cnt_;

    bs::error_code bs_ec;
    if (!bf::is_directory(GetTrackDataPath(), bs_ec))
    {
        LOG_ERROR("{1} is not a directory.", GetTrackDataPath());
        return kFailure;
    }

    for (const auto& fe : bf::directory_iterator(GetTrackDataPath(), bs_ec))
    {
        Message::SqnType last_msg_sqn = 0;
        FileWriteBuffer* key_value_buffer = nullptr;
        ErrorCode_def ec;

        ///< StreamKey
        StreamKey str_key_value;
        ec = RecoverKeyIndexFile(fe.path(), str_key_value, last_msg_sqn, &key_value_buffer);
        if (kFailure == ec)
        {
            return ec;
        }
        else if (kSuccess == ec)
        {
            if (last_msg_sqn > 0)
            {
                // kMsgToFreeQMaxLength was the maximum index was not saved
                auto begin_sqn = last_msg_sqn;
                auto end_sqn = last_msg_sqn + 1;

                // read message from range [last_msg_sqn - kMsgToFreeQMaxLength, last_msg_sqn + 1)
                if (last_msg_sqn > kMsgToFreeQMaxLength)
                {
                    begin_sqn = last_msg_sqn - kMsgToFreeQMaxLength;
                }
                else
                {
                    begin_sqn = 1;
                }

                ///恢复last_msg_sqn_map_
                auto prev_index = last_msg_sqn;
                auto new_index = begin_sqn - 1;
                RecordReader reader;
                reader.ReadTxSTRHistMessage(
                    fe.path().parent_path(),
                    str_key_value.HashCode(),
                    begin_sqn,
                    end_sqn,
                    [&new_index](AmiMessage* ami_message) -> ErrorCode {
                        // get the actually number messages on stream
                        ++new_index;
                        return kSuccess;
                    });

                if (prev_index != new_index)
                {
                    if (TruncateIndexFile(key_value_buffer, new_index) != ErrorCode::kSuccess)
                    {
                        return ErrorCode::kFailure;
                    }
                }
            }
            continue;
        }
    }

    return kSuccess;
}

ErrorCode_def
TxMessageTrack::RecoverIndexDataFiles(const LostMsg& lost_msg_tag)
{
    if (OpenOrdinalIndexBufferWrite() != ErrorCode::kSuccess)
    {
        return ErrorCode::kFailure;
    }
    ///< 恢复数据文件
    std::string data_file_path_str = msg_data_file_buf_->GetFilePathStr();
    RecordReader reader;
    reader.ReadTxHistMessage(GetTrackDataPath(),
                             AmiRecorderBase::kBegin,
                             AmiRecorderBase::kMostRecent, // ReadTxHistMessage using ordinal_index to seek the begin messsage
                             [this](AmiMessage* ami_msg) {

                                 auto oridnal_index = OrdinalIndex(cur_msgdata_filepos_); 
                                 Message& app_msg = *(ami_msg->message());
                                 if (app_msg.msg_header.stream_id == 0
                                     || ami_msg->ami_meta_data.recorder_receive_msg_time_ns == 0)
                                 {
                                    LOG_ERROR("invalid data, stream_id = <{1}>, "
                                              "recorder_receive_msg_time_ns = <{2}>",
                                              app_msg.msg_header.stream_id,
                                              ami_msg->ami_meta_data.recorder_receive_msg_time_ns);
                                    return ErrorCode::kFailure;
                                 }

                                 cur_msgdata_filepos_ += (GetMessageFixLen() + app_msg.app_data_len);
                                 if (IsUseCRC())
                                 {
                                     cur_msgdata_filepos_ += sizeof(MsgCRCType);
                                 }

                                 recorded_msg_cnt_++;

                                 ///< StreamKey
                                 auto ret = UpdateLastMsgSqn(StreamKey(app_msg.msg_header.stream_id),
                                                  oridnal_index);
                                 if (ret != ErrorCode::kSuccess)
                                 {
                                    return ErrorCode::kFailure;
                                 }

                                 if (RewriteIndexFile(ordinal_index_write_, oridnal_index) != ErrorCode::kSuccess)
                                 {
                                     LOG_ERROR("write on file <{1}> failed, errno <{2}>, desc <{3}>",
                                               data_path_ + "/index", errno, ::strerror(errno));
                                     return ErrorCode::kFailure;
                                 }

                                 return kSuccess;
                             });

    if (RewriteIndexFileDone() != ErrorCode::kSuccess)
    {
        return ErrorCode::kFailure;
    }

    if (!DePreallocateDataFile())
    {
        return kFailure;
    }
    msgdata_len_inc_prealloc_ = cur_msgdata_filepos_;

    const auto phy_file_pos = GetMsgDataPhyFilePos(cur_msgdata_filepos_,
                                                   msg_data_file_header_)
                                  .second;
    if (!msg_data_file_buf_->Truncate(phy_file_pos)
        || (phy_file_pos != static_cast<FileWriteBuffer::FileBuffer&>(*msg_data_file_buf_).pubseekpos(phy_file_pos)))
    {
        LOG_ERROR("truncate to next msg(sqn={1})'s data pos(={2}) "
                  "of file '{3}' failed",
                  recorded_msg_cnt_ + 1,
                  phy_file_pos,
                  data_file_path_str);
        return kFailure;
    }
    else
    {
        LOG_INFO("truncate to next msg(sqn={1})'s data pos(={2}) "
                 "of file '{3}' ok",
                 recorded_msg_cnt_ + 1,
                 phy_file_pos,
                 data_file_path_str);
    }

    ///< 恢复索引文件
    std::string index_file_path_str = ordinal_index_file_buf_->GetFilePathStr();
    last_msg_sqn_                   = recorded_msg_cnt_;
    IF_ERR_RET(RecoverIndexFile(ordinal_index_file_buf_, last_msg_sqn_, lost_msg_tag));

    ///< 恢复关键字索引文件
    bs::error_code bs_ec;
    if (!bf::is_directory(GetTrackDataPath(), bs_ec))
    {
        LOG_ERROR("{1} is not a directory.", GetTrackDataPath());
        return kFailure;
    }

    for (const auto& fe : bf::directory_iterator(GetTrackDataPath(), bs_ec))
    {
        ErrorCode_def ec;

        ///< StreamKey
        ec = RecoverKeyIndexFile(fe.path(), StreamKey(), lost_msg_tag);
        if (kFailure == ec)
        {
            return ec;
        }
        else if (kSuccess == ec)
        {
            continue;
        }
    }

    return kSuccess;
}

void TxMessageTrack::ClearQueueMsgAtRecovery()
{
    if (msgdata_filebuf_to_recover_ == nullptr)
    {
        return;
    }

    // FIXME: dump the queue cursors
    // add the following code to prevent dead locking
    if (msg_ptr_queue_ != nullptr)
    {
        msg_ptr_queue_->Consistent();    
    }

    uint32_t cnt = 0;
    while (msg_ptr_queue_ != nullptr)
    {
        adk::Entry* entry = nullptr;
        auto ec = msg_ptr_queue_->WaitEntry(&entry);
        if (ec == adk::ErrorCode::kSuccess)
        {
            msg_ptr_queue_->FreeEntry(entry);
            ++cnt;
        }
        else
        {
            break;
        }
    }
    LOG_INFO("clear the track: {1} msg queue at recovery mode, clear msg cnt: {2}",
              GetTrackPath(),
              cnt);
}

}  // namespace ami
