/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_RECORDER_H_
#define AMI_RECORDER_H_

///< posix
#include <signal.h>  //sigaction
#include <stdint.h>
#include <sys/types.h>  //pid_t

///< cpp std
#include <algorithm>
#include <map>
#include <string>
#include <mutex>

///< BOOST
#include <boost/locale/format.hpp>  //format
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/optional.hpp>
#include <boost/property_tree/ptree.hpp>

///< adk, ami public header
#include <adk/lock_free_msg_queue.h>
#include <adk/mem_pool.h>
#include <adk/shm_ptr.h>
#include <ami/config_key.h>
#include <ami/error_code.h>
#include <ami/property.h>

///< ami impl
#include "../log.h"
#include "../util.h"

///< impl
#include "control_server.h"
#include "record_file_header.h"
#include "recorded_message_index.h"
#include "recorder_base.h"
#include "recorder_fwd.h"
#include <adk/singleton_process.h>

namespace ami
{

class EventHandler;
class RecordAgent;

/**
 * 将ami放在共享内存中的消息持久化到文件，并建立索引
 *
 * recorder以进程单例的形式运行。每个recorder进程可以服务多个context。
 *
 */
class Recorder
{
public:
    static const uint32_t kMaxPathLen            = 256;
    static const uint32_t kLaunchRetries         = 10;     // 0.5s * kLaunchRetries
    static constexpr const char* kTrackInfoQName = "track_info_q";
    const std::string kShmPath                   = "/dev/shm";

    /**
     * 启动recorder，recorder在fork出的一个子进程中运行
     *
     * @param domain_server domain server的名字，格式参考
     * config::context::kDomainServer的说明
     * @param recorder_name recorder实例名，启动时使用domain server中该实例名对应的配置
     * @param recorder_id 启动成功的recorder的id
     * @param recovery_mode true - 故障恢复模式启动；false - 正常模式启动
     *
     * @return kSuccess - 启动成功（如果是故障恢复模式，即表示恢复成功
     * 且可以开始一个新的会话）；kFailure - 启动失败
     */
    //[recorder_config_detail
    /*`
      [heading recorder的配置]
      domain server中每个context都可以配置自己的recorder配置（但是一个
      应用实例，真正启动的recorder只有一个，该recorder使用该应用中某一
      个context的recorder配置，至于是该应用中的哪个context的recorder配
      置生效则不确定）。实际配置时，在指定context配置中增加子配置树
      "Recorder"即可。或者启动recorder时在Property中设置配置项。可用的
      配置项如下：
       
      * config::context::recorder::kTxThreadNum - 处理输出消息持久化
      的线程数，默认'1'，如果小于'1'则设为默认值
         
      * config::context::recorder::kDataPath - 保存数据文件的根目录，
      默认'./recorder_data'
       
      * config::context::recorder::kShmToUse - recorder使用的共享内存
      大小（会使用该配置预先分配，因此请根据资源的情况配置）。默认
      512M，最小128M，最大64G。如果配置1G，请直接设置该配置项的值为
      "1G"或者"1g"；
       
      * config::context::recorder::kReportStatus - 上报状态。默认为true
        
      * config::context::recorder::kSnapshotCycleMilli - 上报状态的频率。默认为60 * 1000
       
      * config::context::recorder::kCpuAffinity - 假设想让recorder只跑
        在cpu = 1,2,3,5,9上，则配置值为"1-3,5,9"
        
      * config::context::recorder::kMsgQueueSize - 通道消息队列的长度，
      默认为1024，最小为1
      
      * config::context::recorder::kUseMsgCRC - 每个消息都生成一个crc
      校验码，保证每个消息的完整性，默认为false
       
      * config::context::recorder::kIgnoreLostMsg，默认为false
          * 配置为true且正常运行场景时，不要求消息号的连续性，即容忍丢消息
          * 配置为true且故障恢复场景时，当发现不完整消息时，认为丢消息可以容忍，继续恢复流程
    */
    //]
    //[recorder_config_detail_cascade
    /*`
      [heading channel, agent, recorder三者共同拥有的配置项的cascade语义]
      三者共同拥有的配置项如[*MsgQueueSize]，channel的配置覆盖agent的配置，
      agent的配置覆盖recorder的配置。如果三者都没有配置该配置项，则该配置项
      使用默认值。
    */
    //]
    static ErrorCode_def Launch(const std::string domain_server,
                                const std::string recorder_name,
                                RecorderId& recorder_id,
                                bool recovery_mode,
                                RecordAgent* record_agent);

    static std::string MakeRecorderTempDirectory(const std::string& recorder_data_path,
                                                 const std::string& recorder_name)
    {
        return recorder_data_path + "/temp/" + recorder_name + "/";
    }

    static std::string MakeRecorderPropertyFile(const std::string& recorder_data_path,
                                                const std::string& recorder_name,
                                                pid_t recorder_pid)
    {
        return MakeRecorderTempDirectory(recorder_data_path, recorder_name)
            + "property_"
            + boost::lexical_cast<std::string>(recorder_pid);
    }

    static bool PrepareExecRecorderBinary(const std::string& recorder_name,
                                          const Property& recorder_props);

    static void ExecRecorderBinary(bool recovery_mode,
                                   const std::string& recorder_name,
                                   const Property& recorder_props);

    static ErrorCode_def RecorderMain(pid_t recorder_pid,
                                      bool recovery_mode,
                                      const std::string& recorder_name,
                                      const std::string& recorder_data_path,
                                      Property& recorder_props);

    /**
     * 启动recorder，recorder在fork出的一个子进程中运行
     *
     * @param recorder_props recorder的配置
     * @param recorder_name recorder实例名
     * @param recorder_id 启动成功的recorder的id
     * @param recovery_mode true - 故障恢复模式启动；false - 正常模式启动
     */
    static ErrorCode_def Launch(const Property& recorder_props,
                                const std::string recorder_name,
                                RecorderId& recorder_id,
                                bool recovery_mode,
                                EventHandler* on_launch_event,
                                RecordAgent* record_agent);

    static void StopLaunch();

    static ErrorCode_def Stop(bool imm);

    static const RecorderId& GetId()
    {
        return Instance().id_;
    }

    static bool MayLostMsg()
    {
        return Instance().may_lost_msg_;
    }

    static Property GetCascadeConfig()
    {
        return Instance().CascadeConfig();
    }

    static adk::MPManager& GetMPManager()
    {
        return Instance().mp_manager_;
    }

    static adk::MemoryPool* GetMsgDataBufferPool()
    {
        return (Instance().msgdata_buffer_pool_);
    }

    static adk::MQManager* GetMQManager()
    {
        return (Instance().mq_manager_);
    }

    static adk::MPSCQueue* GetTrackInfoQ()
    {
        return (Instance().track_info_q_);
    }

    static bool IsTrackInfoSaved(uint64_t value)
    {
        return Instance().saved_track_info_.count(value);
    }

    static SerialWorker* GetRecordWorker(size_t idx)
    {
        return (Instance().record_workers_[idx]);
    }

    static std::string GetShmName(const std::string& shm_name)
    {
        return (boost::locale::format("{1}_recorder-{2}_{3}")
                % GetLoginUserName()
                % Instance().GetId()
                % shm_name)
            .str();
    }

    std::string GetMQManagerName()
    {
        std::string recorder_name = Instance().GetId().GetInsName();
        return MakeRCMQShmName(recorder_name);
    }

    std::string GetMPTableName()
    {
        std::string recorder_name = Instance().GetId().GetInsName();
        return MakeRCMPTablesShmName(recorder_name);
    }

    std::string GetMsgDataBufferPoolName()
    {
        std::string recorder_name = Instance().GetId().GetInsName();
        return MakeRCMsgDataBufferMPShmName(recorder_name);
    }

    static void SignalParent(char s);

    static bool recorder_fork_end() { return s_recorder_fork_end; }

    static void set_recorder_fork_end() { s_recorder_fork_end = true; }

    static bool to_exit();

    static void DeleteRecorder(const std::string recorder_name, const RecorderId& recorder_id);

private:
    struct MsgTrack
    {
        struct TrackPathI
        {
        };  //track path index
        struct CtxNameI
        {
        };  //context name index

        std::string track_path;
        std::string ctx_name;
        MessageTrack* track;
        bool ctx_backuped;

        MsgTrack(const std::string& track_path_,
                 MessageTrack* track_)
            : track_path(track_path_),
              ctx_name(CtxName(track_path)),
              track(track_)
        {
        }
    };

    typedef boost::multi_index::multi_index_container<
        MsgTrack,
        boost::multi_index::indexed_by<
            //track path index
            boost::multi_index::ordered_unique<boost::multi_index::tag<MsgTrack::TrackPathI>,
                                               BOOST_MULTI_INDEX_MEMBER(MsgTrack, std::string, track_path)>,

            //context name index
            boost::multi_index::ordered_non_unique<boost::multi_index::tag<MsgTrack::CtxNameI>,
                                                   BOOST_MULTI_INDEX_MEMBER(MsgTrack, std::string, ctx_name)>>  //all indexes defined
        >
        MsgTrackSetType;
    typedef MsgTrackSetType::index<MsgTrack::TrackPathI>::type TrackIType;
    typedef MsgTrackSetType::index<MsgTrack::CtxNameI>::type CtxIType;

    typedef std::vector<SerialWorker*> WorkerVecType;
    typedef std::map<std::string, MessageTrack*> TrackMapType;

    static constexpr const char* kMsgDataBufferPoolName = "msgdata_buffer_mp";
    static constexpr const char* kIndexBufferPoolName   = "index_buffer_mp";
    static constexpr size_t kDataFileSizeLimit =
        64u * 1024u * 1024u;  ///< 消息文件的上限为64M

    static Recorder* single_instance_;
    static int pipefd_[2];  //ipc after fork
    static bool recovery_mode_;
    static bool to_signal_parent_;
    static bool is_launched_;
    static bool s_is_sig_stop;

    Recorder();
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    static ByteSize ShmUsedByMPUpLimit();

    /**
     * 单子模式，获得recorder的唯一实例
     */
    static Recorder& Instance()
    {
        if (single_instance_)
        {
            return *single_instance_;
        }
        else
        {
            return *(single_instance_ = new Recorder());
        }
    }

    /**
     * 注册到OS的信号处理句柄
     */
    static void SignalHandler(int sig, siginfo_t* info, void* secret);

    ErrorCode_def Init(const Property& recorder_props, bool recovery = false);
    ErrorCode_def Start();
    ErrorCode_def Recover(const Property& recorder_props);
    void Idle();  ///< 等待信号量，等到SIGINT、SIGQUIT、SIGTERM退出。其他信号忽略
    void Exit(int);

    /**
     * @return true - 已经处于就绪状态；false - 还没有就绪
     */
    bool IsReady() const
    {
        return (true == ready_to_go_);
    }

    bool CanIgnoreMsgLost() const
    {
        return ((ignore_lost_msg_) && (*ignore_lost_msg_));
    }

    static void RegisterSignal();

    ErrorCode_def
    CreateMergeChannels(const Property& request, Property& reply);

    ErrorCode_def
    CreateMessageChannel(const Property& request, Property& reply);

    ErrorCode_def
    CreateStatusChannel(const Property& request, Property& reply);

    int32_t RealWorkerNum() const
    {
        return worker_num_;
    }

    int32_t DispatchWorker()
    {
        return dispatch_cnt_++ % (worker_num_ == 1 ? worker_num_ : worker_num_ - 1);
    }

    /**
     * 处理client发送来的request
     *
     * @param request_buf
     * @param request_len
     * @param reply_buf 响应消息的缓冲区间
     * @param reply_len 响应消息缓冲区间的长度
     * 作为入参表示rep_buf的可用空间长度，作为出参表示实际收到的包长
     *
     * @par 调用方式
     * 同步方式发送请求，获取响应后返回
     *
     * @par 线程安全
     * 安全
     */
    bool OnRequest(const void* request_buf,
                   uint32_t request_len,
                   void* reply_buf,
                   uint32_t* reply_len);

    ErrorCode_def CreateMQManager();
    ErrorCode_def AttachOrCreateMQManager();
    ErrorCode_def CreateMPs();
    ErrorCode_def AttachOrCreateMPs();

    ErrorCode_def TryDestroyMQManager()
    {
        ErrorCode_def ec =
            adk::MQManager::Destroy(GetMQManagerName());
        mq_manager_   = nullptr;
        track_info_q_ = nullptr;

        return ec;
    }

    ErrorCode_def TryDestroyMPs()
    {
        ErrorCode_def ec     = mp_manager_.DestroyAll();
        msgdata_buffer_pool_ = nullptr;
        return ec;
    }

    void TearDown();
    void OnClientNE();

    bool TrackExist(const std::string& track_path) const
    {
        const auto& ti = track_set_.get<MsgTrack::TrackPathI>();
        return (ti.count(track_path) > 0);
    }

    size_t TrackNum() const
    {
        return track_set_.size();
    }

    MessageTrack* GetTrack(const std::string& track_path) const
    {
        if (TrackExist(track_path))
        {
            const auto& ti = track_set_.get<MsgTrack::TrackPathI>();
            return ti.find(track_path)->track;
        }
        else
        {
            return nullptr;
        }
    }

    void AddTrack(const std::string& track_path, MessageTrack* track)
    {
        if (TrackExist(track_path))
            return;
        track_set_.emplace(track_path, track);
    }

    void StopTracks();
    void ClearTracksMQ();
    void DestroyTracks();

    /**
     * @return kSuccess - 恢复成功；kFailure - 恢复失败；kTryAgain - 正在恢复
     */
    ErrorCode_def CheckTracksRecovered() const;

    void NotifyTracksToQuit();

    /**
     * @return kSuccess - 可以退出；kFailure - 可以退出但是有错误；kTryAgain - 正在退出
     */
    ErrorCode_def CheckTracksQuitable() const;

    bool HasSameContextTrackExist(const string& track_path) const
    {
        const auto& cni = track_set_.get<MsgTrack::CtxNameI>();
        return (cni.count(CtxName(track_path)) > 0);
    }

    std::string& GetBackupTime();

    boost::filesystem::path& CreateRecorderShmDir();
    boost::filesystem::path CreateContextShmDir(const std::string& ctx_name);

    void BackupRecorderShmData();
    void BackupContextShmDir(const std::string ctx_name);
    void BackupShareMemoryData(const std::string& ctx_name = "");

    void TryBackupContextFiles(const std::string& track_path);

    Property CascadeConfig() const
    {
        Property res;
        if (msg_queue_size_)
        {
            res.SetValue(config::context::recorder::kMsgQueueSize,
                         *msg_queue_size_);
        }

        if (use_msg_crc_)
        {
            res.SetValue(config::context::recorder::kUseMsgCRC, *use_msg_crc_);
        }

        if (ignore_lost_msg_)
        {
            res.SetValue(config::context::recorder::kIgnoreLostMsg, *ignore_lost_msg_);
        }

        return res;
    }

    void DumpToPtree(boost::property_tree::ptree& status_tree) const;
    bool OnCollectIndicator(boost::property_tree::ptree& indicator) const
    {
        DumpToPtree(indicator);
        return true;
    }

    /**
     * 供单元测试用，重置recorder的状态
     */
    void Reset()
    {
        may_lost_msg_        = false;
        exit_immediatly_     = true;
        to_exit_             = false;
        ready_to_go_         = false;
        track_info_q_        = nullptr;
        dispatch_cnt_        = 0;
        mq_manager_          = nullptr;
        msgdata_buffer_pool_ = nullptr;
        id_                  = RecorderId();
    }

    void HandleMPMExcept();
    
    ErrorCode_def IncreSyncIdMaps(const Property& request, Property& reply);

    ///< from recorder config
    RecorderId id_;
    int32_t worker_num_ = 0;
    WorkerVecType record_workers_;
    std::string data_path_;
    ByteSize shm_to_use_;
    bool report_status_ = true;
    boost::optional<size_t> snapshot_cycle_milli_;
    std::string cpu_affinity_;

    /*******************************************
     * cascade语义的配置项
     */
    boost::optional<size_t> msg_queue_size_;
    boost::optional<bool> use_msg_crc_;
    boost::optional<bool> ignore_lost_msg_;
    Property config_to_cascade_;
    /********************************************/

    ControlServer control_server_;
    adk::MPManager mp_manager_;
    adk::MemoryPool* msgdata_buffer_pool_ = nullptr;
    adk::MQManager* mq_manager_           = nullptr;
    int32_t dispatch_cnt_                 = 0;
    MsgTrackSetType track_set_;
    adk::MPSCQueue* track_info_q_ = nullptr;
    std::set<uint64_t> saved_track_info_;

    ///< recorder status
    bool ready_to_go_     = false;
    bool to_exit_         = false;
    bool exit_immediatly_ = true;  ///< true：立即退出，不等待所有消息落地
    bool may_lost_msg_    = false;

    ///< backup status
    bool backup_recorder_shm_data_done_ = false;
    boost::mutex backup_time_mutex_;
    boost::mutex backup_mutex_;
    std::string backup_time_;
    std::set<std::string> backup_shm_data_done_set_;

    static int keepalive_fds_[2];
    int internal_keepalive_fds_[2] = {-1, -1};

    static adk::SingletonProcess* s_singleton_process_;
    static bool s_recorder_fork_end;
    static int s_disk_retry_interval_milli;

    static std::mutex recorder_map_mtx_;
    static std::map<std::string, Recorder*> recorder_map_;

    static int32_t WaitRunningRecorderExit(const std::string& lock_path,
                                           const std::string& recorder_name);

    friend std::ostream& operator<<(std::ostream&, const Recorder&);
    friend void MPMExceptHandler(void*);

    LOG_DECLARE

    void GenIdMaps(const Property& request);
};

std::ostream& operator<<(std::ostream&, const Recorder&);

namespace recorder
{
// note: use this api when recorder ws launched
std::string GetRecorderDataPath(Property& props);
}

}  // namespace ami
namespace fmt
{
template <> 
struct formatter<ami::ByteSize> : formatter<string_view>
{
    template<typename FormatContext>
    auto format(const ami::ByteSize& x, FormatContext& ctx) -> decltype(this->formatter<string_view>::format(string_view{}, ctx))
    {
        std::ostringstream os;
        os << x;
        return formatter<string_view>::format(string_view(os.str()), ctx);
    }
};
template <> 
struct formatter<ami::Recorder> : formatter<string_view>
{
    template<typename FormatContext>
    auto format(const ami::Recorder& x, FormatContext& ctx) -> decltype(this->formatter<string_view>::format(string_view{}, ctx))
    {
        std::ostringstream os;
        os << x;
        return formatter<string_view>::format(string_view(os.str()), ctx);
    }
};
}
#endif /* AMI_RECORDER_H_ */
