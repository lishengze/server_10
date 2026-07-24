/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */
///< boost
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/locale/format.hpp>

///< impl header
#include "record_reader.h"
#include "recorder.h"
#include "rx_message_track.h"

namespace ami
{

namespace bl = boost::locale;
namespace bf = boost::filesystem;
namespace bs = boost::system;

LOG_DEFINE(ami::RxMessageTrack)

constexpr const char* RxMessageTrack::kRepairQueuePrefix;

// bootstrap scenario
// DoInit and then Start
ErrorCode_def RxMessageTrack::DoInit(const string& track_path, const Property& request,
                                     Property& reply, size_t recorder_worker_idx)
{
    // common method to:
    //      create directories
    //      create msg data file and ordinal index file, link ordinal index file and msg data file
    //      write file handler
    //      create msg pointer queue (zero copy transfer messages between libami and recorder process)
    //      push msg data file buffer into the TrackInfo share memory queue
    //      
    // note: the partial index file was create when the first message was received
    //      
    IF_ERR_RET(MessageTrack::DoInit(track_path, request, reply, recorder_worker_idx));
    // reply the realtime queue index to libami
    reply.SetValue(kOriginQueueIndex, (int)msg_ptr_queue_->index());    

    auto recorder_mq_manager = Recorder::GetMQManager();
    assert(recorder_mq_manager);
    repair_msg_queue_ = recorder_mq_manager->CreateSharedMPSCQueue(GetRepairQName(), msg_queue_size_);
    if (nullptr == repair_msg_queue_)
    {
        LOG_ERROR("create repair message queue '{1}' failed.",
                  GetRepairQName());
        return kFailure;
    }
    else
    {
        LOG_INFO("create repair message queue '{1}' ok.",
                 GetRepairQName());
    }

    // reply the repair queue index to libami
    reply.SetValue(kRepairQueueIndex, (int)repair_msg_queue_->index());
    return kSuccess;
}

// call the parent RecoverIndexDataFiles(),  recovery the ordinal_index (primary index) and data file
// recover the partial index
// this method was used without system crash
ErrorCode_def RxMessageTrack::RecoverIndexDataFiles()
{
    // call the common method to recover the ordinal_index and msg data file
    IF_ERR_RET(MessageTrack::RecoverIndexDataFiles());

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

        ///< TransportKey
        TransportKey tp_key_value;
        // last_msg_sqn output from here is the index, not message sqn
        ec = RecoverKeyIndexFile(fe.path(), tp_key_value, last_msg_sqn, &key_value_buffer);    
        if (kFailure == ec)
        {
            return ec;
        }
        else if (kSuccess == ec)
        {
            if (last_msg_sqn > 0)
            {
                AMI_TD_PARAM_JOB_BY_ENV(
                "AMI_TEST_MAKE_CRC_ERROR",
                [](char* env_str){
                    if (std::string("RxTNP") == env_str)
                    {
                        g_simulate_crc_error = true;
                    }
                });


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
                auto prev_last_msg_sqn = last_msg_sqn;
                auto prev_index = last_msg_sqn;
                auto new_index = begin_sqn - 1;
                RecordReader reader;
                int32_t ec = reader.ReadRxTNPHistMessage(
                    fe.path().parent_path(),
                    tp_key_value.HashCode(),
                    begin_sqn,
                    end_sqn,
                    [&last_msg_sqn, &new_index](AmiMessage* ami_message) -> ErrorCode {
                        // get the actually message sqn on topic
                        last_msg_sqn = ami_message->message()->topic_seq_num(); 
                        ++new_index;
                        return kSuccess;
                    });
                if (ec != ErrorCode::kSuccess)
                {
                    // if crc error happened, return failure
                    // keep the data/index file without change
                    if (reader.is_crc_error())
                    {
                        LOG_ERROR("reading the RxTransport message to recover the last transport index failed, "
                            "path <{1}>, last_msg_sqn <{2}>, read range [{3},{4}), transport_id <{5}>",
                            fe.path().string(), last_msg_sqn, begin_sqn, end_sqn, 
                            tp_key_value.HashCode());
                        return ErrorCode::kFailure;
                    }
                }

                if (prev_last_msg_sqn != last_msg_sqn)
                {
                    // message loss scenario
                    LOG_INFO("msg index {1} is not equal to msg sqn {2}, file '{3}'",
                             prev_last_msg_sqn, last_msg_sqn, fe.path().string());
                }

                if (prev_index != new_index)
                {
                    if (TruncateIndexFile(key_value_buffer, new_index) != ErrorCode::kSuccess)
                    {
                        return ErrorCode::kFailure;
                    }
                }
            }

            // save the message sqn on topic
            // note: recorder use this sqn to detect message loss and to filter message
            last_msg_sqn_map_.emplace(std::make_pair(tp_key_value.key_value, last_msg_sqn));
            continue;
        }
        // else kTryAgain, this file was not a transport index

        ///< EndpointKey
        EndpointKey ep_key_value;
        key_value_buffer = nullptr;
        ec = RecoverKeyIndexFile(fe.path(), ep_key_value, last_msg_sqn, &key_value_buffer);
        if (kFailure == ec)
        {
            return ec;
        }
        else if (kSuccess == ec)
        {
            if (last_msg_sqn > 0)
            {
                AMI_TD_PARAM_JOB_BY_ENV(
                "AMI_TEST_MAKE_CRC_ERROR",
                [](char* env_str){
                    if (std::string("RxEDP") == env_str)
                    {
                        g_simulate_crc_error = true;
                    }
                });

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
                int32_t ec = reader.ReadRxEDPHistMessage(
                    fe.path().parent_path(),
                    ep_key_value.HashCode(),
                    begin_sqn,
                    end_sqn,
                    [&new_index](AmiMessage* ami_message) -> ErrorCode {
                        // get the actually number messages on endpoint
                        ++new_index;
                        return kSuccess;
                    });
                if (ec != ErrorCode::kSuccess)
                {
                    // if crc error happened, return failure
                    // keep the data/index file without change
                    if (reader.is_crc_error())
                    {
                        LOG_ERROR("reading the RxEndpoint message to recover the last Endpoint index failed, "
                            "path <{1}>, last_msg_sqn <{2}>, read range [{3},{4}), endpoint_id <{5}>",
                            fe.path().string(), last_msg_sqn,
                            begin_sqn, end_sqn, ep_key_value.HashCode());
                        return ErrorCode::kFailure;
                    }
                }

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
        // else kTryAgain, this file was not a endpoint index

        ///< StreamKey
        StreamKey str_key_value;
        key_value_buffer = nullptr;
        ec = RecoverKeyIndexFile(fe.path(), str_key_value, last_msg_sqn, &key_value_buffer);
        if (kFailure == ec)
        {
            return ec;
        }
        else if (kSuccess == ec)
        {
            if (last_msg_sqn > 0)
            {
                AMI_TD_PARAM_JOB_BY_ENV(
                "AMI_TEST_MAKE_CRC_ERROR",
                [](char* env_str){
                    if (std::string("RxSTR") == env_str)
                    {
                        g_simulate_crc_error = true;
                    }
                });
                
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
                int32_t ec = reader.ReadRxSTRHistMessage(
                    fe.path().parent_path(),
                    str_key_value.HashCode(),
                    begin_sqn,
                    end_sqn,
                    [&new_index](AmiMessage* ami_message) -> ErrorCode {
                        // get the actually number messages on stream
                        ++new_index;
                        return kSuccess;
                    });
                if (ec != ErrorCode::kSuccess)
                {
                    // if crc error happened, return failure
                    // keep the data/index file without change
                    if (reader.is_crc_error())
                    {
                        LOG_ERROR("read the message on rx stream to recover the stream index failed, "
                            "path <{1}>, last_msg_sqn <{2}>, read range [{3},{4}), stream_id <{5}>",
                            fe.path().string(), last_msg_sqn, begin_sqn, end_sqn, 
                            str_key_value.HashCode());
                        return ErrorCode::kFailure;
                    }
                }

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
        // else kTryAgain, this file was not a stream index
    }

    return kSuccess;
}

// system crash/reboot scenario
ErrorCode_def RxMessageTrack::RecoverIndexDataFiles(const LostMsg& lost_msg_tag)
{
    if (OpenOrdinalIndexBufferWrite() != ErrorCode::kSuccess)
    {
        return ErrorCode::kFailure;
    }

    std::map<int32_t, uint64_t> msg_counter_map;
    ///< 遍历消息准备恢复的数据
    std::string data_file_path_str = msg_data_file_buf_->GetFilePathStr();
    RecordReader reader;
    reader.ReadRxHistMessage(GetTrackDataPath(),
                             AmiRecorderBase::kBegin, AmiRecorderBase::kMostRecent,
                             [this, &msg_counter_map](AmiMessage* ami_msg) {
                                
                                if (ami_msg->ami_meta_data.transport_id == 0
                                      || ami_msg->ami_meta_data.endpoint_id == 0
                                      || ami_msg->ami_meta_data.recorder_receive_msg_time_ns == 0)
                                 {
                                    LOG_ERROR("invalid data, transport_id = <{1}>, endpoint_id = <{2}>, "
                                              "recorder_receive_msg_time_ns = <{3}>",
                                              ami_msg->ami_meta_data.transport_id,
                                              ami_msg->ami_meta_data.endpoint_id,
                                              ami_msg->ami_meta_data.recorder_receive_msg_time_ns);
                                    return ErrorCode::kFailure;
                                 }

                                 auto oridnal_index = OrdinalIndex(cur_msgdata_filepos_);
                                 Message& app_msg = *(ami_msg->message());
                                 cur_msgdata_filepos_ += GetMessageFixLen() + app_msg.app_data_len;
                                 if (IsUseCRC())
                                 {
                                     cur_msgdata_filepos_ += sizeof(MsgCRCType);
                                 }

                                 recorded_msg_cnt_++;

                                 const auto transport_id = ami_msg->ami_meta_data.transport_id;
                                 if (last_msg_sqn_map_.count(transport_id))
                                 {
                                     last_msg_sqn_map_[transport_id] = app_msg.topic_sqn;
                                     ++msg_counter_map[transport_id];
                                 }
                                 else
                                 {
                                     last_msg_sqn_map_.emplace(std::make_pair(transport_id,
                                                                              app_msg.topic_sqn));
                                     msg_counter_map[transport_id] = 1;
                                 }

                                 // note: the following hash map maintain the index not sqn
                                 
                                 ///< TransportKey
                                 // maintain the index on the transport
                                 auto ret = UpdateLastMsgSqn(
                                     TransportKey(ami_msg->ami_meta_data.transport_id), oridnal_index);
                                 if (ret != ErrorCode::kSuccess)
                                 {
                                    return ErrorCode::kFailure;
                                 }

                                 ///< EndpointKey
                                 // maintain the index on the endpoint
                                 ret = UpdateLastMsgSqn(
                                     EndpointKey(ami_msg->ami_meta_data.endpoint_id), oridnal_index);
                                 if (ret != ErrorCode::kSuccess)
                                 {
                                    return ErrorCode::kFailure;
                                 }

                                 ///< StreamKey
                                 // maintain the index on the stream
                                 ret = UpdateLastMsgSqn(
                                     StreamKey(ami_msg->message()->msg_header.stream_id), oridnal_index);
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

    LOG_INFO("the recorder rx recovery status: total messages <{1}>", recorded_msg_cnt_);
    for (auto& node : last_msg_sqn_map_)
    {
        auto counter = msg_counter_map[node.first];
        LOG_INFO("transport_id <{1}>, last topic_sqn <{2}>, number messages <{3}>",
                 node.first, node.second, counter);
    }

    ///< 恢复数据文件
    if (!DePreallocateDataFile())
    {
        return kFailure;
    }

    msgdata_len_inc_prealloc_ = cur_msgdata_filepos_;
    const auto phy_file_pos   = GetMsgDataPhyFilePos(cur_msgdata_filepos_,
                                                   msg_data_file_header_)
                                  .second;
    const auto next_msg_sqn = recorded_msg_cnt_ + 1;
    if (!msg_data_file_buf_->Truncate(phy_file_pos)
        || (phy_file_pos != static_cast<FileWriteBuffer::FileBuffer&>(*msg_data_file_buf_).pubseekpos(phy_file_pos)))
    {
        LOG_ERROR("rx side, truncate to next msg(total order sqn={1})'s data file pos(={2}) "
                  "of file '{3}' failed",
                  next_msg_sqn, phy_file_pos, data_file_path_str);
        return kFailure;
    }
    else
    {
        LOG_INFO("truncate to next msg(total order sqn={1})'s data file pos(={2}) "
                 "of file '{3}' ok",
                 next_msg_sqn, phy_file_pos, data_file_path_str);
    }

    ///< 恢复索引文件
    std::string index_file_path_str = ordinal_index_file_buf_->GetFilePathStr();
    FilePosType next_msg_index_pos  = OrdinalIndex::GetMsgIndexPos(next_msg_sqn);
    if (!ordinal_index_file_buf_->Truncate(next_msg_index_pos)
        || (next_msg_index_pos != static_cast<FileWriteBuffer::FileBuffer&>(*ordinal_index_file_buf_).pubseekpos(next_msg_index_pos)))
    {
        LOG_ERROR("truncate to next msg(total order sqn={1})'s "
                  "index file pos(={2}) of file '{3}' failed",
                  next_msg_sqn, next_msg_index_pos, index_file_path_str);
        return kFailure;
    }
    else
    {
        LOG_INFO("truncate to next msg(total order sqn={1})'s "
                 "index file pos(={2}) of file '{3}' ok",
                 next_msg_sqn, next_msg_index_pos, index_file_path_str);
    }

    ///< 恢复关键字索引文件
    bs::error_code bs_ec;
    if (!bf::is_directory(GetTrackDataPath(), bs_ec))
    {
        LOG_ERROR("{1} is not a directory.", GetTrackDataPath());
        return kFailure;
    }

    // recovery other index, by ordinal_index 
    // note: during the normal processing, flushing the ordinal_index at the end
    for (const auto& fe : bf::directory_iterator(GetTrackDataPath(), bs_ec))
    {
        ErrorCode_def ec;

        ///< TransportKey
        // to recovery as Transport index
        ec = RecoverKeyIndexFile(fe.path(), TransportKey(), lost_msg_tag);
        if (kFailure == ec)
        {
            return ec;
        }
        else if (kSuccess == ec)
        {
            continue;
        }

        ///< EndpointKey
        // to recovery as Endpoint index
        ec = RecoverKeyIndexFile(fe.path(), EndpointKey(), lost_msg_tag);
        if (kFailure == ec)
        {
            return ec;
        }
        else if (kSuccess == ec)
        {
            continue;
        }

        ///< StreamKey
        // to recovery as Stream index
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

// recovery senario, cooperate with 3 parameters MessageTrack::DoInit
//        RxMessageTrack::DoInit(2 params) -> Start() -> Stop() 
//                  -> CreateMessageChannel -> MessageTrack::DoInit(3 params) -> Start()
// shm_point was poped from the TrackInfo share memory queue
ErrorCode_def RxMessageTrack::DoInit(const adk::ShmPointer& shm_point,
                                     size_t recorder_worker_idx)
{
    // create msg data file buffer by shm_point
    // init path_ and data_path_ by shm_point
    // create data file buf and ordinal_index file buffer
    // attach message ptr queue, consistent the producer and consumer side
    IF_ERR_RET(MessageTrack::DoInit(shm_point, recorder_worker_idx));   // if the "rx/ tx/" directory was removed, return from here
    
    // seek ordinal_index file buffer
    // seek msg data file buffer
    // create partial index file buffer and seek the file pos
    // recover the topic sqn on RxTransport, recorder use this sqn to detect message loss and filter message
    // note: no truncate
    IF_ERR_RET(RecoverIndexDataFiles());

    // recorded_msg_cnt_ was updated in RecoverIndexDataFiles
    msg_cnt_before_process_mq_ = recorded_msg_cnt_;

    auto recorder_mq_manager = Recorder::GetMQManager();
    assert(recorder_mq_manager);
    repair_msg_queue_ = recorder_mq_manager->AttachSharedMPSCQueue(GetRepairQName());
    if (nullptr == repair_msg_queue_)
    {
        LOG_ERROR("attach repair message queue '{1}' failed.",
                  GetRepairQName());
        return kFailure;
    }
    else
    {
        // consistent the producer and consumer side
        repair_msg_queue_->Consistent();
        LOG_INFO("attach repair message queue '{1}' ok.",
                 GetRepairQName());
    }

    return kSuccess;
}

ErrorCode_def
RxMessageTrack::FilterMessage(const MsgRecord* msg_record,
                              MQMsgEntry* entry)
{
    const auto transport_id = msg_record->transport_id;
    const auto cur_msg_sqn  = msg_record->topic_sqn;

    if (last_msg_sqn_map_.count(transport_id))
    {
        uint64_t& last_msg_sqn = last_msg_sqn_map_.at(transport_id);
        if (cur_msg_sqn > last_msg_sqn + 1)
        {
            LOG_INFO("there is a hole [{1},{2}) on the transport_id {3}, msg_record {4}",
                     last_msg_sqn + 1, cur_msg_sqn,
                     transport_id, *msg_record);

            repairing_     = true;
            pending_entry_ = entry;
            return Repair();
        }
        else if (cur_msg_sqn <= last_msg_sqn)
        {
            AMI_TD_LOG_INFO_RATELIMITED_VERY_LOW(
                        "filter the dup realtime messages, cur_msg_sqn = {1}, last_msg_sqn = {2}", 
                        cur_msg_sqn, last_msg_sqn);
            try
            {
                PushIntoRecordingMsgQ(this, entry, msg_ptr_queue_, false);
            }
            catch (const std::system_error& e)
            {
                return kFailure;
            }

            filtered_msg_cnt_++;
            return kCanBeIgnored;
        }
        else
        {
            last_msg_sqn = cur_msg_sqn;
            return kSuccess;
        }
    }
    else
    {  //该transport第一次出现
        if (cur_msg_sqn > AmiRecorderBase::kBegin)
        {
            LOG_INFO("there is a hole [{1}, {2}) on the transport_id {3}, msg_record {4}",
                     1, cur_msg_sqn, transport_id, *msg_record);

            last_msg_sqn_map_.emplace(std::make_pair(transport_id,
                                                     AmiRecorderBase::kBegin - 1));
            repairing_     = true;
            pending_entry_ = entry;
            return Repair();
        }
        else if (cur_msg_sqn < AmiRecorderBase::kBegin)
        {
            LOG_ERROR("receive unexpected msg, msg on receive queue ({4}), expect msg (1) "
                      "queue (name={1}, index={2}, len={3}), transport_id {5}",
                      msg_ptr_queue_->name(),
                      msg_ptr_queue_->index(),
                      msg_ptr_queue_->length(),
                      cur_msg_sqn,
                      transport_id);

            return kFailure;
        }
        else /*==AmiRecorderBase::kBegin*/
        {
            last_msg_sqn_map_.emplace(std::make_pair(transport_id,
                                                     cur_msg_sqn));
            LOG_INFO("the first message (sqn=1) was received, transport_id {1}", transport_id);
            return kSuccess;
        }
    }
}

ErrorCode_def RxMessageTrack::Repair()
{
    adk::Entry* repairing_entry;
    adk::ErrorCode_def ec = repair_msg_queue_->WaitEntry(&repairing_entry);
    if (ec == adk::ErrorCode::kSuccess)
    {
        if (MQMsgEntry::IsSignalRecord(repairing_entry,
                                       MsgRecord::kRepairWithPlaceHolder))
        {  //ami重传模块无法补全缺失的消息，通知recorder使用占位消息来补全
            MQMsgEntry* repairing_msg_entry =
                MQMsgEntry::ConvertFromAdkEntry(repairing_entry);
            const uint64_t place_hold_cnt =
                repairing_msg_entry->msg_record.GetPlaceHolderCnt();
            try
            {
                PushIntoRecordingMsgQ(this, repairing_msg_entry,
                                      repair_msg_queue_, false);
            }
            catch (const std::system_error&)
            {
                return kFailure;
            }

            const auto expected_transport_id = pending_entry_->msg_record.transport_id;
            const auto repair_begin           = last_msg_sqn_map_.at(expected_transport_id) + 1;
            const auto repair_end             = repair_begin + place_hold_cnt;

            LOG_INFO(
                "received a place holder signal record "
                "from retrans queue (name={1}, index={2}, len={3}), "
                "loss range [{4}, {5}), transport_id={6}",
                repair_msg_queue_->name(),
                repair_msg_queue_->index(),
                repair_msg_queue_->length(),
                repair_begin, repair_end, expected_transport_id);

            // 1. generating place holder is too slow in scenarios that have large lost range
            // 2. when the application request for history, they using continueous sequence number
            // for (auto lost_sqn = repair_begin; lost_sqn < repair_end; lost_sqn++)
            // {
            //     const MsgRecord& place_holder =
            //         *NewPlaceHolderMsgRecord(pending_entry_->msg_record);
            //     if (false == RecordOneMessage(place_holder))
            //     { return kFailure; }
            // }

            uint64_t& last_msg_sqn_ref = last_msg_sqn_map_.at(expected_transport_id);
            last_msg_sqn_ref = repair_end - 1;

            uint64_t pending_entry_sqn = pending_entry_->msg_record.topic_sqn;
            if (pending_entry_sqn == repair_end)
            {
                if (false == RecordOneMessage(pending_entry_->msg_record, pending_entry_, msg_ptr_queue_))
                {
                    return kFailure;
                }

                last_msg_sqn_ref = pending_entry_sqn;
                pending_entry_ = nullptr;
                repairing_     = false;
            }
            else
            {
                LOG_INFO("receive partial place holder signal record, "
                          "transport <{1}>, "
                          "left loss range repair_end [{2}, {3})",
                          expected_transport_id,
                          repair_end,
                          pending_entry_sqn);
            }

            return kRepairing;
        }

        MQMsgEntry* repairing_msg_entry =
            MQMsgEntry::InitFromAdkEntry(repairing_entry, app_msg_mp_manager_);
        LOG_TRACE("fetch repairing message record({1}) "
                  "from retrans queue(name={2}, index={3}, len={4})",
                  repairing_msg_entry->msg_record,
                  repair_msg_queue_->name(),
                  repair_msg_queue_->index(),
                  repair_msg_queue_->length());

        const int32_t  transport_id          = repairing_msg_entry->msg_record.transport_id;
        const uint64_t msg_sqn               = repairing_msg_entry->msg_record.topic_sqn;
        const int32_t  expected_transport_id = pending_entry_->msg_record.transport_id;
        const uint64_t expected_sqn          = last_msg_sqn_map_.at(expected_transport_id) + 1;

        if ((transport_id != expected_transport_id)
            || (msg_sqn > expected_sqn))
        {
            auto* ami_msg = repairing_msg_entry->GetOrigAmiMsg(app_msg_mp_manager_);
            LOG_ERROR("get unexpected msg, msg on retrans queue, (tp_id={1}, msg_sqn={2}) != expected msg (tp_id={3}, msg_sqn={4}), "
                      "retrans queue (name={5}, index={6}, len={7}), the msg reference (rx={8}, tx={9}), "
                      "topic_sqn ({10}) ",
                      transport_id, 
                      msg_sqn,
                      expected_transport_id,
                      expected_sqn,
                      repair_msg_queue_->name(),
                      repair_msg_queue_->index(),
                      repair_msg_queue_->length(),
                      ami_msg->ami_meta_data.ref_cnt.slave_counter_rx,
                      ami_msg->ami_meta_data.ref_cnt.slave_counter_tx,
                      ami_msg->message()->topic_sqn);

        #ifdef __AMI_TEST_FRAMEWORK__
            abort();
        #endif
            return kFailure;
        }
        else
        {
            assert(msg_sqn <= expected_sqn);
            if (msg_sqn < expected_sqn)
            {
                AMI_TD_LOG_INFO_RATELIMITED_VERY_LOW(
                        "filter the dup repair messages, msg_sqn = {1}, expected_sqn = {2}", 
                        msg_sqn, expected_sqn);
                // duplicated repair messages
                // eg. before the recorder was killed, the messages ware flushed into the data file
                //     but the message buffers ware still on the message queue.
                try
                {
                    PushIntoRecordingMsgQ(this, repairing_msg_entry,
                                          repair_msg_queue_,-
                                          // set false to bypass the stats on recorded_msg_cnt_-
                                          false);
                    ++filtered_msg_cnt_repair_;
                }
                catch (const std::system_error&)
                {
                    return kFailure;
                }
                // notify the job framework to fetch the next message without decreasing the quota
                return kCanBeIgnored;
            }

            AMI_TD_JOB_BY_ENV_ARG_THRESHOLD("AMI_TEST_RECORDER_REPAIR_STOP_AT",
                std::string("rx"), 
                {
                    sleep(10000);
                }
            );

            if (false == RecordOneMessage(repairing_msg_entry->msg_record, repairing_msg_entry, repair_msg_queue_))
            {
                return kFailure;
            }

            uint64_t& last_msg_sqn_ref = last_msg_sqn_map_.at(transport_id);
            last_msg_sqn_ref = msg_sqn;
            uint64_t pending_entry_sqn = pending_entry_->msg_record.topic_sqn;
            if (pending_entry_sqn == msg_sqn + 1)
            {
                if (false == RecordOneMessage(pending_entry_->msg_record, pending_entry_, msg_ptr_queue_))
                {
                    return kFailure;
                }

                last_msg_sqn_ref = pending_entry_sqn;
                pending_entry_   = nullptr;
                repairing_       = false;
            }

            return kRepairing;
        }
    }
    else
    {
        ///恢复模式下，如果没有消息了，则直接结束
        if (msgdata_filebuf_to_recover_ && !recovery_ok_)
        {
            const auto expected_transport_id  = pending_entry_->msg_record.transport_id;
            const auto expected_msg_sqn       = pending_entry_->msg_record.topic_sqn;
            const uint64_t last_msg_sqn       = last_msg_sqn_map_.at(expected_transport_id);

            LOG_INFO(
                "recovery finished, the loss range "
                "({1}, {2}] of transport_id={3} was not complete, "
                "total <{4}> messages received from rx side message queue <{5}>",
                last_msg_sqn, expected_msg_sqn, expected_transport_id,
                recorded_msg_cnt_ - msg_cnt_before_process_mq_,
                repair_msg_queue_->name());

            recovery_ok_ = true;
        }

        if (pending_entry_ != nullptr)
        {
            const auto expected_transport_id  = pending_entry_->msg_record.transport_id;
            const auto expected_msg_sqn       = pending_entry_->msg_record.topic_sqn;
            const uint64_t last_msg_sqn       = last_msg_sqn_map_.at(expected_transport_id);
            INV_LOG_INFO(inv_logger_,
                         "can not receive any messages "
                         "from retrans queue (name={1}, index={2}, len={3}), "
                         "wait for the left loss range [{4},{5}), transport_id {6}",
                         repair_msg_queue_->name(),
                         repair_msg_queue_->index(),
                         repair_msg_queue_->length(),
                         last_msg_sqn + 1, expected_msg_sqn - 1,
                         expected_transport_id);
        }
        else
        {
            INV_LOG_INFO(inv_logger_,
                         "can not receive any messages "
                         "from retrans queue (name={1}, index={2}, len={3})",
                         repair_msg_queue_->name(),
                         repair_msg_queue_->index(),
                         repair_msg_queue_->length());
        }
        can_quit_elegantly_ = true;
        return kTryAgain;
    }
}

ErrorCode_def
RxMessageTrack::GetLastMessageLen(const Message::SqnType& last_msg_sqn,
                                  Message::SizeType& last_msg_len) const
{
    RecordReader reader;
    Message::SizeType lml;
    IF_ERR_RET(reader.ReadRxHistMessage(GetTrackDataPath(), last_msg_sqn, last_msg_sqn + 1,
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

void RxMessageTrack::ClearQueueMsgAtRecovery()
{
    if (msgdata_filebuf_to_recover_ == nullptr)
    {
        return;
    }

    // FIXME: dump the queue cursors
    if (repair_msg_queue_ != nullptr)
    {
        repair_msg_queue_->Consistent();    
    }
    
    uint32_t cnt = 0;
    while (repair_msg_queue_ != nullptr)
    {
        adk::Entry* entry = nullptr;
        auto ec = repair_msg_queue_->WaitEntry(&entry);
        if (ec == adk::ErrorCode::kSuccess)
        {
            repair_msg_queue_->FreeEntry(entry);
            ++cnt;
        }
        else
        {
            break;
        }
    }
    LOG_INFO("clear the track: {1} repair queue at recovery mode, clear msg cnt: {2}",
              GetTrackPath(),
              cnt);
    // FIXME: dump the queue cursors
    // the pending_entry_ was poped from the queue, but not free
    if (msg_ptr_queue_ != nullptr)
    {
        msg_ptr_queue_->Consistent();    
    }

    cnt = 0;
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

    if (repairing_ || pending_entry_ != nullptr)
    {
        LOG_INFO("reset the track {1} repairing status", GetTrackPath());
        repairing_ = false;
        pending_entry_ = nullptr;
    }
}

}  // namespace ami
