#include <fcntl.h>
#include <atomic>
#include <algorithm>
#include <unordered_set>

#include "record_data_tracker.h"

#include "recorded_message_index.h"
#include "record_file_header.h"

#include <adk/entry_wrapper.h>

#include <boost/exception/diagnostic_information.hpp>

namespace ami 
{
    
LOG_DEFINE(ami::RecordDataTracker)
 
constexpr Message::SqnType RecordDataTracker::kBegin;

constexpr Message::SqnType RecordDataTracker::kMostRecent;

constexpr int RecordDataTracker::kEndPointIdLength;
    
constexpr int RecordDataTracker::kTransPointIdLength;

constexpr uint64_t RecordDataTracker::kSecondToNanosecond;

constexpr uint64_t RecordDataTracker::kMilliSecondToNanosecond;
     
RecordDataTracker::~RecordDataTracker()
{
    if (is_running_)
    {
        Stop();    
    }
}  

ErrorCode_def RecordDataTracker::Init(const Property& props)
{
    context_name_ = props.GetValue(config::context::recorder::kName, std::string());
    auto recorder_props = props.GetValue(config::context::kRecorder, Property());
    
    recorder_data_root_path_ = recorder_props.GetValue(config::context::recorder::kDataPath, std::string());
                                            
    if (context_name_.empty())
    {
        LOG_ERROR("context_name is empty");
        return ErrorCode::kInvalidParameters;
    }
    
    if (recorder_data_root_path_.empty())
    {
        LOG_ERROR("recorder_data_root_path is empty");
        return ErrorCode::kInvalidParameters;
    }
    
    if (!boost::filesystem::exists(recorder_data_root_path_))
    {
        LOG_ERROR("recorder data path invalid, path:<{1}> don't exist", recorder_data_root_path_);
        return ErrorCode::kInvalidParameters;
    }

    boost::filesystem::path path(recorder_data_root_path_);
    path /= context_name_;
    if (!boost::filesystem::exists(path))
    {
        LOG_ERROR("context name invalid, path:<{1}> don't exist", path.string());
        return ErrorCode::kInvalidParameters;
    }

    watch_timeout_sec_ = recorder_props.GetValue(config::context::recorder::kWatchTimeoutSec, 10);
    
    read_thread_num_ = recorder_props.GetValue(config::context::recorder::kReadThreadNum, 1);
    
    if (watch_timeout_sec_ == 0
        || read_thread_num_ == 0)
    {
        LOG_ERROR("WatchTimeoutSec or ReadThreadNum is 0");
        return  ErrorCode::kInvalidParameters;
    }
    
    work_threads_.reserve(read_thread_num_);
    
    if (inotify_.Init(watch_timeout_sec_ * 1000) != adk_impl::ErrorCode::kSuccess)
    {
        LOG_ERROR("inotify init failed");
        return  ErrorCode::kFailure;
    }
    
    const char* env_str = std::getenv("AMI_RECORDER_NO_CRC_CHECK");
    if (env_str != nullptr 
        && (*env_str == 'Y' || *env_str == 'y' || *env_str == '1') )
    {
        is_check_crc_ = false;
    }
    
    return  ErrorCode::kSuccess;
}  

ErrorCode_def RecordDataTracker::Start()
{
    is_running_ = true;
    
    if (inotify_.Start() != adk_impl::ErrorCode::kSuccess)
    {
        LOG_ERROR("inotify start failed");
        return ErrorCode::kFailure;
    }
    
    for (size_t i = 0; i < read_thread_num_; ++i)
    {
        work_threads_.emplace_back(adk::boost_thread("record data track", 
                                                     "record data track thread", 
                                                      boost::bind(&RecordDataTracker::Run, this)));
    }
    
    LOG_INFO("record data tracker was started");
    
    return ErrorCode::kSuccess;
}

ErrorCode_def RecordDataTracker::Stop()
{
    is_running_ = false;
    
    for (auto& work_thread: work_threads_)
    {
        if(work_thread.joinable())
        {
            work_thread.join();
        }
    }
    
    inotify_.Stop(); 
    
    boost::lock_guard<boost::mutex> lock_guard(tracker_mutex_);
    for (auto iter = record_file_path_name_fd_map_.begin(); iter != record_file_path_name_fd_map_.end(); ++iter)
    {
        close(iter->second);
    }
    
    LOG_INFO("record data tracker was stopped");
    
    return ErrorCode::kSuccess;
}

ErrorCode_def RecordDataTracker::RegisterErrorHandler(const OnError on_error)
{
    if (on_error == nullptr)
    {
        LOG_ERROR("error handler is nullptr");
        return ErrorCode::kFailure;
    }
        
    boost::lock_guard<boost::mutex> lock_guard(tracker_mutex_);
    if (on_error_ != nullptr)
    {
        LOG_WARN("error handler was registered");
        return ErrorCode::kAlreadyInited;
    }

    on_error_ = on_error;
    
    return ErrorCode::kSuccess;
}             

ErrorCode_def RecordDataTracker::RegisterCompleteHandler(const OnComplete on_complete)
{
    if (on_complete == nullptr)
    {
        LOG_ERROR("complete handler is nullptr");
        return ErrorCode::kFailure;
    }
    
    boost::lock_guard<boost::mutex> lock_guard(tracker_mutex_);
    if (on_complete_ != nullptr)
    {
        LOG_WARN("complete handler was registered");
        return ErrorCode::kAlreadyInited;
    }
    
    on_complete_ = on_complete;
    
    return ErrorCode::kSuccess;
}   

int32_t RecordDataTracker::AsyncReadRxMessage(const std::string& context_name, 
                                             const OnMessage on_message,
                                             const Message::SqnType &begin,
                                             const Message::SqnType &end,
                                             const Property& props)
{
    return AsyncReadRecordMessage(context_name, record_file_t::rx_index, on_message, 0, begin, end, "", props);
}
                                            
                                            

int32_t RecordDataTracker::AsyncReadRxMessage(const std::string& context_name, 
                                            const uint64_t endpoint_id,
                                            const OnMessage on_message,
                                            const Message::SqnType &begin,
                                            const Message::SqnType &end,
                                            const Property& props)
{
    return AsyncReadRecordMessage(context_name, 
                                  record_file_t::rx_endpoint_index, 
                                  on_message, 
                                  endpoint_id, 
                                  begin, 
                                  end, 
                                  "", 
                                  props);
}                                       
                        
int32_t RecordDataTracker::AsyncReadRxStreamMessage(const std::string& context_name, 
                                                    const uint64_t stream_id,
                                                    const OnMessage on_message,
                                                    const Message::SqnType &begin,
                                                    const Message::SqnType &end,
                                                    const Property& props)     
{
    return AsyncReadRecordMessage(context_name, 
                                  record_file_t::rx_stream_index, 
                                  on_message, 
                                  stream_id, 
                                  begin, 
                                  end, 
                                  "", 
                                  props);
}
                                                                                        
int32_t RecordDataTracker::AsyncReadRxTransportMessage(const std::string& context_name, 
                                                        const uint64_t transport_id,
                                                        const OnMessage on_message,
                                                        const Message::SqnType &begin,
                                                        const Message::SqnType &end,
                                                        const Property& props)
{
    return AsyncReadRecordMessage(context_name, 
                                  record_file_t::rx_transport_index, 
                                  on_message, 
                                  transport_id, 
                                  begin, 
                                  end, 
                                  "", 
                                  props);
}

int32_t RecordDataTracker::AsyncReadTxMessage(const std::string& context_name, 
                                            const std::string& transport_name,
                                            const OnMessage on_message,
                                            const Message::SqnType &begin,
                                            const Message::SqnType &end,
                                            const Property& props)
{
    return AsyncReadRecordMessage(context_name, 
                                  record_file_t::tx_transport_index, 
                                  on_message, 
                                  0, 
                                  begin, 
                                  end, 
                                  transport_name, 
                                  props);
}

int32_t RecordDataTracker::AsyncReadTxMessage(const std::string& context_name, 
                                            const std::string& transport_name,
                                            const uint64_t  stream_id,
                                            const OnMessage on_message,
                                            const Message::SqnType &begin,
                                            const Message::SqnType &end,
                                            const Property& props)
{
    return AsyncReadRecordMessage(context_name, 
                                  record_file_t::tx_transport_stream_index, 
                                  on_message, 
                                  stream_id, 
                                  begin, 
                                  end, 
                                  transport_name, 
                                  props);
}

ErrorCode_def RecordDataTracker::GetRxMessageCount(const std::string& context_name, Message::SqnType& count)
{
    return GetMessageCount(context_name, record_file_t::rx_index, count);
}
    
ErrorCode_def RecordDataTracker::GetRxMessageCount(const std::string& context_name, const uint64_t endpoint_id, Message::SqnType& count)
{
    return GetMessageCount(context_name, record_file_t::rx_endpoint_index, count, endpoint_id);
}

ErrorCode_def RecordDataTracker::GetRxStreamMessageCount(const std::string& context_name, const uint64_t stream_id, Message::SqnType& count)
{
    return GetMessageCount(context_name, record_file_t::rx_stream_index, count, stream_id);
}
    
ErrorCode_def RecordDataTracker::GetRxTransportMessageCount(const std::string& context_name, const uint64_t transport_id, Message::SqnType& count)
{
    return GetMessageCount(context_name, record_file_t::rx_transport_index, count, transport_id);
}
    
ErrorCode_def RecordDataTracker::GetTxMessageCount(const std::string& context_name, const std::string& transport_name, Message::SqnType& count)
{
    return GetMessageCount(context_name, record_file_t::tx_transport_index, count, 0, transport_name);
}
    
ErrorCode_def RecordDataTracker::GetTxMessageCount(const std::string& context_name, 
                                                   const std::string& transport_name, 
                                                   const uint64_t stream_id,
                                                   Message::SqnType& count)
{
    return GetMessageCount(context_name, record_file_t::tx_transport_stream_index, count, stream_id, transport_name);
}

ErrorCode_def RecordDataTracker::StopAsyncReadMessage(const int32_t async_read_id)
{
    {
        boost::unique_lock<boost::mutex> pre_locker(pre_mutex_);
        auto iter = pre_async_tasks_map_.find(async_read_id);
        if (iter != pre_async_tasks_map_.end())   // 如果该任务还未开始运行，直接删除
        {
            pre_async_tasks_map_.erase(iter);
            auto itr = std::find(pre_wait_async_task_ids_.begin(),
                                 pre_wait_async_task_ids_.end(),
                                 async_read_id);
            if (itr != pre_wait_async_task_ids_.end())
            {
                pre_wait_async_task_ids_.erase(itr);
            }
        }
    }

    boost::unique_lock<boost::mutex> locker(task_mutex_);
    watch_async_task_ids_.erase(async_read_id);
    
    auto iter = std::find(wait_async_task_ids_.begin(), wait_async_task_ids_.end(), async_read_id);
    if (iter != wait_async_task_ids_.end())
    {
        wait_async_task_ids_.erase(iter);    
    }

    auto itr = async_tasks_map_.find(async_read_id);
    if (itr != async_tasks_map_.end())
    {
        auto task = itr->second;
        if (task != nullptr)
        {
            task->is_running_ = false;
        }
        async_tasks_map_.erase(itr);
    }
    else 
    {
        LOG_INFO("stop async read message failed, because of no this async task, async_read_id: <1>", async_read_id);
        return ErrorCode::kFailure;
    }

    LOG_INFO("stop async read message task <{1}> successful", async_read_id);

    return ErrorCode::kSuccess;
}       

static uint64_t GetNowTime();

ErrorCode_def RecordDataTracker::StopAllAsyncReadMessageComplete(uint32_t wait_timeout_milli)
{
    std::unordered_set<int32_t> read_task_ids;
    {
        boost::unique_lock<boost::mutex> pre_locker(pre_mutex_);
        for (auto iter = pre_async_tasks_map_.begin(); iter != pre_async_tasks_map_.end(); ++iter)  // 等待还未调用的异步任务完成
        {
            auto task_id = iter->first;
            auto task = iter->second;
            if (task->end_sqn_ != kMostRecent)
            {
                read_task_ids.insert(task_id);
            }
        }
    }

    boost::unique_lock<boost::mutex> locker(task_mutex_);
    auto tasks_map = async_tasks_map_;
    locker.unlock();

    for (auto iter = tasks_map.begin(); iter != tasks_map.end(); ++iter) // 等待已经调用的异步任务完成
    {
        auto task_id = iter->first;
        auto task = iter->second;
        if (task->end_sqn_ != kMostRecent)
        {
            read_task_ids.insert(task_id);
        }
    }

    bool is_finished = true;
    while (is_running_)
    {
        for (auto iter = read_task_ids.begin(); iter != read_task_ids.end();)
        {
            is_finished = true;
            {
                boost::unique_lock<boost::mutex> pre_locker(pre_mutex_);
                if (pre_async_tasks_map_.find(*iter) != pre_async_tasks_map_.end())
                {
                    is_finished = false;  // 如果该异步任务还没有开始，需要等待完成
                }
            }

            if (is_finished)
            {
                locker.lock();
                if (async_tasks_map_.find(*iter) != async_tasks_map_.end())  // 如果该异步任务还处于执行中，需要等待该任务执行完成
                {
                    is_finished = false;
                }
                locker.unlock();
            }

            if (is_finished)  // 如果该任务已经完成，从未完成任务中删除
            {
                iter = read_task_ids.erase(iter);
            }
            else 
            {
                ++iter;
            }
        }

        if (read_task_ids.empty())
        {
            break;
        }

        usleep(0);
    }
    
    if (wait_timeout_milli > 0)
    {
        auto wait_time = GetNowTime();
        while (is_running_)
        {
            usleep(1000);
            auto now_time = GetNowTime();
            if (now_time - wait_time >= (wait_timeout_milli * kMilliSecondToNanosecond))  // 等待超时
            {
                break;
            }
        }
    }

    locker.lock();
    for (auto iter = async_tasks_map_.begin(); iter != async_tasks_map_.end();)
    {
        if (iter->second != nullptr)
        {
            iter->second->is_running_ = false; 
        }

        iter = async_tasks_map_.erase(iter);
    }

    LOG_INFO("stop all async read message complete successful");

    return ErrorCode::kSuccess;
}    
                              
int32_t RecordDataTracker::AsyncReadRecordMessage(const std::string& context_name, 
                                            const record_file_t& record_file_type, 
                                            const OnMessage on_message,
                                            const uint64_t id, 
                                            const Message::SqnType &begin,
                                            const Message::SqnType &end,
                                            const std::string& transport_name,
                                            const Property& props)
{
    if (!is_running_)
    {
        LOG_ERROR("async read record message failed, because of this server was not running");
        return -1; 
    }
    
    if (on_message == nullptr 
       || (begin == kMostRecent)
       || (end != kMostRecent && begin >= end))
    {
        LOG_ERROR("async read record message failed, because of args is invalid");
        return -1; 
    }
    
    auto record_index_path = GetOpenRecordFilePath(context_name, record_file_type, id, transport_name);
    if (record_index_path.empty())
    {
        LOG_ERROR("async read record message failed, because of getting record index path failed");
        return -1;
    }
    
    record_file_t record_file_msg_data_type = record_file_t::rx_msg_data;
    if (record_file_type > record_file_t::rx_msg_data)
    {
        record_file_msg_data_type = record_file_t::tx_msg_data;
    }
    
    auto msg_data_path = GetOpenRecordFilePath(context_name, record_file_msg_data_type, id, transport_name);
    if (msg_data_path.empty())
    {
        LOG_ERROR("async read record message failed, because of getting msg data path failed");
        return -1;
    }
    
    auto index_fd = GetRecordFileDescriptor(record_index_path);
    if (index_fd < 0)
    {
        LOG_ERROR("get record file descriptor index fd failed");
        return -1;
    }
    
    auto msg_data_fd = GetRecordFileDescriptor(msg_data_path);
    if (msg_data_fd < 0)
    {
        LOG_ERROR("get record file descriptor msg data fd failed");
        return -1;
    }
    
    auto async_task_id = GetAddAsyncReadCount();
    
    bool msg_data_file_opt = false;
    int64_t record_file_head_len = 0; 
    if (GetMsgDataFileOpts(msg_data_path, record_file_head_len, msg_data_file_opt) != ErrorCode::kSuccess)
    {
        LOG_ERROR("get msg data file opts failed, msg_data_path: <{1}>", msg_data_path);
        return -1;
    }

    auto task_ptr =  std::make_shared<Task>(async_task_id, 
                                            context_name, 
                                            record_index_path, 
                                            msg_data_path, 
                                            index_fd,
                                            msg_data_fd,
                                            on_message, 
                                            begin, 
                                            end,
                                            record_file_head_len,
                                            msg_data_file_opt);
    if (task_ptr == nullptr 
       || task_ptr->ami_message_ == nullptr)
    {
        LOG_ERROR("task is null or ami_message is null");
        return -1;
    }

    task_ptr->ami_message_->reset<true>();
    task_ptr->ami_message_->message()->ResetAppMessage();
    if (record_file_msg_data_type == record_file_t::rx_msg_data)
    {
        task_ptr->ami_message_->ami_meta_data.ami_flags |= AMI_INGRESS_MESSAGE;    
    }
    else 
    {
        task_ptr->ami_message_->ami_meta_data.ami_flags = 0;
        task_ptr->is_rx_ = false;
    }

    std::string watch_name = task_ptr->index_path_ + "_" + std::to_string(task_ptr->id_);
    auto ret = inotify_.AddWatch(task_ptr->index_path_, 
                                  adk_impl::inotify_event_t::in_delete, 
                                  watch_name, 
                                  boost::bind(&RecordDataTracker::OnWatch,
                                              this, 
                                              boost::placeholders::_1, 
                                              boost::placeholders::_2, 
                                              boost::placeholders::_3,
                                              task_ptr->id_),
                                              false);
    if (ret != adk_impl::ErrorCode::kSuccess
        && ret != adk_impl::ErrorCode::kDuplicatedOption)
    {
        LOG_ERROR("add delete watch failed, path: <{1}>", task_ptr->index_path_);
        return -1;
    }

    boost::unique_lock<boost::mutex> pre_locker(pre_mutex_);
    pre_async_tasks_map_.insert({async_task_id, std::move(task_ptr)});
    pre_wait_async_task_ids_.emplace_back(async_task_id);

    LOG_INFO("async read record message successful, context name: <{1}>, task id: <{2}>",
             context_name, async_task_id);

    return async_task_id; 
}
                                    
int32_t RecordDataTracker::GetAddAsyncReadCount()
{
    return ++async_read_count_;
}

std::string RecordDataTracker::GetOpenRecordFilePath(const std::string& context_name,
                                                     const record_file_t& record_file_type, 
                                                     const uint64_t id,
                                                     const std::string& transport_name
                                                     )
{
    std::string path_name = recorder_data_root_path_ + "/" + context_name + "/";
    switch (record_file_type)
    {
    case record_file_t::rx_index:
        path_name += "rx/index";
        break;
    case record_file_t::rx_endpoint_index:
        path_name += "rx/ENDPOINT_ID-" + std::to_string(id) + "_index";
        break;
    case record_file_t::rx_stream_index:
        path_name += "rx/STREAM_ID-" + std::to_string(id) + "_index";
        break;
    case record_file_t::rx_transport_index:
        path_name += "rx/TRANSPORT_ID-" + std::to_string(id) + "_index";
        break;
    case record_file_t::rx_msg_data:
        path_name += "rx/msgdata_0";
        break;
    case record_file_t::tx_transport_stream_index:
        path_name += "tx/" + transport_name + "/STREAM_ID-" + std::to_string(id) + "_index";
        break;
    case record_file_t::tx_transport_index:
        path_name += "tx/" + transport_name + "/index";
        break;
    case record_file_t::tx_msg_data:
        path_name += "tx/" + transport_name + "/msgdata_0";
        break;
    default:
        path_name = std::string();
        break;
    }
    
    boost::lock_guard<boost::mutex> lock_guard(tracker_mutex_);
    if (record_file_path_name_fd_map_.count(path_name) == 0
        && is_running_)
    {
        if (!FileIsExisting(path_name))
        {
            LOG_ERROR("file <{1}> is not exist", path_name);
            return std::string();    
        }
        
        auto fd = open(path_name.c_str(), O_RDONLY, S_IRUSR);
        if (fd < 0)
        {
            LOG_ERROR("open file <{1}> failed", path_name);
            return std::string();
        }

        record_file_path_name_fd_map_.insert({path_name, fd});
    }
    
    return path_name;
}

bool RecordDataTracker::FileIsExisting(const std::string& file_path_name)
{
    struct stat buffer;
    return (stat(file_path_name.c_str(), &buffer) == 0);
}

int RecordDataTracker::GetRecordFileDescriptor(const std::string& file_path_name)
{
    int fd = -1;
    boost::lock_guard<boost::mutex> lock_guard(tracker_mutex_);
    if (record_file_path_name_fd_map_.count(file_path_name) != 0)
    {
        fd = record_file_path_name_fd_map_[file_path_name];
    }
    
    return fd;
}

ErrorCode_def RecordDataTracker::ReadOneMsgIndex(const int fd, 
                                                 const Message::SqnType& sqn, 
                                                 const int64_t header_len, 
                                                 char* buf,
                                                 int64_t& cur_msg_pos)
{
    auto cur_msg_index_pos = OrdinalIndex::GetMsgIndexPos(sqn);
    int index_size = sizeof(OrdinalIndex);   
    
    if (index_size != pread64(fd, buf, index_size, cur_msg_index_pos))
    {
        LOG_ERROR("read one msg index failed, cur msg index pos: <{1}>", cur_msg_index_pos);
        return ErrorCode::kFailure;    
    }
    
    auto* ordinal_index = reinterpret_cast<OrdinalIndex*>(buf);
    if (ordinal_index == nullptr)
    {
        LOG_ERROR("read one msg index failed, ordinal index is null");
        return ErrorCode::kFailure;
    }
    
    auto cur_msg_index = static_cast<int64_t>(ordinal_index->GetPos());
    cur_msg_pos = cur_msg_index + header_len;
    
    return ErrorCode::kSuccess;
}

ErrorCode_def RecordDataTracker::ReadEndPointId(const int fd, 
                                                const bool file_opt,
                                                MsgCRCType& crc,
                                                int64_t& offset, 
                                                AmiMessage& ami_message)
{ 
    if (kEndPointIdLength != pread64(fd, (char*)&ami_message.ami_meta_data.endpoint_id, kEndPointIdLength, offset))
    {
        LOG_ERROR("read endpoint id failed");
        return ErrorCode::kFailure;
    }
    
    if (is_check_crc_ && file_opt)
    {
        crc = MsgCRCCalFunc(&ami_message.ami_meta_data.endpoint_id, kEndPointIdLength, crc);
    }
    
    offset += kEndPointIdLength;
    
    return ErrorCode::kSuccess;
}

ErrorCode_def  RecordDataTracker::ReadTransportId(const int fd, 
                                                  const bool file_opt,
                                                  MsgCRCType& crc, 
                                                  int64_t& offset,
                                                  AmiMessage& ami_message)
{    
    if (kTransPointIdLength != pread64(fd, (char*)&ami_message.ami_meta_data.transport_id, kTransPointIdLength, offset))
    {
        LOG_ERROR("read transport id failed");
        return ErrorCode::kFailure;
    }
    
    if (is_check_crc_ && file_opt)
    {
        crc = MsgCRCCalFunc(&ami_message.ami_meta_data.transport_id, kTransPointIdLength, crc);
    }
    
    offset += kTransPointIdLength;
    
    return ErrorCode::kSuccess;
}

ErrorCode_def RecordDataTracker::ReadOneAppMessageData(const int fd, 
                                                       const bool file_opt,
                                                       MsgCRCType& crc,
                                                       int64_t& offset, 
                                                       AmiMessage& ami_message)
{   
    Message& app_msg = *(ami_message.message());
    
    const auto len_sqn_to_read = sizeof(app_msg.stream_sqn) + sizeof(app_msg.topic_sqn);
    if(len_sqn_to_read != pread64(fd, (char*)&app_msg.stream_sqn, len_sqn_to_read, offset))
    {
        LOG_ERROR("read sqn failed");
        return ErrorCode::kFailure;
    }
    offset += len_sqn_to_read;
    
    const auto app_data_len = sizeof(app_msg.app_data_len);
    if (app_data_len != pread64(fd, (char*)&app_msg.app_data_len, app_data_len, offset))
    {
        LOG_ERROR("read app data len failed");
        return ErrorCode::kFailure;
    }
    offset += app_data_len;

    if (app_msg.app_data_len > AMI_MAX_MESSAGE_SIZE_INTERNAL)
    {
        LOG_ERROR("unexpected message data len(={1})", app_msg.app_data_len);   
        return ErrorCode::kFailure;
    }

    const auto msg_header_len = sizeof(MsgRecord::msg_header);
    if (msg_header_len != sizeof(app_msg.msg_header))
    {
        LOG_ERROR("msg header len is not equal app_msg.msg_header length");
        return ErrorCode::kFailure;
    }
    
    if (msg_header_len != pread64(fd, (char*)&app_msg.msg_header, msg_header_len, offset))
    {
        LOG_ERROR("read msg header failed");
        return ErrorCode::kFailure;
    }
    offset += msg_header_len;

    recorder::ExMessageHeader ex_msg_header;
    const auto ex_msg_header_len = sizeof(MsgRecord::ex_msg_header);
    if (ex_msg_header_len != pread64(fd, (char*)&ex_msg_header, ex_msg_header_len, offset))
    {
        LOG_ERROR("read ex msg header failed");
        return ErrorCode::kFailure;
    }
    offset += ex_msg_header_len;
    
    ami_message.ami_meta_data.c_stream_sqn                      = ex_msg_header.c_stream_sqn;
    ami_message.ami_meta_data.c_topic_sqn                       = ex_msg_header.c_topic_sqn;
    ami_message.ami_meta_data.c_endpoint_sqn                    = ex_msg_header.c_endpoint_sqn;
    (*(RecordedMsgPropUnion*)(&app_msg.ex_msg_header)).msg_prop = ex_msg_header.msg_prop;
    ami_message.ami_meta_data.ami_persistent_context_0          = ex_msg_header.ami_user_context_0;
    ami_message.ami_meta_data.ami_persistent_context_1          = ex_msg_header.ami_user_context_1;
    ami_message.ami_meta_data.recorder_receive_msg_time_ns      = ex_msg_header.recorder_receive_msg_time_ns;
    
    if (app_msg.app_data_len != pread64(fd, (char*)app_msg.app_data_begin, app_msg.app_data_len, offset))
    {
        LOG_ERROR("read app data failed");
        return ErrorCode::kFailure;
    }
                
    offset += app_msg.app_data_len;
    
    
    if (is_check_crc_ && file_opt)
    {  //FIXME: add recorder::ex_msg_header, exclude msg props
        crc = MsgCRCCalFunc(&app_msg.stream_sqn,
                                sizeof(app_msg.stream_sqn) + sizeof(app_msg.topic_sqn),
                                crc);
        crc = MsgCRCCalFunc(&app_msg.app_data_len,
                                sizeof(app_msg.app_data_len),
                                crc);
        crc = MsgCRCCalFunc(&app_msg.msg_header,
                                sizeof(MessageHeader) + app_msg.app_data_len,
                                crc);
    }
    
    return ErrorCode::kSuccess;
}

ErrorCode_def RecordDataTracker::ReadOneAppMessage(const int fd, 
                                                   const int64_t msg_pos,
                                                   const bool is_rx,
                                                   const bool file_opt,
                                                   MsgCRCType& crc, 
                                                   AmiMessage& ami_message, 
                                                   const RMPropType& msg_prop)
{   
    auto cur_offset = msg_pos;
    if (is_rx)
    {
        if (ReadEndPointId(fd, file_opt, crc, cur_offset, ami_message) != ErrorCode::kSuccess)
        {
            LOG_ERROR("read endpoint id failed");
            return ErrorCode::kFailure;
        }
        
        if (ReadTransportId(fd, file_opt, crc, cur_offset, ami_message) != ErrorCode::kSuccess)
        {
            LOG_ERROR("read transport id failed");
            return ErrorCode::kFailure;
        }    
    }
    
    if (ReadOneAppMessageData(fd, file_opt, crc, cur_offset, ami_message) != ErrorCode::kSuccess)
    {
        LOG_ERROR("read one app message data failed");
        return ErrorCode::kFailure;
    }
    
    if (file_opt 
        && is_check_crc_
        && (!CheckCRC(fd, cur_offset, crc)))
    {
        LOG_ERROR("CheckCRC failed, msg pos: <{1}>, transport-id <{2}>, topic sqn <{3}>",
                  msg_pos,
                  ami_message.ami_meta_data.transport_id,
                  ami_message.ami_meta_data.c_topic_sqn);
        return ErrorCode::kFailure;
    }

    if (msg_prop)
    {
        Message& app_msg = *(ami_message.message());
        
        RecordedMsgPropUnion prop_union = {app_msg.ex_msg_header};
        if (prop_union.msg_prop != *msg_prop)
        {
            prop_union.msg_prop   = *msg_prop;
            app_msg.ex_msg_header = prop_union.orig_holder;
            
            cur_offset += 2 * sizeof(Message::IDType) + kAppmsgPropOffset;

            const auto len_to_write = sizeof(prop_union.msg_prop);
                                     
            if (len_to_write != pread64(fd, (char*)&prop_union, len_to_write, cur_offset))
            {
                LOG_ERROR("read prop_union failed");
                return ErrorCode::kFailure;
            }
        }
    }
    
    return ErrorCode::kSuccess;
}

ErrorCode_def RecordDataTracker::GetLastMsgSqn(const int fd, Message::SqnType& last_msg_sqn)
{
    struct stat index_file_stat;
    if (::fstat(fd, &index_file_stat) != 0)
    {
        LOG_ERROR("get latest msg sqn failed, fd: <{1}>", fd);
        return ErrorCode::kFailure;
    }
    
    auto index_file_size = index_file_stat.st_size;
    
    last_msg_sqn = OrdinalIndex::GetMsgSqn(index_file_size);
    
    return ErrorCode::kSuccess;
}

ErrorCode_def RecordDataTracker::OnWatch(const std::string& path_name, 
                                         const adk_impl::inotify_event_t& event, 
                                         const std::string& watch_name,
                                         const int32_t async_task_id)
{
    MovePreTask();

    boost::unique_lock<boost::mutex> locker(task_mutex_);

    auto iter = async_tasks_map_.find(async_task_id);
    if (iter == async_tasks_map_.end())
    {
        LOG_INFO("async tasks map has no this aysnc task id, async_task_id: <{1}>", async_task_id);
        watch_async_task_ids_.erase(async_task_id);
        return adk_impl::ErrorCode::kFailure;
    }

    auto task = iter->second;
    if (task == nullptr)
    {
        LOG_WARN("task is null, async_task_id: <{1}>", async_task_id);
        watch_async_task_ids_.erase(async_task_id);
        return adk_impl::ErrorCode::kFailure;
    }

    locker.unlock();

    if (event == adk_impl::inotify_event_t::in_modify)
    {
        task->last_read_time_ = 0;
    }
    else if (event == adk_impl::inotify_event_t::in_delete)
    {
        locker.lock();
        watch_async_task_ids_.erase(async_task_id);
        locker.unlock();

        LOG_WARN("file <{1}> was deleted", task->index_path_);
        
        std::string err_msg = task->index_path_ + " was deleted";
        if (on_error_ != nullptr)
        {
            on_error_(async_task_id, err_msg);
        }
        return adk_impl::ErrorCode::kFailure;   
    }

    LOG_DEBUG("watch event happen, watch_name: <{1}>", watch_name);

    return adk_impl::ErrorCode::kSuccess;
}

ErrorCode_def RecordDataTracker::GetMsgDataFileOpts(const std::string& path_name, int64_t& record_file_head_len, bool& record_file_opt)
{
    RecordFileHdr record_file_header = RecordFileHdr();
    if (!record_file_header.Read(path_name))
    {
        LOG_ERROR("record file header read path <{1}> failed", path_name);
        return ErrorCode::kFailure;
    }
    
    record_file_head_len = record_file_header.HeaderLength();
    record_file_opt = (!!(record_file_header.GetFileOpts() & FileOpts::kCrc));
    
    return ErrorCode::kSuccess;
}

ErrorCode_def RecordDataTracker::GetMessageCount(const std::string& context_name, 
                                                 const record_file_t& record_file_type, 
                                                 Message::SqnType& count,
                                                 const uint64_t id, 
                                                 const std::string& transport_name)
{
    auto record_index_path = GetOpenRecordFilePath(context_name, record_file_type, id, transport_name);
    if (record_index_path.empty())
    {
        LOG_ERROR("record index path is empty, context_name: <{1}>, record_file_type: <{2}>, id: <{3}>, transport_name: <{4}>",
                  context_name, static_cast<uint8_t>(record_file_type), id, transport_name);
        return ErrorCode::kFailure;
    }

    int index_fd = 0;
    bool is_need_file = false;
    if (is_running_)
    {
        index_fd = GetRecordFileDescriptor(record_index_path);
        if (index_fd < 0)
        {
            LOG_ERROR("get index fd failed, context_name: <{1}>, record_file_type: <{2}>, id: <{3}>, transport_name: <{4}>",
                    context_name, static_cast<uint8_t>(record_file_type), id, transport_name);
            return ErrorCode::kFailure;
        }
    } 
    else 
    {
        index_fd = open(record_index_path.c_str(), O_RDONLY, S_IRUSR);
        if (index_fd < 0)
        {
            LOG_ERROR("open file <{1}> failed", record_index_path);
            return ErrorCode::kFailure;
        }
        is_need_file = true;
    }

    auto ec = GetLastMsgSqn(index_fd, count);
    if (is_need_file)
    {
        close(index_fd);
    }

    return ec;
}

ErrorCode_def RecordDataTracker::ReadHisMessage(const std::shared_ptr<Task>& task,
                                                Message::SqnType& cur_sqn,
                                                const RMPropType& msg_prop)
{
    if (task->ami_message_ == nullptr
        || task->on_message_ == nullptr)
    {
        LOG_ERROR("read his message failed, ami_message_ is null or on_message_ is null");
        return ErrorCode::kFailure;
    }
    
    cur_sqn = task->begin_sqn_ - 1;
    
    Message::SqnType last_msg_sqn = 0;
    if(ErrorCode::kSuccess != GetLastMsgSqn(task->index_fd_, last_msg_sqn))
    {
        LOG_ERROR("get last msg sqn failed, index_fd_: <{1}>", task->index_fd_);
        return ErrorCode::kFailure;
    }
    
    if (cur_sqn >= last_msg_sqn)
    {
        return ErrorCode::kTryAgain;
    }
    
    auto end_msg_sqn = last_msg_sqn;    
    if (task->end_sqn_ != kMostRecent && task->end_sqn_ <= last_msg_sqn)
    {
        end_msg_sqn = task->end_sqn_ - 1;
    }    

    
    int64_t  cur_msg_index = 0;
    
    for (auto cur_msg_sqn = task->begin_sqn_; cur_msg_sqn <= end_msg_sqn; ++cur_msg_sqn)
    { 
        if (!task->is_running_)
        {
            LOG_WARN("task <{1}> was stopped", task->id_);
            return ErrorCode::kFailure;
        }

        if (ReadOneMsgIndex(task->index_fd_, cur_msg_sqn, task->record_file_head_len_, task->ami_index_buf_, cur_msg_index) != kSuccess)
        {
            if (on_error_ != nullptr)
            {
                on_error_(task->id_, "read one msg index failed");
            }
            LOG_ERROR("read one msg index failed");
            return ErrorCode::kFailure;
        }
        
        if (ReadOneAppMessage(task->msg_data_fd_, cur_msg_index, task->is_rx_, task->record_file_opt_, task->crc_, *task->ami_message_, msg_prop) != kSuccess)
        {
            if (on_error_ != nullptr)
            {
                on_error_(task->id_, "read one app message faild");
            }
            LOG_ERROR("read one app message failed");
            return ErrorCode::kFailure;
        }
        
        if (task->on_message_(cur_msg_sqn, task->ami_message_->message()) != kSuccess)
        {
            LOG_ERROR("on_message_ exec failed");
            return ErrorCode::kFailure;
        } 
    }
    
    cur_sqn = end_msg_sqn;
    
    if (task->end_sqn_ == kMostRecent || cur_sqn < task->end_sqn_ - 1)
    {
        return ErrorCode::kTryAgain; 
    }
    
    return ErrorCode::kSuccess;
}

bool RecordDataTracker::CheckCRC(const int fd, const int64_t cur_offset, MsgCRCType& crc)
{
    MsgCRCType local_crc = {0};
    
    const auto len_to_read = sizeof(MsgCRCType);
    if (len_to_read != pread64(fd, (char*)&local_crc, len_to_read, cur_offset))
    {
        return false;
    }

    if (local_crc != crc)
    {
        //LOG_ERROR("message CRC check failed");
        return false;
    }

    crc = 0;   // reset crc for next message
    return true;
}

std::shared_ptr<RecordDataTracker::Task> RecordDataTracker::GetTask()
{
    MovePreTask();

    boost::unique_lock<boost::mutex> locker(task_mutex_);
    if (!wait_async_task_ids_.empty())
    {
        auto async_task_id = wait_async_task_ids_.front();
        wait_async_task_ids_.pop_front();
        auto iter = async_tasks_map_.find(async_task_id);
        if (iter != async_tasks_map_.end())
        {
            return iter->second;
        }
    }

    if (wait_async_task_ids_.empty())
    {
        usleep(0);
    }

    return nullptr;
}

void RecordDataTracker::RemoveTask(const int32_t async_task_id)
{
    boost::unique_lock<boost::mutex> locker(task_mutex_);
    async_tasks_map_.erase(async_task_id);
    watch_async_task_ids_.erase(async_task_id);

    LOG_INFO("remove task <{1}> successful", async_task_id);
}

static uint64_t GetNowTime()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    
    uint64_t now = ts.tv_sec * RecordDataTracker::kSecondToNanosecond + ts.tv_nsec;
    
    return now;
}

void RecordDataTracker::Run()
{
    const auto watch_timeout_nano_second = kSecondToNanosecond * watch_timeout_sec_;
    while (is_running_)
    {
        try 
        {   
            auto task = GetTask();

            if (task == nullptr 
                || task->ami_message_ == nullptr
                || task->on_message_ == nullptr)
            {
                continue;
            }
            
            if (task->end_sqn_ == kMostRecent)
            {
                auto nows = GetNowTime();
                if (task->last_read_time_ >= nows)
                {
                    RemoveTask(task->id_);
                    continue;
                }
                
                if (nows - task->last_read_time_ < watch_timeout_nano_second)
                {
                    boost::unique_lock<boost::mutex> locker(task_mutex_);
                    wait_async_task_ids_.push_back(task->id_);
                    continue;
                }
            }
            
            Message::SqnType cur_msg_sqn = 0;
            
            retry:   
                auto ret = ReadHisMessage(task, cur_msg_sqn);           
                if(ErrorCode::kSuccess != ret && ErrorCode::kTryAgain != ret)
                {
                    RemoveTask(task->id_);
                    continue;
                }

                if (ErrorCode::kTryAgain == ret)
                {
                    if (cur_msg_sqn >= task->begin_sqn_)
                    {
                        task->begin_sqn_ = cur_msg_sqn + 1;    
                    }
                    
                    if (task->end_sqn_ == kMostRecent)
                    {
                        boost::unique_lock<boost::mutex> locker(task_mutex_);
                        if (async_tasks_map_.count(task->id_) == 0)
                        {
                            watch_async_task_ids_.erase(task->id_); 
                            continue;
                        }

                        auto nows = GetNowTime();
                        task->last_read_time_ = nows;

                        std::string watch_name = task->index_path_ + "_" + std::to_string(task->id_);

                        if (watch_async_task_ids_.count(task->id_) == 0)
                        {
                            locker.unlock();    
                            auto ret = inotify_.AddWatch(task->index_path_, 
                                                        adk_impl::inotify_event_t::in_modify, 
                                                        watch_name, 
                                                        boost::bind(&RecordDataTracker::OnWatch,
                                                                    this, 
                                                                    boost::placeholders::_1, 
                                                                    boost::placeholders::_2, 
                                                                    boost::placeholders::_3,
                                                                    task->id_),
                                                        false);
                            if (ret != adk_impl::ErrorCode::kSuccess
                               && ret != adk_impl::ErrorCode::kDuplicatedOption)
                            {
                                locker.lock();
                                async_tasks_map_.erase(task->id_);
                                continue;
                            }
                            locker.lock();
                            watch_async_task_ids_.insert(task->id_);
                        }

                        wait_async_task_ids_.push_back(task->id_);
                    }
                    else
                    {
                        if (cur_msg_sqn < task->end_sqn_ - 1)
                        {
                            goto retry;
                        }
                    }
                }
                else if (on_complete_ != nullptr)
                {
                    LOG_INFO("async read task <{1}> call complete callback start", task->id_);
                    on_complete_(task->id_);
                    LOG_INFO("async read task <{1}> call complete callback finished", task->id_);

                    RemoveTask(task->id_);
                }
        } 
        catch(const std::exception& e)
        {
            LOG_WARN("catch exception, what <{1}>", e.what());
        }
    }
}

void RecordDataTracker::MovePreTask()
{
    boost::unique_lock<boost::mutex> pre_locker(pre_mutex_);  // 说明有新增的异步任务，需要移动到调度队列
    if (!pre_async_tasks_map_.empty()
        || !pre_wait_async_task_ids_.empty())
    {
            for (auto iter = pre_async_tasks_map_.begin();
                iter != pre_async_tasks_map_.end();
                ++iter)
        {
            boost::unique_lock<boost::mutex> locker(task_mutex_);
            async_tasks_map_[iter->first] = iter->second;
        }

        for (auto iter = pre_wait_async_task_ids_.begin();
                iter != pre_wait_async_task_ids_.end();
                ++iter)
        {
            boost::unique_lock<boost::mutex> locker(task_mutex_);
            wait_async_task_ids_.push_back(*iter);
        }

        pre_async_tasks_map_.clear();
        pre_wait_async_task_ids_.clear();
    }
}

}