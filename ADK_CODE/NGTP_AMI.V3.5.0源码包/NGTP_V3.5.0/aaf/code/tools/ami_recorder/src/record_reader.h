/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_RECORD_READER_H_
#define AMI_RECORD_READER_H_

#include <time.h>
///< cpp std
#include <functional>
#include <memory>
#include <streambuf>
#include <unordered_map>

///< boost
#include <boost/filesystem.hpp>
#include <boost/optional.hpp>

///< adk, ami public header
#include <ami/error_code.h>

///< ami impl
#include "../ami_constant.h"
#include "../ami_message.h"
#include "../log.h"

///< impl
#include "record_file_header.h"
#include "recorded_message_index.h"
#include "recorder_base.h"
#include "recorder_fwd.h"

namespace ami
{

#ifdef __AMI_TEST_FRAMEWORK__
extern bool g_simulate_crc_error;
extern uint32_t g_simulate_crc_error_count;
#endif

class RecordReader
{
private:
    typedef boost::optional<RecordedMsgProp> RMPropType;

    class Filebuf : public std::filebuf
    {
    public:
        Filebuf* open(const boost::filesystem::path& file_path,
                      std::ios_base::openmode open_mode)
        {
            file_path_ = file_path;

            if (nullptr == std::filebuf::open(file_path.string().c_str(), open_mode))
            {
                return nullptr;
            }

            return this;
        }

        boost::filesystem::path filepath() const
        {
            return file_path_;
        }

    private:
        boost::filesystem::path file_path_;
    };

    typedef Filebuf FilebufType;
    typedef std::shared_ptr<FilebufType> FilebufPtrType;
    typedef std::unordered_map<std::string, FilebufPtrType> IndexFilebufMapType;

    class AutoSyncFilebuf
    {
    public:
        AutoSyncFilebuf() = default;
        
        explicit AutoSyncFilebuf(FilebufPtrType filebuf)
            : filebuf_(filebuf)
        {
        }

        ~AutoSyncFilebuf()
        {
            if (filebuf_)
            {
                filebuf_->pubsync();
            }
        }

    private:
        FilebufPtrType filebuf_;
    };

public:
    RecordReader()
        : msgdata_filebuf_(new FilebufType())
    {
        /**
         * 使用ami_msg_buf_构造ami_msg_，不可以delete ami_msg_
         */
        ami_msg_ = reinterpret_cast<AmiMessage*>(ami_msg_buf_);
        ami_msg_->reset<true>();
        ami_msg_->message()->ResetAppMessage();

        char* env_str = std::getenv("AMI_RECORDER_NO_CRC_CHECK");
        if (env_str != nullptr 
            && (*env_str == 'Y' || *env_str == 'y' || *env_str == '1') )
        {
            is_check_crc_ = false;
        }
    }

    RecordReader(const RecordReader&) = delete;
    RecordReader& operator=(const RecordReader&) = delete;

    ~RecordReader() = default;

    ErrorCode_def ReadRxHistMessage(const boost::filesystem::path& track_data_path,
                                    const Message::SqnType& begin, const Message::SqnType& end,
                                    const OnAMIMessageType& on_rx_hist_msg,
                                    const RMPropType& msg_prop = RMPropType());

    ErrorCode_def ReadRxTNPHistMessage(const boost::filesystem::path& track_data_path,
                                       const AmiMetaData::IDType& transport_id,
                                       const Message::SqnType& begin, const Message::SqnType& end,
                                       const OnAMIMessageType& on_rx_hist_msg,
                                       const RMPropType& msg_prop = RMPropType());

    ErrorCode_def ReadRxEDPHistMessage(const boost::filesystem::path& track_data_path,
                                       const AmiMetaData::IDType& endpoint_id,
                                       const Message::SqnType& begin, const Message::SqnType& end,
                                       const OnAMIMessageType& on_rx_hist_msg,
                                       const RMPropType& msg_prop = RMPropType());

    ErrorCode_def ReadRxSTRHistMessage(const boost::filesystem::path& track_data_path,
                                       const MessageHeader::IDType& stream_id,
                                       const Message::SqnType& begin, const Message::SqnType& end,
                                       const OnAMIMessageType& on_rx_hist_msg,
                                       const RMPropType& msg_prop = RMPropType());

    ErrorCode_def ReadTxHistMessage(const boost::filesystem::path& track_data_path,
                                    const Message::SqnType& begin, const Message::SqnType& end,
                                    const OnAMIMessageType& on_tx_hist_msg,
                                    const RMPropType& msg_prop = RMPropType());

    ErrorCode_def ReadTxSTRHistMessage(const boost::filesystem::path& track_data_path,
                                       const MessageHeader::IDType& stream_id,
                                       const Message::SqnType& begin, const Message::SqnType& end,
                                       const OnAMIMessageType& on_tx_hist_msg,
                                       const RMPropType& msg_prop = RMPropType());

    ErrorCode_def ReadStatusMessage(const boost::filesystem::path& track_data_path,
                                    const OnAMIMessageType& on_status_msg);

    /**
     * 获取通道track_data_path的消息总量
     */
    Message::SqnType GetHistMsgCnt(const boost::filesystem::path& track_data_path);

    /**
     * 获取tx通道track_data_path的指定stream_id的消息总量
     */
    Message::SqnType GetTxSTRHistMsgCnt(const boost::filesystem::path& track_data_path,
                                        const MessageHeader::IDType& stream_id);

    /**
     * 获取rx通道track_data_path的指定stream_id的消息总量
     */
    Message::SqnType GetRxSTRHistMsgCnt(const boost::filesystem::path& track_data_path,
                                        const MessageHeader::IDType& stream_id);

    /**
     * 获取rx通道track_data_path的指定transport_id的消息总量
     */
    Message::SqnType GetRxTNPHistMsgCnt(const boost::filesystem::path& track_data_path,
                                        const AmiMetaData::IDType& transport_id);

    /**
     * 获取rx通道track_data_path的指定endpoint_id的消息总量
     */
    Message::SqnType GetRxEDPHistMsgCnt(const boost::filesystem::path& track_data_path,
                                        const AmiMetaData::IDType& endpoint_id);

    Message::SqnType GetRxHistMessageCnt(const boost::filesystem::path& track_data_path);

    const RecordFileHdr& GetFileHeader() const
    {
        return file_header_;
    }

private:
    bool OpenFile(const boost::filesystem::path& file_path,
                  FilebufPtrType file_buf,
                  std::ios_base::openmode rw_mode = std::ios_base::in)
    {
        if (file_buf->is_open())
        {
            return true;
        }

        const auto open_mode = std::ios_base::binary | rw_mode;
        if (nullptr == file_buf->open(file_path.c_str(), open_mode))
        {
            LOG_ERROR("open file {1} failed.", file_path);
            return false;
        }

        LOG_DEBUG("open file {1} ok.", file_path);
        return true;
    }

    bool OpenSeekFile(const boost::filesystem::path& file_path,
                      FilebufPtrType file_buf,
                      const FilePosType& target_pos,
                      FileSizeType& file_size,
                      std::ios_base::openmode rw_mode)
    {
        if (!OpenFile(file_path, file_buf, rw_mode))
        {
            return false;
        }

        boost::system::error_code ec;
        file_size = boost::filesystem::file_size(file_path, ec);
        if (ec)
        {
            LOG_ERROR("stat file '{1}' failed: {2}.", file_path, ec.message());
            return false;
        }

        if (file_size < target_pos)
        {
            LOG_WARN_RATELIMITED_LOW("too big begin(tpos={1})", target_pos);
            return false;
        }

        if (target_pos != file_buf->pubseekpos(target_pos))
        {
            LOG_ERROR("can not seek to tpos={1} of file '{2}'",
                      target_pos, file_path);
            return false;
        }

        return true;
    }

    bool OpenSeekMsgdataFile(const PhysicalFilePosType& target_pos,
                             FileSizeType& file_size)
    {
        const boost::filesystem::path msgdata_path(GetMsgDataFilePath(track_data_path_.string(),
                                                                      target_pos.first));
        const auto rw_mode = std::ios_base::in | std::ios_base::out;

        if (!OpenFile(msgdata_path, msgdata_filebuf_, rw_mode))
        {
            return false;
        }

        if (!file_header_readed_)
        {
            if (!file_header_.Read(*msgdata_filebuf_))
            {
                return false;
            }

            file_header_readed_ = true;
        }

        return OpenSeekFile(msgdata_path, msgdata_filebuf_,
                            target_pos.second, file_size, rw_mode);
    }

    FilebufPtrType OpenSeekIndexFile(const boost::filesystem::path& file_path,
                                     const FilePosType& target_pos,
                                     FileSizeType& file_size)
    {
        FilebufPtrType index_filebuf;
        if (!index_filebuf_map_.count(file_path.string()))
        {
            index_filebuf = FilebufPtrType(new FilebufType());
            index_filebuf_map_.emplace(file_path.string(), index_filebuf);
        }
        else
        {
            index_filebuf = index_filebuf_map_.at(file_path.string());
        }

        if (false == OpenSeekFile(file_path, index_filebuf, target_pos, file_size, std::ios_base::in))
        {
            return FilebufPtrType();
        }

        return index_filebuf;
    }

    bool ReadOrdinalIndex(std::streambuf& i, OrdinalIndex& index_item)
    {
        const FileSizeType len_to_read = index_item.ValueSize();
        const auto len_read            = i.sgetn((char*)index_item.ValueBegin(), len_to_read);
        if (len_read < len_to_read)
        {
            is_eof_ = true;
            return false;
        }

        return true;
    }

    bool ReadOrdinalIndex(std::streambuf& i,
                          const FilePosType& index_pos,
                          OrdinalIndex& index_item)
    {
        if (index_pos != i.pubseekpos(index_pos))
        {
            return false;
        }

        return ReadOrdinalIndex(i, index_item);
    }

    void Reset(const boost::filesystem::path& track_data_path)
    {
        if (track_data_path == track_data_path_)
        {
            is_eof_ = false;
            return;
        }

        msgdata_filebuf_.reset(new FilebufType());
        index_filebuf_map_.clear();
        is_eof_             = false;
        is_crc_error_       = false;
        cur_msg_sqn_        = AmiRecorderBase::kMostRecent;
        file_header_readed_ = false;
        file_header_        = RecordFileHdr();
        track_data_path_    = track_data_path;
    }

    bool ReadMsgEndpointID(std::streambuf& i, AmiMessage& ami_message)
    {
        const FileSizeType len_to_read = sizeof(AmiMetaData::endpoint_id);
        auto* endpint_id_ptr           = (std::streambuf::char_type*)&ami_message.ami_meta_data.endpoint_id;
        const auto len_read            = i.sgetn(endpint_id_ptr, len_to_read);
        if (len_read < len_to_read)
        {
            is_eof_ = true;
            return false;
        }

        if (is_check_crc_ && !!(file_header_.GetFileOpts() & FileOpts::kCrc))
        {
            crc_ = MsgCRCCalFunc(&ami_message.ami_meta_data.endpoint_id,
                                 len_to_read, crc_);
        }

        return true;
    }

    bool ReadMsgTransportID(std::streambuf& i, AmiMessage& ami_message)
    {
        const FileSizeType len_to_read = sizeof(AmiMetaData::transport_id);
        auto* transport_id_ptr         = (std::streambuf::char_type*)&ami_message.ami_meta_data.transport_id;
        const auto len_read            = i.sgetn(transport_id_ptr, len_to_read);
        if (len_read < len_to_read)
        {
            is_eof_ = true;
            return false;
        }

        if (is_check_crc_ && !!(file_header_.GetFileOpts() & FileOpts::kCrc))
        {
            crc_ = MsgCRCCalFunc(&ami_message.ami_meta_data.transport_id,
                                 len_to_read, crc_);
        }

        return true;
    }

    bool ReadAppMsg(std::streambuf& i, AmiMessage& ami_message)
    {
        Message& app_msg = *(ami_message.message());

        ///Message::stream_sqn ~ topic_sqn
        const auto len_sqn_to_read = sizeof(app_msg.stream_sqn)
            + sizeof(app_msg.topic_sqn);
        if (len_sqn_to_read != i.sgetn((std::streambuf::char_type*)&app_msg.stream_sqn, len_sqn_to_read))
        {
            is_eof_ = true;
            return false;
        }

        ///Message::app_data_len
        if (sizeof(app_msg.app_data_len)
            != i.sgetn((std::streambuf::char_type*)&app_msg.app_data_len,
                       sizeof(app_msg.app_data_len)))
        {
            is_eof_ = true;
            return false;
        }

        if (app_msg.app_data_len > AMI_MAX_MESSAGE_SIZE_INTERNAL)
        {
            LOG_ERROR("unexpected message data len(={1})",
                      app_msg.app_data_len);
            return false;
        }

        assert(sizeof(MsgRecord::msg_header) == sizeof(app_msg.msg_header));
        if (sizeof(MsgRecord::msg_header)
            != i.sgetn((std::streambuf::char_type*)&app_msg.msg_header,
                       sizeof(MsgRecord::msg_header)))
        {
            is_eof_ = true;
            return false;
        }

        recorder::ExMessageHeader ex_msg_header;
        if (sizeof(MsgRecord::ex_msg_header)
            != i.sgetn((std::streambuf::char_type*)&ex_msg_header,
                       sizeof(MsgRecord::ex_msg_header)))
        {
            is_eof_ = true;
            return false;
        }

        ami_message.ami_meta_data.c_stream_sqn                      = ex_msg_header.c_stream_sqn;
        ami_message.ami_meta_data.c_topic_sqn                       = ex_msg_header.c_topic_sqn;
        ami_message.ami_meta_data.c_endpoint_sqn                    = ex_msg_header.c_endpoint_sqn;
        (*(RecordedMsgPropUnion*)(&app_msg.ex_msg_header)).msg_prop = ex_msg_header.msg_prop;
        ami_message.ami_meta_data.ami_persistent_context_0          = ex_msg_header.ami_user_context_0;
        ami_message.ami_meta_data.ami_persistent_context_1          = ex_msg_header.ami_user_context_1;
        ami_message.ami_meta_data.recorder_receive_msg_time_ns      = ex_msg_header.recorder_receive_msg_time_ns;

        ///read app data
        const auto len_read = i.sgetn((std::streambuf::char_type*)app_msg.app_data_begin,
                                      app_msg.app_data_len);
        if (len_read < app_msg.app_data_len)
        {
            is_eof_ = true;
            return false;
        }

        if (is_check_crc_ && !!(file_header_.GetFileOpts() & FileOpts::kCrc))
        {  //FIXME: add recorder::ex_msg_header, exclude msg props
            crc_ = MsgCRCCalFunc(&app_msg.stream_sqn,
                                 sizeof(app_msg.stream_sqn) + sizeof(app_msg.topic_sqn),
                                 crc_);
            crc_ = MsgCRCCalFunc(&app_msg.app_data_len,
                                 sizeof(app_msg.app_data_len),
                                 crc_);
            crc_ = MsgCRCCalFunc(&app_msg.msg_header,
                                 sizeof(MessageHeader) + app_msg.app_data_len,
                                 crc_);
        }

        return true;
    }

    bool CheckCRC(std::streambuf& i)
    {
        MsgCRCType crc;
        const FileSizeType len_to_read = sizeof(MsgCRCType);
        const auto len_read            = i.sgetn((char*)&crc, len_to_read);
        if (len_read < len_to_read)
        {
            is_eof_ = true;
            return false;
        }

        // note: place this logic after i.sgetn
        if (!is_check_crc_)
        {
            return true;
        }

        #ifdef __AMI_TEST_FRAMEWORK__
        do {
            // simulate crc error!
            if (g_simulate_crc_error)   
            {   
                if ((--g_simulate_crc_error_count) == 0)
                {
                    crc_ = 0x01;
                }
            }
        } while (false);
        #endif

        if (crc != crc_)
        {
            is_crc_error_ = true;
            LOG_ERROR("message CRC check failed");
            return false;
        }
        crc_ = 0;   // reset crc for next message
        return true;
    }

    template <typename TrackType>
    bool ReadNextMessage(const RMPropType& msg_prop = RMPropType())
    {
        if (is_check_crc_ && !!(file_header_.GetFileOpts() & FileOpts::kCrc))
        {
            crc_ = 0;
        }

        FilePosType msg_start_pos = 0, msg_end_pos = 0;

        if (msg_prop)
        {
            msg_start_pos = msgdata_filebuf_->pubseekoff(0, std::ios_base::cur);
        }

        if (false == ReadAppMsg(*msgdata_filebuf_, *ami_msg_))
        {
            return false;
        }

        if (!!(file_header_.GetFileOpts() & FileOpts::kCrc)
            && (false == CheckCRC(*msgdata_filebuf_)))
        {
            LOG_ERROR("read check msg({1})'s crc failed.", *ami_msg_);
            return false;
        }

        if (msg_prop)
        {
            msg_end_pos                     = msgdata_filebuf_->pubseekoff(0, std::ios_base::cur);
            Message& app_msg                = *(ami_msg_->message());
            RecordedMsgPropUnion prop_union = {app_msg.ex_msg_header};
            if (msg_prop && prop_union.msg_prop != *msg_prop)
            {
                prop_union.msg_prop   = *msg_prop;
                app_msg.ex_msg_header = prop_union.orig_holder;
                const FilePosType prop_pos =
                    msg_start_pos + (PosOffType)kAppmsgPropOffset;
                if (prop_pos != msgdata_filebuf_->pubseekpos(prop_pos))
                {
                    return false;
                }

                const FileSizeType len_to_write = sizeof(prop_union.msg_prop);
                auto* prop_union_ptr            = (std::streambuf::char_type*)&prop_union;
                const auto len_written          = msgdata_filebuf_->sputn(prop_union_ptr,
                                                                 len_to_write);
                if (len_written < len_to_write)
                {
                    return false;
                }

                if (msg_end_pos != msgdata_filebuf_->pubseekpos(msg_end_pos))
                {
                    return false;
                }
            }
        }

        return true;
    }

    template <typename TrackType>
    bool ReadNextMessage(const FilePosType& pos,
                         const RMPropType& msg_prop = RMPropType())
    {
        if (pos != msgdata_filebuf_->pubseekpos(pos))
        {
            return false;
        }

        return ReadNextMessage<TrackType>(msg_prop);
    }

    bool CheckBeginEndValidation(const Message::SqnType& begin,
                                 const Message::SqnType& end)
    {
        if (AmiRecorderBase::kMostRecent != begin
            && AmiRecorderBase::kMostRecent != end && end < begin)
        {
            LOG_ERROR("begin(sqn={1} > end(sqn={2}))", begin, end);
            return false;
        }

        if (AmiRecorderBase::kMostRecent == begin
            && AmiRecorderBase::kMostRecent != end)
        {
            LOG_ERROR("begin(sqn={1} and end(sqn={1}) is meaningless",
                      begin, end);
            return false;
        }

        return true;
    }

    template <typename TrackType>
    ErrorCode_def ReadHistMessage(const std::string& track_data_path,
                                  const Message::SqnType& begin, const Message::SqnType& end,
                                  const OnAMIMessageType& on_hist_msg,
                                  const RMPropType& msg_prop = RMPropType());

    template <typename TrackType, typename KeyType>
    ErrorCode_def ReadHistMessage(const std::string& track_data_path,
                                  const KeyType& key_value,
                                  const Message::SqnType& begin, const Message::SqnType& end,
                                  const OnAMIMessageType& on_hist_msg,
                                  const RMPropType& msg_prop = RMPropType());

    template <typename KeyType>
    Message::SqnType GetHisMsgCntByKey(const std::string& track_data_path,
                                       const KeyType& key_value);

    bool is_crc_error()  { return is_crc_error_; }

private:
    char ami_msg_buf_[AMI_MAX_MESSAGE_SIZE_INTERNAL] = {0};
    AmiMessage* ami_msg_                             = nullptr;
    bool        is_check_crc_ = true; // to bypass crc check
    boost::filesystem::path track_data_path_;
    RecordFileHdr file_header_;
    bool file_header_readed_      = false;
    Message::SqnType cur_msg_sqn_ = AmiRecorderBase::kMostRecent;
    bool is_eof_                  = false;
    bool is_crc_error_            = false; // mark if crc check failed
    MsgCRCType crc_;
    IndexFilebufMapType index_filebuf_map_;
    FilebufPtrType msgdata_filebuf_;

    LOG_DECLARE
    friend class MessageTrack;
    friend class TxMessageTrack;
    friend class RxMessageTrack;
    friend class StMessageTrack;
    template <typename TrackType>
    friend class RecordIterator;
    template <typename TrackType, typename KeyindexType>
    friend class KeyindexRecordIterator;
};

template <>
inline bool RecordReader::ReadNextMessage<RxTag>(const RMPropType& msg_prop)
{
    if (!!(file_header_.GetFileOpts() & FileOpts::kCrc))
    {
        crc_ = 0;
    }

    FilePosType msg_start_pos = 0, msg_end_pos = 0;

    if (msg_prop)
    {
        msg_start_pos = msgdata_filebuf_->pubseekoff(0, std::ios_base::cur);
    }

    if ((false == ReadMsgEndpointID(*msgdata_filebuf_, *ami_msg_))
        || (false == ReadMsgTransportID(*msgdata_filebuf_, *ami_msg_))
        || (false == ReadAppMsg(*msgdata_filebuf_, *ami_msg_)))
    {
        return false;
    }

    if (!!(file_header_.GetFileOpts() & FileOpts::kCrc)
        && (false == CheckCRC(*msgdata_filebuf_)))
    {
        return false;
    }

    if (msg_prop)
    {
        msg_end_pos                     = msgdata_filebuf_->pubseekoff(0, std::ios_base::cur);
        Message& app_msg                = *(ami_msg_->message());
        RecordedMsgPropUnion prop_union = {app_msg.ex_msg_header};
        if (msg_prop && prop_union.msg_prop != *msg_prop)
        {
            prop_union.msg_prop   = *msg_prop;
            app_msg.ex_msg_header = prop_union.orig_holder;
            const FilePosType prop_pos =
                msg_start_pos + (PosOffType)(2 * sizeof(Message::IDType))
                + (PosOffType)kAppmsgPropOffset;
            if (prop_pos != msgdata_filebuf_->pubseekpos(prop_pos))
            {
                return false;
            }

            const FileSizeType len_to_write = sizeof(prop_union.msg_prop);
            const auto len_written =
                msgdata_filebuf_->sputn((std::streambuf::char_type*)&prop_union,
                                        len_to_write);
            if (len_written < len_to_write)
            {
                return false;
            }

            if (msg_end_pos != msgdata_filebuf_->pubseekpos(msg_end_pos))
            {
                return false;
            }
        }
    }

    return true;
}

}  // namespace ami
namespace fmt
{
template <> 
struct formatter<ami::AmiMessage> : formatter<string_view>
{
    template<typename FormatContext>
    auto format(const ami::AmiMessage& x, FormatContext& ctx) -> decltype(this->formatter<string_view>::format(string_view{}, ctx))
    {
        std::ostringstream os;
        os << x;
        return formatter<string_view>::format(string_view(os.str()), ctx);
    }
};
}
#endif /* AMI_RECORD_READER_H_ */
