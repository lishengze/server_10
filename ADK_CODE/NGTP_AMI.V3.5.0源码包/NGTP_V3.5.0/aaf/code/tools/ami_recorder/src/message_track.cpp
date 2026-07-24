/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */

///< linux
#include <fcntl.h>  //fallocate
#include <sys/types.h>
#include <unistd.h>  //ftruncate

///< cpp std
#include <cassert>
#include <iomanip>

///< BOOST
#include <boost/lexical_cast.hpp>

///< adk, ami public
#include <ami/config_key.h>
#include <ami/property.h>

///< ami impl
#include "../log.h"

///< impl
#include "config_default_value.h"
#include "control_message_key.h"
#include "message_track.h"
#include "recorder.h"
#include "rx_message_track.h"
#include "st_message_track.h"
#include "tx_message_track.h"

namespace ami
{

namespace bl   = boost::locale;
namespace bs   = boost::system;
namespace bf   = boost::filesystem;
namespace bt   = boost::property_tree;
namespace cc   = config::context;
namespace ccr  = config::context::recorder;
namespace rcdv = recorder::cdv;

LOG_DEFINE(ami::MessageTrack)

/*****************************************************************************
 * MessageTrack::FileWriteBuffer
 */
constexpr size_t MessageTrack::FileWriteBuffer::kDataFileBufferSize;

MessageTrack::FileWriteBuffer::FileWriteBuffer(const bf::path& file_path,
                                               MessageTrack& track,
                                               const AfterOverFlowOpType& op,
                                               size_t quantum)
    : file_buf_(nullptr),
      track_type(0),
      file_name_len_(0),
      path_len_(0),
      data_path_len_(0),
      app_mp_mgr_name_len_(0),
      avail_len_(0)
{
    track_type = track.GetTypeChar();

    size_t pos = 0;
    ///填充文件名
    std::string file_name = file_path.filename().string();
    file_name_len_        = file_name.size();
    memcpy(data_begin_ + pos, file_name.c_str(), file_name_len_);
    pos += file_name_len_;

    ///填充MessageTrack::path_
    path_len_ = track.path_.size();
    memcpy(data_begin_ + pos, track.path_.c_str(), path_len_);
    pos += path_len_;

    ///填充MessageTrack::data_path_
    data_path_len_ = track.data_path_.size();
    memcpy(data_begin_ + pos, track.data_path_.c_str(), data_path_len_);
    pos += data_path_len_;

    ///填充ami消息所属mp manger的名字
    std::string app_mp_mgr_name = track.app_msg_mp_manager_.GetMPTableName();
    app_mp_mgr_name_len_        = app_mp_mgr_name.size();
    memcpy(data_begin_ + pos,
           app_mp_mgr_name.c_str(),
           app_mp_mgr_name_len_);
    pos += app_mp_mgr_name_len_;

    adk::MemoryBuffer* mb =
        GetLowLayerMemoryBuffer();
    avail_len_ = mb->mem_buf_unused
        - offsetof(FileWriteBuffer, data_begin_)
        - pos;

    file_buf_ = new FileBufType(*this, track, op, quantum);
}

void* MessageTrack::FileWriteBuffer::operator new(size_t, const MsgData&)
{
    adk::MemoryPool* mp = Recorder::GetMsgDataBufferPool();
    assert(mp);
    adk::MemoryBuffer* mb = mp->NewBuffer();
    assert(mb);

    /*注意，这里篡改了mb的大小是因为虽然该mb的实际大小是1K，但
      是writev机制并不实际使用mb中的内存做缓存，因此将这里的大小
      设置一下仅作为控制刷磁盘单元大小的手段*/
    mb->set_mem_buf_size(kDataFileBufferSize);
    mb->reset();

    return mb->data;
}

void MessageTrack::FileWriteBuffer::Delete(void* wb, const MsgData&) noexcept
{
    if (!wb)
        return;

    adk::MemoryBuffer* mb =
        ADK_CONTAINER_OF(wb, adk::MemoryBuffer, data);
    adk::MemoryPool* mp = Recorder::GetMsgDataBufferPool();
    if (mp)
    {
        mp->DeleteBuffer(mb);
    }
}

MessageTrack::FileWriteBuffer::FileBuffer::int_type
MessageTrack::FileWriteBuffer::FileBuffer::WriteUntilExit(
    MessageTrack::FileWriteBuffer::FileBuffer::int_type c,
    ssize_t write_size)
{
    do
    {
        auto writev_ret = ::writev(fd_, &(*buf_vec_.begin()), buf_vec_.size());

        if ((writev_ret + write_size) == (this->pptr() - this->pbase()))
        {
            this->setp(this->pbase(), this->pbase() + buf_len_);
            buf_vec_.clear();
            return traits_type::not_eof(c);
        }
        else
        {
            LOG_WARN_RATELIMITED_VERY_LOW("writev of '{1}'(fd={3}) failed: {2}, {4}",
                                          encloser_.GetFilePathStr(), errno, fd_, writev_ret);

            if (writev_ret > 0)
            {
                TrimBufVector(writev_ret);
            }
            else
            {
                writev_ret = 0;
            }

            write_size += writev_ret;
        }

        if (Recorder::to_exit())
            return traits_type::eof();
    } while (true);
}

/*****************************************************************************/

/*****************************************************************************
 * MessageTrack
 */
constexpr const char* MessageTrack::kDataQueuePrefix;
constexpr int MessageTrack::kDefaultWeight;
constexpr int MessageTrack::kRxWeightMultiply;
constexpr FileSizeType MessageTrack::kDataFilePreAllocDefault;

template <class T>
void MessageTrack::CascadeThenCheckConfig(const Property& request, const std::string& key,
                                          T& config_value, const T& default_value,
                                          boost::optional<T> low_limit,
                                          boost::optional<T> up_limit)
{
    if (request.HasValue(cc::kRecorder)
        && request.GetPropertyValue(cc::kRecorder).HasValue(key))
    {
        config_value =
            request.GetPropertyValue(cc::kRecorder)
                .GetValue(key, T());
    }
    else if (Recorder::GetCascadeConfig().HasValue(key))
    {
        config_value = Recorder::GetCascadeConfig().GetValue(key, T());
    }
    else
    {
        config_value = default_value;
    }

    if (low_limit && (config_value < *low_limit))
    {
        LOG_WARN("try to set 'msg_queue_size' to {1} < low limit(={2}), "
                 "set it to {2} forcefully",
                 config_value, *low_limit);
        config_value = *low_limit;
    }

    if (up_limit && (config_value > *up_limit))
    {
        LOG_WARN("try to set 'msg_queue_size' to {1} > up limit(={2}), "
                 "set it to {2} forcefully",
                 config_value, *up_limit);
        config_value = *up_limit;
    }
}

// bootstrap senario
ErrorCode_def MessageTrack::DoInit(const string& track_path, const Property& request,
                                   Property& reply, size_t recorder_worker_idx)
{
    LOG_INFO("handle request track_path: {1}, request: {2}", track_path, request.Dump());
    path_ = track_path;

    std::string app_mp_mgr_name =
        request.GetValue(kAppMsgMPName, std::string());
    IF_ERR_RET(app_msg_mp_manager_.AttachMPTable(app_mp_mgr_name),
               LOG_ERROR("attach to app message mp manager '{1}' failed.",
                         app_mp_mgr_name));
    LOG_INFO("attach to app message mp manager '{1}' ok.",
             app_mp_mgr_name);

    data_path_ = (bl::format("{1}/{2}")
                  % request.GetValue(ccr::kDataPath, rcdv::kDataPath)
                  % path_)
                     .str();

    CascadeThenCheckConfig(request, ccr::kMsgQueueSize,
                           msg_queue_size_, rcdv::kMsgQueueSize,
                           boost::make_optional(rcdv::kMsgQueueSizeLLimit));

    CascadeThenCheckConfig(request, ccr::kUseMsgCRC, use_msg_crc_, true);
    if (use_msg_crc_)
    {
        msg_data_file_header_.GetFileOpts() |= FileOpts::kCrc;
    }

    CascadeThenCheckConfig(request, ccr::kIgnoreLostMsg, ignore_lost_msg_, false);

    record_worker_ = Recorder::GetRecordWorker(recorder_worker_idx);
    assert(record_worker_);

    bs::error_code bs_ec;
    bf::create_directories(data_path_, bs_ec);
    if (bs_ec)
    {
        LOG_ERROR("create directory '{1}' failed: {2}",
                  data_path_, bs_ec.message());
        return kFailure;
    }

    IF_ERR_RET(OpenIndexDataFiles(),
               bf::remove_all(bf::path(data_path_), bs_ec));

    if (Recorder::MayLostMsg())
    {
        if (ignore_lost_msg_ && IsUseCRC())
        {
            IF_ERR_RET(RecoverIndexDataFiles(LostMsg()));
        }
        else
        {
            if (!ignore_lost_msg_)
            {
                LOG_ERROR("CAN NOT be tolerant to msg lost when try to rebuild '{1}'", path_);
            }

            if (!IsUseCRC())
            {
                LOG_ERROR("try rebuild '{1}', but crc DISABLED", path_);
            }

            return kFailure;
        }
    }

    LOG_INFO("msg_queue_size: {1}, use_msg_crc: {2}, worker_id: <{3}>",
             msg_queue_size_,
             use_msg_crc_,
             recorder_worker_idx);
    auto recorder_mq_manager = Recorder::GetMQManager();
    assert(recorder_mq_manager);
    msg_ptr_queue_ = recorder_mq_manager->CreateSharedMPSCQueue(
        GetMsgPtrQueueName(), msg_queue_size_);
    if (nullptr == msg_ptr_queue_)
    {
        LOG_ERROR("create message queue '{1}'(sz={2}) failed.",
                  GetMsgPtrQueueName(), msg_queue_size_);
        return kFailure;
    }
    else
    {
        LOG_INFO("create message queue '{1}'(sz={2}) ok.",
                 GetMsgPtrQueueName(), msg_queue_size_);
    }

    adk::ShmPointer shm_pointer = msg_data_file_buf_->GetLowLayerMemoryBuffer()->shm_ptr;
    if (!Recorder::IsTrackInfoSaved(shm_pointer.value))   // shm_pointer was not saved in the queue
    {
        if (adk::kSuccess != Recorder::GetTrackInfoQ()->Push(shm_pointer))
        {
            LOG_ERROR("push message track {1} into track info q failed.",
                      path_);
            return kFailure;
        }
        LOG_INFO("save track shm_ptr <{1}>, data path <{2}>", 
            shm_pointer.value, data_path_);
    }

    return ErrorCode::kSuccess;
}

// recovery senario, cooperate with 2 parameters DoInit
// push the Track into TrackInfo share memory queue, used at the next recovery
// triggered by CreateMessageChannel
ErrorCode_def MessageTrack::DoInit(const string& track_path,
                                   const Property& request, Property& reply)
{
    assert(msg_data_file_buf_);
    std::string app_mp_mgr_name     = msg_data_file_buf_->GetAppMPMgrName();
    std::string req_app_mp_mgr_name = request.GetValue(kAppMsgMPName, std::string());
    if (app_mp_mgr_name != req_app_mp_mgr_name)
    {
        LOG_ERROR("try to rebuild a recovered channle "
                  "with unmatched app mp manager name: "
                  "try use(={1}) != recovered(={2})",
                  req_app_mp_mgr_name, app_mp_mgr_name);
        return kFailure;
    }

    app_msg_mp_manager_.DetachAll();  ///< 一定要先detach
    
    // attach the new share memory pool manager
    // FIXME: to attach the share memory pool as well
    IF_ERR_RET(app_msg_mp_manager_.AttachMPTable(app_mp_mgr_name),
               LOG_ERROR("attach to app message mp manager '{1}' failed.",
                         app_mp_mgr_name));
    LOG_INFO("attach to app message mp manager '{1}' ok.",
             app_mp_mgr_name);

    adk::ShmPointer shm_pointer = msg_data_file_buf_->GetLowLayerMemoryBuffer()->shm_ptr;
    if (!Recorder::IsTrackInfoSaved(shm_pointer.value))   // shm_pointer was not saved in the queue
    {
        if (kSuccess != Recorder::GetTrackInfoQ()->Push(shm_pointer))
        {
            LOG_ERROR("push message track {1} into track info q failed.",
                      path_);
            return kFailure;
        }

        // {1,number=hex}
        LOG_INFO("save track shm_ptr <{1}>, data path <{2}>",
            shm_pointer.value, data_path_);
    }

    rebuilt_ = true;  //重建成功
    return kSuccess;
}

MessageTrack* MessageTrack::NewTrack(const adk::ShmPointer& shm_point)
{
    FileWriteBuffer* track_info_buffer =
        FileWriteBuffer::ConvertFromShmPointer(shm_point,
                                               Recorder::GetMPManager());
    assert(track_info_buffer);
    assert(track_info_buffer->track_type == 't'
           || track_info_buffer->track_type == 'r'
           || track_info_buffer->track_type == 's');

    switch (track_info_buffer->track_type)
    {
    case 't':
        return new TxMessageTrack();
    case 'r':
        return new RxMessageTrack();
    case 's':
        return new StMessageTrack();
    default:
        /*can't reach here*/
        return nullptr;
    }
}

// recovery senario, cooperate with 3 parameters DoInit
ErrorCode_def MessageTrack::DoInit(const adk::ShmPointer& shm_point,
                                   size_t recorder_worker_idx)
{
    msgdata_filebuf_to_recover_ = FileWriteBuffer::ConvertFromShmPointer(
        shm_point, Recorder::GetMPManager());
    assert(msgdata_filebuf_to_recover_);

    path_ = msgdata_filebuf_to_recover_->GetTrackPathStr();

    std::string app_mp_mgr_name = msgdata_filebuf_to_recover_->GetAppMPMgrName();
    IF_ERR_RET(app_msg_mp_manager_.AttachMPTable(app_mp_mgr_name),
               LOG_ERROR("attach to app message mp manager '{1}' failed.",
                         app_mp_mgr_name));
    LOG_INFO("attach to app message mp manager '{1}' ok.",
             app_mp_mgr_name);

    data_path_ = msgdata_filebuf_to_recover_->GetDataPathStr();

    record_worker_ = Recorder::GetRecordWorker(recorder_worker_idx);
    assert(record_worker_);

    bs::error_code bs_ec;
    if (!bf::exists(bf::path(data_path_), bs_ec))
    {
        LOG_WARN("directory '{1}' not exist", data_path_);
        msgdata_filebuf_to_recover_ = nullptr;  // rollback the recovery status
        app_msg_mp_manager_.DetachAll();

        // this may happened when IsDisableTxRecovery was enabled

        // attach the exist message queue
        auto recorder_mq_manager = Recorder::GetMQManager();
        assert(recorder_mq_manager);
        auto* msg_ptr_queue = recorder_mq_manager->AttachSharedMPSCQueue(GetMsgPtrQueueName());
        if (nullptr == msg_ptr_queue)
        {
            // message queue was removed
            LOG_WARN("attach message queue '{1}' failed.", GetMsgPtrQueueName());
            return kNoResources;
        }

        // fix message queue
        msg_ptr_queue->Consistent();

        // drain message queue
        adk::Entry* entry;
        adk::ErrorCode_def ec;
        uint64_t nr_msgs = 0;
        while (msg_ptr_queue->WaitEntry(&entry) == adk::ErrorCode::kSuccess)
        {
            ++nr_msgs;
            msg_ptr_queue->FreeEntry(entry);
        }

        LOG_INFO("drop stale messages from mq <{1}>, total <{2}>",
                 GetMsgPtrQueueName(), nr_msgs);

        return kNoResources;
    }

    IF_ERR_RET(OpenIndexDataFiles());

    auto recorder_mq_manager = Recorder::GetMQManager();
    assert(recorder_mq_manager);
    msg_ptr_queue_ = recorder_mq_manager->AttachSharedMPSCQueue(GetMsgPtrQueueName());
    if (nullptr == msg_ptr_queue_)
    {
        LOG_ERROR("attach message queue '{1}' failed.", GetMsgPtrQueueName());
        return kFailure;
    }
    else
    {
        IF_ERR_RET(msg_ptr_queue_->Consistent(),
                   LOG_ERROR("Consistent() of '{1}' failed.",
                             GetMsgPtrQueueName()));
        LOG_INFO("attach message queue '{1}' ok.", GetMsgPtrQueueName());
    }

    msg_cnt_before_process_mq_ = recorded_msg_cnt_;
    rebuilt_ = false;  //等待agent端发起重建流程
    return ErrorCode::kSuccess;
}

SerialWorker::JobStatus MessageTrack::RecordMessage()
{
    for (uint32_t quota = weight_; quota > 0;)
    {
        try
        {
            ErrorCode_def ret = FetchThenRecordOneMessage();
            switch (ret)
            {
            case kCanBeIgnored:
                break;
            case kRepairing:
                --quota;
                break;
            case kSuccess:
                --quota;
                break;
            case kTryAgain:
            {
                SerialWorker::JobStatus res = SerialWorker::JobStatus::JS_IDLE;
                if (false == stopped_)
                {
                    LOG_INFO("track {1} stopped actively.", path_);
                    is_already_stop_track_ = true;
                    stopped_ = true;
                    res      = SerialWorker::JobStatus::JS_STOP;
                }
                else
                {
                    /*暂时没消息要处理，稍事休息*/
                    no_work_sleep_cnt_++;
                }

                if (!SyncToFile())
                {
                    LOG_ERROR("track {1} sync to file fail, "
                            "job stopped",
                            path_);
                    error_happened_ = true;
                    res             = SerialWorker::JobStatus::JS_STOP;
                }

                return res;
            }
            case kFailure:
            default:
                if (false == stopped_)
                {
                    LOG_TRACE("track {1} stopped actively.", path_);
                    stopped_ = true;
                }

                SyncToFile();
                LOG_ERROR("track {1}'s job stopped", path_);
                error_happened_ = true;
                LOG_INFO("try to notify recorder to exit");
                Recorder::Stop(false);
                return SerialWorker::JobStatus::JS_STOP;
            }
        }
        catch(...)
        {
            LOG_ERROR("catch exception:{1}, stop track:{2}", 
                       boost::current_exception_diagnostic_information(),
                       path_);
            error_happened_ = true;
            LOG_INFO("try to notify recorder to exit");
            Recorder::Stop(false);
            return SerialWorker::JobStatus::JS_STOP;
        }
    }

    yield_cnt_++;
    return SerialWorker::JobStatus::JS_YIELD; /*稍事休息*/
}

ErrorCode_def MessageTrack::OpenMsgDataFilebuf(std::ios_base::openmode open_mode)
{
    if (!msgdata_filebuf_to_recover_)
    {
        MsgData type_identifier;
        msg_data_file_buf_ = new (type_identifier)
            FileWriteBuffer(GetMsgDataFilePath(data_path_, cur_msgdata_filesqn_), *this);
    }
    else
    {  //故障恢复
        msg_data_file_buf_ = new (msgdata_filebuf_to_recover_)
            FileWriteBuffer(GetMsgDataFilePath(data_path_, cur_msgdata_filesqn_), *this);
    }

    bs::error_code bs_ec;
    bool msg_data_file_exists = bf::exists(msg_data_file_buf_->GetFilePathStr(), bs_ec);
    if (nullptr == msg_data_file_buf_->Open(open_mode))
    {
        LOG_ERROR("create msgdatga file buffer {1} failed.",
                  msg_data_file_buf_->GetFilePathStr());
        return kFailure;
    }
    else
    {
        if (!msg_data_file_exists)
        {
            if (!WriteFileHeader())
            {
                return kFailure;
            }
        }
        else
        {
            if (!msg_data_file_header_.Read(msg_data_file_buf_->GetFilePathStr()))
            {
                return kFailure;
            }
        }

        LOG_INFO("create msgdatga file buffer {1} ok.",
                 msg_data_file_buf_->GetFilePathStr());
    }

    return kSuccess;
}

ErrorCode_def MessageTrack::OpenIndexDataFiles()
{
    Index type_identifier;
    ordinal_index_file_buf_ = new (type_identifier)
        FileWriteBuffer(GetOrdinalIndexFilePath(data_path_),
                        *this,
                        std::bind(&MessageTrack::FreeAllMessages, this),
                        OrdinalIndex::ValueSize());
    if (nullptr == ordinal_index_file_buf_->Open(std::ios_base::binary | std::ios_base::out))
    {
        LOG_ERROR("create ordinal index file buffer {1} failed.",
                  ordinal_index_file_buf_->GetFilePathStr());
        return kFailure;
    }
    else
    {
        LOG_INFO("create ordinal index file buffer {1} ok.",
                 ordinal_index_file_buf_->GetFilePathStr());
    }

    IF_ERR_RET(OpenMsgDataFilebuf((std::ios_base::binary
                                   | std::ios_base::out)));

    /**
     * 将索引文件的内容同步到磁盘之前必须先将消息数据文件的内容
     * 同步到磁盘，以免RecordReader读时发现索引对应的数据在数据
     * 文件中不存在
     */

    // ordinal_index_file_buf_ -> msg_data_file_buf_
    ordinal_index_file_buf_->Tie(msg_data_file_buf_);

    return kSuccess;
}

ErrorCode_def MessageTrack::FetchThenRecordOneMessage()
{
    adk::Entry* entry;
    adk::ErrorCode_def ec = msg_ptr_queue_->WaitEntry(&entry);
    if (ec == adk::ErrorCode::kSuccess)
    {
        MQMsgEntry* msg_entry = MQMsgEntry::InitFromAdkEntry(
            entry, app_msg_mp_manager_);

        LOG_TRACE("fetch message record({1}) "
                  "from queue(name={2}, index={3}, len={4})",
                  msg_entry->msg_record,
                  msg_ptr_queue_->name(),
                  msg_ptr_queue_->index(),
                  msg_ptr_queue_->length());

        GAUGE_BEGIN(null_gauge_);
        GAUGE_GAUGE(null_gauge_);

        IF_ERR_RET(FilterMessage(&msg_entry->msg_record, msg_entry));

        is_doing_record_ = true;
        GAUGE_BEGIN(record_one_msg_gauge_);
        if (ADK_UNLIKELY(
                false == RecordOneMessage(msg_entry->msg_record, msg_entry, msg_ptr_queue_)))
        {
            return kFailure;
        }
        GAUGE_GAUGE(record_one_msg_gauge_);
        is_doing_record_ = false;

        return kSuccess;
    }
    else
    {
        INV_LOG_TRACE(no_msg_inv_logger_, "queue(name={1}, index={2}, len={3})",
                      msg_ptr_queue_->name(),
                      msg_ptr_queue_->index(),
                      msg_ptr_queue_->length());

        ///恢复模式下，如果没有消息了，则说明完成了
        if (msgdata_filebuf_to_recover_)
        {
            if (!recovery_ok_)
            {
                LOG_INFO("total <{1}> messages received from rx side message queue <{2}>",
                         recorded_msg_cnt_ - msg_cnt_before_process_mq_, 
                         msg_ptr_queue_->name());
            }
            recovery_ok_ = true;
        }

        can_quit_elegantly_ = true;
        return kTryAgain;
    }
}

ErrorCode_def MessageTrack::TruncateIndexFile(FileWriteBuffer* index_filebuf,
                                              Message::SqnType new_last_msg_sqn)
{
    index_filebuf->recover_next_msg_sqn(new_last_msg_sqn);
    const Message::SqnType next_msg_sqn  = new_last_msg_sqn + 1;
    const FilePosType next_msg_index_pos = OrdinalIndex::GetMsgIndexPos(next_msg_sqn);

    if (index_filebuf->Truncate(next_msg_index_pos) == nullptr)
    {
        return kFailure;
    }

    if (next_msg_index_pos != static_cast<FileWriteBuffer::FileBuffer&>(*index_filebuf).pubseekpos(next_msg_index_pos))
    {
        LOG_ERROR("reset to next msg(sqn={1})'s "
                  "index pos(={2}) of index file '{3}' failed",
                  next_msg_sqn, next_msg_index_pos, index_filebuf->GetFilePathStr());
        return kFailure;
    }
    else
    {
        LOG_INFO("reset to next msg(sqn={1})'s "
                 "index pos(={2}) of index file '{3}' ok",
                 next_msg_sqn, next_msg_index_pos, index_filebuf->GetFilePathStr());
    }

    return kSuccess;
}

// the common method to recovery index value from the index file
// this method was used without system crash
ErrorCode_def MessageTrack::RecoverIndexFile(FileWriteBuffer* index_filebuf,
                                             Message::SqnType& last_msg_sqn)
{
    std::string index_file_path_str = index_filebuf->GetFilePathStr();
    try
    {
        last_msg_sqn = OrdinalIndex::GetMsgSqn(bf::file_size(index_file_path_str));
    }
    catch (const bf::filesystem_error& e)
    {
        return kFailure;
    }

    index_filebuf->recover_next_msg_sqn(last_msg_sqn);
    const Message::SqnType next_msg_sqn  = last_msg_sqn + 1;
    const FilePosType next_msg_index_pos = OrdinalIndex::GetMsgIndexPos(next_msg_sqn);

    if (next_msg_index_pos != static_cast<FileWriteBuffer::FileBuffer&>(*index_filebuf).pubseekpos(next_msg_index_pos))
    {
        LOG_ERROR("reset to next msg(sqn={1})'s "
                  "index pos(={2}) of index file '{3}' failed",
                  next_msg_sqn, next_msg_index_pos, index_file_path_str);
        return kFailure;
    }
    else
    {
        LOG_INFO("reset to next msg(sqn={1})'s "
                 "index pos(={2}) of index file '{3}' ok",
                 next_msg_sqn, next_msg_index_pos, index_file_path_str);
    }

    return kSuccess;
}

// system crash/reboot
ErrorCode_def MessageTrack::RecoverIndexFile(FileWriteBuffer* index_filebuf,
                                             const Message::SqnType& last_msg_sqn,
                                             const LostMsg&)
{
    std::string index_file_path_str      = index_filebuf->GetFilePathStr();
    const Message::SqnType next_msg_sqn  = last_msg_sqn + 1;
    const FilePosType next_msg_index_pos = OrdinalIndex::GetMsgIndexPos(next_msg_sqn);
    if (!index_filebuf->Truncate(next_msg_index_pos)
        || (next_msg_index_pos != static_cast<FileWriteBuffer::FileBuffer&>(*index_filebuf).pubseekpos(next_msg_index_pos)))
    {
        LOG_ERROR("truncate to next msg(index sqn={1})'s "
                  "index pos(={2}) of index file '{3}' failed",
                  next_msg_sqn, next_msg_index_pos, index_file_path_str);
        return kFailure;
    }
    else
    {
        LOG_INFO("truncate to next msg(index sqn={1})'s "
                 "index pos(={2}) of index file '{3}' ok",
                 next_msg_sqn, next_msg_index_pos, index_file_path_str);
    }

    return kSuccess;
}

ErrorCode_def MessageTrack::RecoverIndexDataFiles()
{
    ///< 恢复全局索引文件
    IF_ERR_RET(RecoverIndexFile(ordinal_index_file_buf_, recorded_msg_cnt_));

    ///< 恢复消息数据文件
    std::string index_file_path_str = ordinal_index_file_buf_->GetFilePathStr();
    std::string data_file_path_str  = msg_data_file_buf_->GetFilePathStr();
    if (recorded_msg_cnt_ == 0)
    {
        cur_msgdata_filepos_ = 0;
    }
    else
    {
        RecordReader reader;
        std::filebuf index_file_buf;
        if (nullptr == index_file_buf.open(index_file_path_str.c_str(), std::ios_base::binary | std::ios_base::in))
        {
            LOG_ERROR("open to read index file '{1}' failed",
                      index_file_path_str);
            return kFailure;
        }

        FilePosType last_msg_index_file_pos = OrdinalIndex::GetMsgIndexPos(recorded_msg_cnt_);
        OrdinalIndex last_msg_ord_index;
        if (!reader.ReadOrdinalIndex(index_file_buf,
                                     last_msg_index_file_pos,
                                     last_msg_ord_index))
        {
            LOG_ERROR("read last msg(sqn={1})'s index(pos={2}) "
                      "from file '{3}' failed",
                      recorded_msg_cnt_, last_msg_index_file_pos,
                      index_file_path_str);
            return kFailure;
        }
        FilePosType last_msg_data_pos = last_msg_ord_index.GetPos();

        ///获取恢复前最后一个落地消息的总长度
        Message::SizeType last_msg_len;
        IF_ERR_RET(GetLastMessageLen(recorded_msg_cnt_, last_msg_len));
        cur_msgdata_filepos_ = last_msg_data_pos + (PosOffType)last_msg_len;
    }

    if (!DePreallocateDataFile())
    {
        return kFailure;
    }
    msgdata_len_inc_prealloc_ = cur_msgdata_filepos_;

    const auto phy_file_pos = GetMsgDataPhyFilePos(cur_msgdata_filepos_,
                                                   msg_data_file_header_)
                                  .second;
    const auto next_msg_sqn = recorded_msg_cnt_ + 1;
    if (phy_file_pos != static_cast<FileWriteBuffer::FileBuffer&>(*msg_data_file_buf_).pubseekpos(phy_file_pos))
    {
        LOG_ERROR("reset to next msg(sqn={1})'s data pos(={2}) "
                  "of file '{3}' failed",
                  next_msg_sqn, phy_file_pos, data_file_path_str);
        return kFailure;
    }
    else
    {
        std::size_t file_size = 0;
        if (msg_data_file_buf_->Stat(file_size) != ErrorCode::kSuccess)
        {
            return kFailure;
        }

        if (file_size < (std::size_t)phy_file_pos)
        {
            LOG_ERROR("the file {1} size {2} was invalid, expected file size {3} ",
                      data_file_path_str, file_size, phy_file_pos);
            return kFailure;
        }
        else if (file_size > (std::size_t)phy_file_pos)
        {
            if (msg_data_file_buf_->Truncate(phy_file_pos) == nullptr)
            {
                return kFailure;
            }
            // success
        }
        // else file_size == phy_file_pos
        
        LOG_INFO("reset to next msg(sqn={1})'s data pos(={2}) "
                 "of file '{3}' ok, orignal file size {4}",
                 next_msg_sqn, phy_file_pos, data_file_path_str, file_size);
    }

    return kSuccess;
}

void MessageTrack::Start()
{
    record_worker_->PostJob(boost::bind(&MessageTrack::RecordMessage, this));
    is_already_stop_track_ = false;
    LOG_INFO("message track {1} started", path_);
}

void MessageTrack::Stop()
{
    stopped_ = false;

    IntervalLogger inv_logger(5);
    while (false == stopped_)
    {
        usleep(1000);
        INV_LOG_INFO(inv_logger, "stopping actively...");
    }

    LOG_INFO("stopped");
}

void MessageTrack::DoDump(std::ostream& os) const
{
    os << "track(" << path_ << "): "
       << "message_memory_pool = " << app_msg_mp_manager_.GetMPTableName() << ", "
       << "message_queue = " << msg_ptr_queue_->name() << ", "
       << "message_recorded = " << recorded_msg_cnt_ << ", "
       << "message_filtered = " << filtered_msg_cnt_ << ", "
       << "message_filtered_repair = " << filtered_msg_cnt_repair_ << ", "
       << "error_happened = " << std::boolalpha << HasError() << ", "
       << "recovery_ok_from_remained_shared_memory = "
       << std::boolalpha << IsRecoveryOk() << ", "
       << "no_work_sleep_count = " << no_work_sleep_cnt_ << ", "
       << "yield_count = " << yield_cnt_ << ", "
       << "msgs_to_free = " << msgs_to_free_.size() << ", "
       << "is_doing_record = " << is_doing_record_;
}

void MessageTrack::DoDumpToPtree(bt::ptree& status_tree) const
{
    bt::ptree& track_status_tree = status_tree.add_child(path_, bt::ptree());

    track_status_tree.put("message_recorded", recorded_msg_cnt_);
    track_status_tree.put("message_filtered", filtered_msg_cnt_);
    track_status_tree.put("message_filtered_repair", filtered_msg_cnt_repair_);
    track_status_tree.put("error_happend", HasError());
    track_status_tree.put("recovery_ok_from_remained_shared_memory",
                          IsRecoveryOk());
    track_status_tree.put("no_work_sleep_count", no_work_sleep_cnt_);
    track_status_tree.put("yield_count", yield_cnt_);
}

bool MessageTrack::PreallocateDataFileUntilExit()
{
    while (true)
    {
        if (Recorder::to_exit())
            return false;

        const auto phyFilePos = GetMsgDataPhyFilePos(cur_msgdata_filepos_,
                                                     msg_data_file_header_)
                                    .second;
        if (nullptr == msg_data_file_buf_->Preallocate(phyFilePos, kDataFilePreAllocDefault))
        {
            continue;
        }

        msgdata_len_inc_prealloc_ = cur_msgdata_filepos_
            + (FilePosType)kDataFilePreAllocDefault;
        return true;
    }
}

/*****************************************************************************/

}  // namespace ami
