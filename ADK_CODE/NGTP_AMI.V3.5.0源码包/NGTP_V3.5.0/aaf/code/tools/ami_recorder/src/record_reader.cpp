/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */

///< cpp std
#include <cassert>

///< impl
#include "record_reader.h"
#include "recorder.h"

namespace ami
{

LOG_DEFINE(ami::RecordReader)

namespace bf = boost::filesystem;
namespace bs = boost::system;
namespace bl = boost::locale;

#ifdef __AMI_TEST_FRAMEWORK__
bool g_simulate_crc_error = false;
uint32_t g_simulate_crc_error_count = 1;
#endif

Message::SqnType RecordReader::GetHistMsgCnt(const bf::path& track_data_path)
{
    try
    {
        bf::path file_path(GetOrdinalIndexFilePath(track_data_path.string()));
        if (bf::exists(file_path) && bf::is_regular_file(file_path))
        {
            const auto fsize = bf::file_size(file_path);
            return OrdinalIndex::GetMsgSqn(fsize);
        }
    }
    catch (const bf::filesystem_error& e)
    {
    }

    return AmiRecorderBase::kMostRecent;
}

template <typename KeyType>
Message::SqnType RecordReader::GetHisMsgCntByKey(const std::string& track_data_path,
                                                 const KeyType& key_value)
{
    try
    {
        bf::path file_path(GetKeyindexFilePath(track_data_path, key_value));
        if (bf::exists(file_path) && bf::is_regular_file(file_path))
        {
            const auto fsize = bf::file_size(file_path);
            return OrdinalIndex::GetMsgSqn(fsize);
        }
    }
    catch (const bf::filesystem_error& e)
    {
    }

    return AmiRecorderBase::kMostRecent;
}

Message::SqnType RecordReader::GetTxSTRHistMsgCnt(const bf::path& track_data_path,
                                                  const AmiMetaData::IDType& stream_id)
{
    return GetHisMsgCntByKey(track_data_path.string(),
                             StreamKey(stream_id));
}

Message::SqnType RecordReader::GetRxSTRHistMsgCnt(const bf::path& track_data_path,
                                                  const AmiMetaData::IDType& stream_id)
{
    return GetHisMsgCntByKey(track_data_path.string(),
                             StreamKey(stream_id));
}

Message::SqnType RecordReader::GetRxTNPHistMsgCnt(const bf::path& track_data_path,
                                                  const AmiMetaData::IDType& transport_id)
{
    return GetHisMsgCntByKey(track_data_path.string(),
                             TransportKey(transport_id));
}

Message::SqnType RecordReader::GetRxEDPHistMsgCnt(const bf::path& track_data_path,
                                                  const AmiMetaData::IDType& endpoint_id)
{
    return GetHisMsgCntByKey(track_data_path.string(),
                             EndpointKey(endpoint_id));
}

Message::SqnType RecordReader::GetRxHistMessageCnt(const boost::filesystem::path& track_data_path)
{
    return GetHistMsgCnt(track_data_path);
}

template <typename TrackType>
ErrorCode_def RecordReader::ReadHistMessage(const std::string& track_data_path,
                                            const Message::SqnType& begin,
                                            const Message::SqnType& end,
                                            const OnAMIMessageType& on_hist_msg,
                                            const RMPropType& msg_prop)
{
    {
        bs::error_code ec;
        if (!bf::exists(track_data_path, ec))
        {
            LOG_ERROR("track <{1}> was not exist", track_data_path);
            return kFailure;
        }
        // tracke exists 
    }

    if (!CheckBeginEndValidation(begin, end))
    {
        return kFailure;
    }

    auto begin_              = begin;
    auto end_                = end;
    bool read_to_most_recent = false;

    if (AmiRecorderBase::kMostRecent == begin)
    {
        begin_ = GetHistMsgCnt(track_data_path);
        if (AmiRecorderBase::kMostRecent == begin_)
        {
            // FIXME: distinguish file not exist and not records
            LOG_INFO_RATELIMITED_VERY_LOW("{1} has NO recorded message", track_data_path);
            return kSuccess;
        }

        end_                = begin_ + 1;
        read_to_most_recent = true;
    }
    else if (AmiRecorderBase::kMostRecent == end)
    {
        end_ = GetHistMsgCnt(track_data_path);
        if (AmiRecorderBase::kMostRecent == end_)
        {
            LOG_INFO_RATELIMITED_VERY_LOW("{1} has NO recorded message", track_data_path);
            return kSuccess;
        }

        end_++;

        if (end_ < begin_)
        {
            LOG_ERROR("{1} has NO recorded message from begin(={2})",
                      track_data_path, begin_);
            return kFailure;
        }

        read_to_most_recent = true;
    }
    else if (end == begin)
    {
        return kSuccess;
    }

    ///读索引
    bf::path ordinal_index_file_path(GetOrdinalIndexFilePath(track_data_path));
    auto begin_index_pos = OrdinalIndex::GetMsgIndexPos(begin_);
    FileSizeType ordinal_index_file_size;
    FilebufPtrType index_filebuf;
    if (!(index_filebuf = OpenSeekIndexFile(ordinal_index_file_path,
                                            begin_index_pos,
                                            ordinal_index_file_size)))
    {
        return kFailure;
    }

    OrdinalIndex begin_ordinal_index;
    if (!RecordReader::ReadOrdinalIndex(*index_filebuf, begin_ordinal_index))
    {
        if (is_eof_)
        {
            return kTryAgain;
        }

        LOG_ERROR("read index of 'sqn={1}' in ordinal index file '{2}' failed.",
                  begin_, ordinal_index_file_path);
        return kFailure;
    }

    ///读消息
    cur_msg_sqn_ = begin_;
    PhysicalFilePosType begin_msg_rpos =
        GetMsgDataPhyFilePos(begin_ordinal_index.GetPos(), file_header_);
    FileSizeType msgdata_filesize;
    if (false == OpenSeekMsgdataFile(begin_msg_rpos, msgdata_filesize))
    {
        return kFailure;
    }

    AutoSyncFilebuf filebuf_guard;
    if (msg_prop)
    {
        filebuf_guard = AutoSyncFilebuf(msgdata_filebuf_);
    }

    do
    {
        bool ok = false;
        if (msg_prop)
        {
            ok = ReadNextMessage<TrackType>(msg_prop);
        }
        else
        {
            ok = ReadNextMessage<TrackType>();
        }

        if (!ok)
        {
            if (read_to_most_recent && is_eof_)
            {
                return kSuccess;
            }

            if (is_eof_)
            {
                return kTryAgain;
            }

            LOG_ERROR("read msg(sqn={1}) in file {2} failed.",
                      cur_msg_sqn_, msgdata_filebuf_->filepath());
            return kFailure;
        }

        auto ret = on_hist_msg(ami_msg_);
        if (kSuccess != ret)
        {
            return ret;
        }

        cur_msg_sqn_++;
    } while (cur_msg_sqn_ < end_ || read_to_most_recent);

    return kSuccess;
}

template <typename TrackType, typename KeyType>
ErrorCode_def RecordReader::ReadHistMessage(const std::string& track_data_path,
                                            const KeyType& key_value,
                                            const Message::SqnType& begin, const Message::SqnType& end,
                                            const OnAMIMessageType& on_hist_msg,
                                            const RMPropType& msg_prop)
{
    if (!CheckBeginEndValidation(begin, end))
    {
        return kFailure;
    }

    auto begin_              = begin;
    auto end_                = end;
    bool read_to_most_recent = false;
    if (AmiRecorderBase::kMostRecent == begin)
    {
        begin_ = GetHisMsgCntByKey(track_data_path, key_value);
        if (AmiRecorderBase::kMostRecent == begin_)
        {
            LOG_ERROR("{1} has NO recorded message", track_data_path);
            return kFailure;
        }

        end_                = begin_ + 1;
        read_to_most_recent = true;
    }
    else if (AmiRecorderBase::kMostRecent == end)
    {
        end_ = GetHisMsgCntByKey(track_data_path, key_value);
        if (AmiRecorderBase::kMostRecent == end_)
        {
            LOG_ERROR("{1} has NO recorded message", track_data_path);
            return kFailure;
        }

        end_++;

        if (end_ < begin_)
        {
            LOG_ERROR("{1} has NO recorded message from begin(={2})",
                      track_data_path, begin_);
            return kFailure;
        }

        read_to_most_recent = true;
    }
    else if (end == begin)
    {
        return kSuccess;
    }

    //读第一个索引
    bf::path key_index_file_path(GetKeyindexFilePath(track_data_path, key_value));
    auto begin_index_pos = OrdinalIndex::GetMsgIndexPos(begin_);
    FileSizeType key_index_file_size;
    FilebufPtrType key_index_filebuf;
    if (!(key_index_filebuf = OpenSeekIndexFile(key_index_file_path,
                                                begin_index_pos,
                                                key_index_file_size)))
    {
        return kFailure;
    }

    OrdinalIndex ordinal_index;
    if (!RecordReader::ReadOrdinalIndex(*key_index_filebuf, ordinal_index))
    {
        if (is_eof_)
        {
            return kTryAgain;
        }

        LOG_ERROR("read index of 'sqn={1}' in key index file '{2}' failed.",
                  begin_, key_index_file_path);
        return kFailure;
    }

    ///读消息
    cur_msg_sqn_ = begin_;
    PhysicalFilePosType msg_rpos =
        GetMsgDataPhyFilePos(ordinal_index.GetPos(), file_header_);
    FileSizeType msgdata_filesize;
    if (false == OpenSeekMsgdataFile(msg_rpos, msgdata_filesize))
    {
        return kFailure;
    }

    AutoSyncFilebuf filebuf_guard;
    if (msg_prop)
    {
        filebuf_guard = AutoSyncFilebuf(msgdata_filebuf_);
    }

    do
    {
        bool ok = false;
        if (msg_prop)
        {
            ok = ReadNextMessage<TrackType>(msg_rpos.second, msg_prop);
        }
        else
        {
            ok = ReadNextMessage<TrackType>(msg_rpos.second);
        }

        if (!ok)
        {
            if (read_to_most_recent && is_eof_ == true)
            {
                return kSuccess;
            }

            if (is_eof_)
            {
                return kTryAgain;
            }

            LOG_ERROR("read msg(sqn={1}) in file {2} failed.",
                      cur_msg_sqn_, msgdata_filebuf_->filepath());
            return kFailure;
        }

        auto ret = on_hist_msg(ami_msg_);
        if (kSuccess != ret)
        {
            return ret;
        }

        cur_msg_sqn_++;

        if (cur_msg_sqn_ < end_ || read_to_most_recent)
        {  //读下一个索引
            if (!RecordReader::ReadOrdinalIndex(*key_index_filebuf, ordinal_index))
            {
                if (read_to_most_recent && is_eof_ == true)
                {
                    return kSuccess;
                }

                if (is_eof_)
                {
                    return kTryAgain;
                }

                LOG_ERROR("read index of 'sqn={1}' "
                          "in key index file '{2}' failed.",
                          cur_msg_sqn_, key_index_file_path);
                return kFailure;
            }

            msg_rpos = GetMsgDataPhyFilePos(ordinal_index.GetPos(), file_header_);
        }
    } while (cur_msg_sqn_ < end_ || read_to_most_recent);

    return kSuccess;
}

template <>
ErrorCode_def RecordReader::ReadHistMessage<StTag>(const std::string& track_data_path,
                                                   const Message::SqnType&, const Message::SqnType&,
                                                   const OnAMIMessageType& on_hist_msg,
                                                   const RMPropType&)
{
    PhysicalFilePosType begin_msg_rpos =
        GetMsgDataPhyFilePos(0, file_header_);
    FileSizeType msgdata_filesize;
    if (false == OpenSeekMsgdataFile(begin_msg_rpos, msgdata_filesize))
    {
        return kFailure;
    }

    if (!ReadNextMessage<StTag>())
    {
        return kFailure;
    }

    auto ret = on_hist_msg(ami_msg_);
    if (kSuccess != ret)
    {
        return ret;
    }

    return kSuccess;
}

ErrorCode_def RecordReader::ReadRxHistMessage(const bf::path& track_data_path,
                                              const Message::SqnType& begin, const Message::SqnType& end,
                                              const OnAMIMessageType& on_rx_hist_msg,
                                              const RMPropType& msg_prop)
{
    Reset(track_data_path);
    ami_msg_->reset<true>();
    ami_msg_->ami_meta_data.ami_flags |= AMI_INGRESS_MESSAGE;
    return ReadHistMessage<RxTag>(track_data_path.string(),
                                  begin, end,
                                  on_rx_hist_msg,
                                  msg_prop);
}

ErrorCode_def RecordReader::ReadRxTNPHistMessage(const bf::path& track_data_path,
                                                 const AmiMetaData::IDType& transport_id,
                                                 const Message::SqnType& begin, const Message::SqnType& end,
                                                 const OnAMIMessageType& on_rx_hist_msg,
                                                 const RMPropType& msg_prop)
{
    Reset(track_data_path);
    ami_msg_->reset<true>();
    ami_msg_->ami_meta_data.ami_flags |= AMI_INGRESS_MESSAGE;
    return ReadHistMessage<RxTag, TransportKey>(track_data_path.string(),
                                                transport_id, begin, end,
                                                on_rx_hist_msg, msg_prop);
}

ErrorCode_def RecordReader::ReadRxEDPHistMessage(const bf::path& track_data_path,
                                                 const AmiMetaData::IDType& endpoint_id,
                                                 const Message::SqnType& begin, const Message::SqnType& end,
                                                 const OnAMIMessageType& on_rx_hist_msg,
                                                 const RMPropType& msg_prop)
{
    Reset(track_data_path);
    ami_msg_->reset<true>();
    ami_msg_->ami_meta_data.ami_flags |= AMI_INGRESS_MESSAGE;
    return ReadHistMessage<RxTag, EndpointKey>(track_data_path.string(),
                                               endpoint_id, begin, end,
                                               on_rx_hist_msg, msg_prop);
}

ErrorCode_def RecordReader::ReadRxSTRHistMessage(const bf::path& track_data_path,
                                                 const MessageHeader::IDType& stream_id,
                                                 const Message::SqnType& begin, const Message::SqnType& end,
                                                 const OnAMIMessageType& on_rx_hist_msg,
                                                 const RMPropType& msg_prop)
{
    Reset(track_data_path);
    ami_msg_->reset<true>();
    ami_msg_->ami_meta_data.ami_flags |= AMI_INGRESS_MESSAGE;
    return ReadHistMessage<RxTag, StreamKey>(track_data_path.string(),
                                             stream_id, begin, end,
                                             on_rx_hist_msg, msg_prop);
}

ErrorCode_def RecordReader::ReadTxHistMessage(const bf::path& track_data_path,
                                              const Message::SqnType& begin, const Message::SqnType& end,
                                              const OnAMIMessageType& on_tx_hist_msg,
                                              const RMPropType& msg_prop)
{
    Reset(track_data_path);
    ami_msg_->reset<true>();
    ami_msg_->ami_meta_data.ami_flags = 0;
    return ReadHistMessage<TxTag>(track_data_path.string(),
                                  begin, end,
                                  on_tx_hist_msg, msg_prop);
}

ErrorCode_def RecordReader::ReadTxSTRHistMessage(const bf::path& track_data_path,
                                                 const MessageHeader::IDType& stream_id,
                                                 const Message::SqnType& begin, const Message::SqnType& end,
                                                 const OnAMIMessageType& on_tx_hist_msg,
                                                 const RMPropType& msg_prop)
{
    Reset(track_data_path);
    ami_msg_->reset<true>();
    ami_msg_->ami_meta_data.ami_flags = 0;
    return ReadHistMessage<TxTag, StreamKey>(track_data_path.string(),
                                             stream_id, begin, end,
                                             on_tx_hist_msg, msg_prop);
}

ErrorCode_def RecordReader::ReadStatusMessage(const bf::path& track_data_path,
                                              const OnAMIMessageType& on_status_msg)
{
    Reset(track_data_path);
    return ReadHistMessage<StTag>(track_data_path.string(), 0, 0, on_status_msg);
}

}  // namespace ami
