#ifndef AMI_RECORD_DATA_TRACKER_H_ 
#define AMI_RECORD_DATA_TRACKER_H_ 

#include <functional>
#include <unordered_map>
#include <map>
#include <unordered_set>
#include <memory>
#include <utility>
#include <atomic>

#include <adk/util.h>
#include <adk/inotify_wrapper.h>

#include <ami/error_code.h>
#include <ami/config_key.h>
#include <ami/message.h>
#include <ami/property.h>

#include "recorder_base.h"
#include "record_file_header.h"
#include "../ami_message.h"
#include "../ami_constant.h"
#include "../log.h"

#include <boost/thread/thread.hpp>
#include <boost/optional.hpp>

namespace ami 
{
    
/**
 * @brief      跟踪record数据变化
 *
 */
class RecordDataTracker
{
public:

    static constexpr Message::SqnType kBegin = 1u;

    static constexpr Message::SqnType kMostRecent = 0u;

    static constexpr int kEndPointIdLength = sizeof(AmiMetaData::endpoint_id);
        
    static constexpr int kTransPointIdLength = sizeof(AmiMetaData::transport_id);
    
    static constexpr uint64_t kSecondToNanosecond = 1000ul * 1000ul * 1000ul;
    
    static constexpr uint64_t kMilliSecondToNanosecond = 1000ul * 1000ul;
    
    static constexpr uint32_t kAmiIndexSize = 128u;

    /**
     * @brief       读取recorder消息回调函数
     * 
     * @param[in]   sqn   表示该消息对应的序号
     * @param[in]   msg   表示一条AMI消息   
     * 
     * @return      读取成功返回kSuccess
     *              读取失败返回kFailure
     */
    using OnMessage = std::function<ErrorCode(const Message::SqnType sqn, ami::Message* msg)>;
    
    /**
     * @brief       错误回调函数，当有严重错误发生时调用此函数(比如读取过程中持久化文件被删除)
     *  
     * @param[in]   async_task_id 表示发生错误事件的任务id
     * @param[in]   err_msg       表示错误事件的描述信息   
     * 
     * @return      无
     */
    using OnError = std::function<void(const int32_t async_task_id, const std::string& err_msg)>;
    
    /**
     * @brief       异步任务完成回调函数
     *  
     * @param[in]   async_task_id 表示发生错误事件的任务id
     * 
     * @return      无
     */
    using OnComplete = std::function<void(const int32_t async_task_id)>;
    
    typedef boost::optional<RecordedMsgProp> RMPropType;
    
    RecordDataTracker() = default;
    ~RecordDataTracker();
    
    RecordDataTracker(const RecordDataTracker&) = delete;
    RecordDataTracker& operator=(const RecordDataTracker&) = delete;
    
    /**
     * @brief      初始化RecordDataTracker
     *
     * @param[in]  props         指定属性：kContextName  指定context
     *                                     kRecorder     指定recorder的属性: kDataPath                  指定数据目录
     *                                                                       kWatchTimeoutSec           指定index文件观察超时时间
     *                                                                       kReadTheadNum              指定工作线程数目
     * @return     成功时返回 ErrorCode::kSuccess
     */
    ErrorCode_def Init(const Property& props);
    
    ErrorCode_def Start();
    
    ErrorCode_def Stop();

    /**
     * @brief      注册错误处理回调函数
     *
     * @param[in]  on_error  错误处理回调函数
     * 
     * @return     成功时返回kSuccess, 已经注册则返回kAlreadyInited, 如果on_error为nullptr，返回kFailure
     */                              
    ErrorCode_def RegisterErrorHandler(const OnError on_error);
    
     /**
     * @brief      注册任务完成回调函数
     *
     * @param[in]  on_complete  任务完成回调函数
     * 
     * @return     成功时返回kSuccess, 已经注册则返回kAlreadyInited, 如果on_complete为nullptr，返回kFailure
     */  
    ErrorCode_def RegisterCompleteHandler(const OnComplete on_complete);

    /**
     * @brief      读取Rx方向上某个context的所有主题的持久化消息，如果end不等于kMostRecent，则读取[begin, end)内的消息
     *
     * @param[in]  context_name     指定context
     * @param[in]  on_message       当读取完消息后执行的回调函数
     * @param[in]  begin            指定读取消息的起始序号，默认为1
     * @param[in]  end              指定读取消息的结束序号（读取的消息中不包括此序号消息，默认为kMostRecent，表示持续读取最新消息）
     * @param[in]  props            指定属性: RetryTimeoutMilli：指定读取超时时间
     *                                        RetryIntervalMilli：指定读取重试时间间隔     
     *                                        
     *                     
     * @return     成功时返回async_task_id(大于0)，失败返回-1
     */
    int32_t AsyncReadRxMessage(const std::string& context_name, 
                              const OnMessage on_message,
                              const Message::SqnType &begin = kBegin,
                              const Message::SqnType &end = kMostRecent,
                              const Property& props = Property()
                              );
                                        
                                        
    /**
     * @brief      读取Rx方向上某个context的指定主题的持久化消息，如果end不等于kMostRecent，则读取[begin, end)内的消息
     *
     * @param[in]  context_name     指定context
     * @param[in]  endpoint_id      指定主题
     * @param[in]  on_message       当读取完消息后执行的回调函数
     * @param[in]  begin            指定读取消息的起始序号，默认为1
     * @param[in]  end              指定读取消息的结束序号（读取的消息中不包括此序号消息，默认为kMostRecent，表示持续读取最新消息）
     * @param[in]  props            指定属性: RetryTimeoutMilli：指定读取超时时间
     *                                        RetryIntervalMilli：指定读取重试时间间隔
     *                                       
     *                    
     * @return     成功时返回async_task_id(大于0)，失败返回-1
     */
    int32_t AsyncReadRxMessage(const std::string& context_name, 
                              const uint64_t endpoint_id,
                              const OnMessage on_message,
                              const Message::SqnType &begin = kBegin,
                              const Message::SqnType &end = kMostRecent,
                              const Property& props = Property()
                              );
                                    
    /**
     * @brief      读取Rx方向上某个context的指定流的持久化消息，如果end不等于kMostRecent，则读取[begin, end)内的消息
     *
     * @param[in]  context_name     指定context
     * @param[in]  stream_id        指定流
     * @param[in]  on_message       当读取完消息后执行的回调函数
     * @param[in]  begin            指定读取消息的起始序号，默认为1
     * @param[in]  end              指定读取消息的结束序号（读取的消息中不包括此序号消息，默认为kMostRecent，表示持续读取最新消息）
     * @param[in]  props            指定属性: RetryTimeoutMilli：指定读取超时时间
     *                                        RetryIntervalMilli：指定读取重试时间间隔 
     *                                         
     *                        
     * @return     成功时返回async_task_id(大于0)，失败返回-1
     */
    int32_t AsyncReadRxStreamMessage(const std::string& context_name, 
                                    const uint64_t stream_id,
                                    const OnMessage on_message,
                                    const Message::SqnType &begin = kBegin,
                                    const Message::SqnType &end = kMostRecent,
                                    const Property& props = Property()
                                    );
                                    
    /**
     * @brief      读取Rx方向上某个context的指定transport的持久化消息，如果end不等于kMostRecent，则读取[begin, end)内的消息
     *
     * @param[in]  context_name     指定context
     * @param[in]  transport_id     指定transport_id
     * @param[in]  on_message       当读取完消息后执行的回调函数
     * @param[in]  begin            指定读取消息的起始序号，默认为1
     * @param[in]  end              指定读取消息的结束序号（读取的消息中不包括此序号消息，默认为kMostRecent，表示持续读取最新消息）
     * @param[in]  props            指定属性: RetryTimeoutMilli：指定读取超时时间
     *                                        RetryIntervalMilli：指定读取重试时间间隔  
     *                                       
     *     
     * @return     成功时返回async_task_id(大于0)，失败返回-1
     */
    int32_t AsyncReadRxTransportMessage(const std::string& context_name, 
                                        const uint64_t transport_id,
                                        const OnMessage on_message,
                                        const Message::SqnType &begin = kBegin,
                                        const Message::SqnType &end = kMostRecent,
                                        const Property& props = Property()
                                        );  
                                                               
    /**
     * @brief      读取Tx方向上某个context的指定transport的持久化消息，如果end不等于kMostRecent，则读取[begin, end)内的消息
     *
     * @param[in]  context_name     指定context
     * @param[in]  transport_name   指定tansport_name
     * @param[in]  on_message       当读取完消息后执行的回调函数
     * @param[in]  begin            指定读取消息的起始序号，默认为1
     * @param[in]  end              指定读取消息的结束序号（读取的消息中不包括此序号消息，默认为kMostRecent，表示持续读取最新消息）
     * @param[in]  props            指定属性: RetryTimeoutMilli：指定读取超时时间
     *                                        RetryIntervalMilli：指定读取重试时间间隔  
     *                                        
     *                     
     * @return     成功时返回async_task_id(大于0)，失败返回-1
     */
    int32_t AsyncReadTxMessage(const std::string& context_name, 
                              const std::string& transport_name,
                              const OnMessage on_message,
                              const Message::SqnType &begin = kBegin,
                              const Message::SqnType &end = kMostRecent,
                              const Property& props = Property()
                             );
                                        
                                        
    /**
     * @brief      读取Tx方向上某个context的指定transport的持久化消息，如果end不等于kMostRecent，则读取[begin, end)内的消息
     *
     * @param[in]  context_name     指定context
     * @param[in]  transport_name   指定transport
     * @param[in]  stream_id        指定stream
     * @param[in]  on_message       当读取完消息后执行的回调函数
     * @param[in]  begin            指定读取消息的起始序号，默认为1
     * @param[in]  end              指定读取消息的结束序号（读取的消息中不包括此序号消息，默认为kMostRecent，表示持续读取最新消息）
     * @param[in]  props            指定属性: RetryTimeoutMilli：指定读取超时时间
     *                                        RetryIntervalMilli：指定读取重试时间间隔
     *                                       
     *                
     * @return     成功时返回async_task_id(大于0)，失败返回-1
     */
    int32_t AsyncReadTxMessage(const std::string& context_name, 
                              const std::string& transport_name,
                              const uint64_t  stream_id,
                              const OnMessage on_message,
                              const Message::SqnType &begin = kBegin,
                              const Message::SqnType &end = kMostRecent,
                              const Property& props = Property()
                             );                    
    
    /**
     * @brief      读取Rx方向所有持久化消息的数量
     *
     * @param[in]  context_name  指定context
     * 
     * @param[out] count         消息数量
     * 
     * @return     成功时返回kSuccess, 文件不存在或读取失败kFailure
     */    
    ErrorCode_def GetRxMessageCount(const std::string& context_name, Message::SqnType& count);
    
    /**
     * @brief      读取Rx方向某个主题的持久化消息的数量
     *
     * @param[in]  context_name  指定context
     * @param[in]  endpoint_id   指定主题
     * 
     * @param[out] count         消息数量
     * 
     * @return     成功时返回kSuccess, 文件不存在或读取失败kFailure
     */ 
    ErrorCode_def GetRxMessageCount(const std::string& context_name, const uint64_t endpoint_id, Message::SqnType& count);
    
    /**
     * @brief      读取Rx方向某个流的持久化消息的数量
     *
     * @param[in]  context_name  指定context
     * @param[in]  stream_id     指定流
     * 
     * @param[out] count         消息数量
     * 
     * @return     成功时返回kSuccess, 文件不存在或读取失败kFailure
     */ 
    ErrorCode_def GetRxStreamMessageCount(const std::string& context_name, const uint64_t stream_id, Message::SqnType& count);
    
    /**
     * @brief      读取Rx方向某个transport的持久化消息的数量
     *
     * @param[in]  context_name  指定context
     * @param[in]  transport_id  指定transport
     * 
     * @param[out] count         消息数量
     * 
     * @return     成功时返回kSuccess, 文件不存在或读取失败kFailure
     */ 
    ErrorCode_def GetRxTransportMessageCount(const std::string& context_name, const uint64_t transport_id, Message::SqnType& count);
    
    /**
     * @brief      读取Tx方向某个transport的持久化消息的数量
     *
     * @param[in]  context_name   指定context
     * @param[in]  transport_name 指定transport
     * 
     * @param[out] count         消息数量
     * 
     * @return     成功时返回kSuccess, 文件不存在或读取失败kFailure
     */ 
    ErrorCode_def GetTxMessageCount(const std::string& context_name, const std::string& transport_name, Message::SqnType& count);
    
    /**
     * @brief      读取Tx方向某个流的持久化消息的数量
     *
     * @param[in]  context_name    指定context
     * @param[in]  transport_name  指定transport
     * @param[in]  stream_id       指定流
     * 
     * @param[out] count         消息数量
     * 
     * @return     成功时返回kSuccess, 文件不存在或读取失败kFailure
     */ 
    ErrorCode_def GetTxMessageCount(const std::string& context_name, 
                                    const std::string& transport_name, 
                                    const uint64_t stream_id, 
                                    Message::SqnType& count);
    
    /**
     * @brief      停止读取持久化消息。执行完此函数不意味着立即停止读取消息，如果工作线程正在读取消息，则等消息读完后停止
     *
     * @param[in]  async_task_id  指定需要停止的订阅任务
     * 
     * @return     成功时返回kSuccess, 不存在此任务时返回kFailure
     */                              
    ErrorCode_def StopAsyncReadMessage(const int32_t async_task_id);
    
    /**
     * @brief      等待所有异步读取任务完成
     * 
     * @param[in]  wait_timeout_milli  等待订阅任务的超时时间，默认为0，即不停止等待订阅型任务；否则最多等待wait_timeout_milli毫秒时间函数返回
     * 
     * @return     成功时返回kSuccess, 不存在此任务时返回kFailure
     */                              
    ErrorCode_def StopAllAsyncReadMessageComplete(uint32_t wait_timeout_milli = 0);                 
                    
private:  
    
    enum class record_file_t: uint8_t
    {
        rx_index,
        rx_endpoint_index,
        rx_stream_index,  
        rx_transport_index,
        rx_msg_data,
        tx_transport_stream_index,
        tx_transport_index,
        tx_msg_data,
    };
    
    struct Task
    {
        Task(const int32_t id,
             const std::string& context_name,
             const std::string& index_path,
             const std::string& msg_data_path,
             const int index_fd,
             const int msg_data_fd,
             const OnMessage on_message,
             const Message::SqnType begin_sqn,
             const Message::SqnType end_sqn,
             int64_t record_file_head_len,
             const bool record_file_opt)
            : id_(id), 
            context_name_(context_name),
            index_path_(index_path),
            msg_data_path_(msg_data_path),
            index_fd_(index_fd),
            msg_data_fd_(msg_data_fd),
            on_message_(on_message),
            begin_sqn_(begin_sqn),
            end_sqn_(end_sqn),
            record_file_head_len_(record_file_head_len),
            record_file_opt_(record_file_opt),
            ami_message_(reinterpret_cast<AmiMessage*>(ami_msg_buf_))
        {

        }
                
        ~Task() = default;
            
        int32_t id_ = 0;
        std::string context_name_;
        std::string index_path_;
        std::string msg_data_path_;
        int index_fd_;
        int msg_data_fd_;
        OnMessage on_message_ = nullptr;
        Message::SqnType begin_sqn_;
        Message::SqnType end_sqn_;
        int64_t record_file_head_len_ = 0;
        bool record_file_opt_ = false;
        char ami_index_buf_[kAmiIndexSize];
        MsgCRCType crc_ = 0;
        char ami_msg_buf_[AMI_MAX_MESSAGE_SIZE_INTERNAL] = {0};
        AmiMessage* ami_message_ = nullptr;
        std::atomic<uint64_t> last_read_time_ = {0};
        std::atomic<bool> is_running_ = {true};
        std::atomic<bool> is_rx_ = {true};
    };
    
    struct tracker_mutex : boost::mutex {};
    struct task_mutex : boost::mutex {};
    
    int32_t AsyncReadRecordMessage(const std::string& context_name, 
                              const record_file_t& record_file_type, 
                              const OnMessage on_message,
                              const uint64_t id = 0, 
                              const Message::SqnType &begin = kBegin,
                              const Message::SqnType &end = kMostRecent,
                              const std::string& transport_name = std::string(),
                              const Property& props = Property()
                              );
                                    
    int32_t GetAddAsyncReadCount();
    
    std::string GetOpenRecordFilePath(const std::string& context_name,
                                      const record_file_t& record_file_type, 
                                      const uint64_t id,
                                      const std::string& transport_name = std::string()); 
                                       
    
    bool FileIsExisting(const std::string& file_path_name);
    
    int GetRecordFileDescriptor(const std::string& file_path_name);
    
    ErrorCode_def ReadOneMsgIndex(const int fd, const Message::SqnType& sqn, const int64_t header_len, char* buf, int64_t& cur_msg_pos);
    
    ErrorCode_def ReadEndPointId(const int fd, 
                                 const bool file_opt,
                                 MsgCRCType& crc,
                                 int64_t& offset, 
                                 AmiMessage& ami_message);
    
    ErrorCode_def ReadTransportId(const int fd, 
                                  const bool file_opt,
                                  MsgCRCType& crc, 
                                  int64_t& offset,
                                  AmiMessage& ami_message);

    ErrorCode_def ReadOneAppMessageData(const int fd, 
                                        const bool file_opt,
                                        MsgCRCType& crc, 
                                        int64_t& offset,
                                        AmiMessage& ami_message);
    
    ErrorCode_def ReadOneAppMessage(const int fd, 
                                    const int64_t msg_pos,
                                    const bool is_rx, 
                                    const bool file_opt,
                                    MsgCRCType& crc,
                                    AmiMessage& ami_message, 
                                    const RMPropType& msg_prop);
                          
    ErrorCode_def GetLastMsgSqn(const int fd, Message::SqnType& last_msg_sqn);
    
    ErrorCode_def OnWatch(const std::string& path_name, 
                          const adk_impl::inotify_event_t& event, 
                          const std::string& watch_name,
                          const int32_t async_task_id);
    
    ErrorCode_def GetMsgDataFileOpts(const std::string& path_name, int64_t& record_file_head_len, bool& record_file_opt);
    
    ErrorCode_def ReadHisMessage(const std::shared_ptr<Task>& task,
                                 Message::SqnType& cur_sqn,
                                 const RMPropType& msg_prop = RMPropType());
                                 
    ErrorCode_def GetMessageCount(const std::string& context_name, 
                                  const record_file_t& record_file_type,
                                  Message::SqnType& count, 
                                  const uint64_t id = 0, 
                                  const std::string& transport_name = std::string());
    
    bool CheckCRC(const int fd, const int64_t cur_offset, MsgCRCType& crc);
    
    std::shared_ptr<RecordDataTracker::Task> GetTask();
    
    void RemoveTask(const int32_t async_task_id);
    
    void Run();

    // 将未开始的任务移动到调度队列
    void MovePreTask();

private:
    std::atomic<bool> is_running_ = {false};
    std::atomic<int32_t> async_read_count_ = {0}; 
    
    std::string context_name_;
    std::string recorder_data_root_path_;
    
    uint64_t watch_timeout_sec_ = 0;
    uint32_t read_thread_num_ = 0;
    
    volatile bool is_check_crc_ = true;
    
    boost::mutex task_mutex_;
    boost::mutex tracker_mutex_;
    
    adk_impl::Inotify inotify_;  
    
    std::vector<boost::thread> work_threads_;
    
    OnError on_error_ = nullptr;
    OnComplete on_complete_ = nullptr;

    std::map<int32_t, std::shared_ptr<Task>> async_tasks_map_;
    std::list<int32_t> wait_async_task_ids_;
    std::unordered_set<int32_t> watch_async_task_ids_;
    
    // <record_file_path_name, fd>
    std::unordered_map<std::string, int> record_file_path_name_fd_map_;

    boost::mutex pre_mutex_;
    std::map<int32_t, std::shared_ptr<Task>> pre_async_tasks_map_;
    std::list<int32_t> pre_wait_async_task_ids_;

    LOG_DECLARE  
};

}

#endif  //AMI_RECORD_DATA_TRACKER_H_