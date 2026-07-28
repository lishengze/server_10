/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */

///< posix
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>  //pipe, read, write, getpid
#include <sys/resource.h>

///< cpp std
#include <cassert>
#include <ctime>
#include <map>

///< boost
#include <boost/algorithm/string.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/regex.hpp>

///< adk, ami public
#include <adk/monitor/monitor.h>
#include <adk/util.h>

///< ami impl
#include "../config_agent.h"
#include "../util.h"

///< impl header
#include "../ami_env.h"
#include "../event_impl.h"
#include "../util.h"
#include "../ami_common.h"
#include "async_record_client.h"
#include "config_default_value.h"
#include "hook.h"
#include "recorded_message_index.h"
#include "recorder.h"
#include "recorder_base.h"
#include "rx_message_track.h"
#include "st_message_track.h"
#include "tx_message_track.h"
#include "record_client.h"
#include <ami/handler.h>
#include <fstream>
#include "../property_container.h"

namespace
{
std::map<int, std::string> g_ignored_sig_map = boost::assign::map_list_of(SIGHUP, "SIGHUP")(SIGINT, "SIGINT")(SIGQUIT, "SIGQUIT")(SIGTERM, "SIGTERM")(SIGUSR1, "SIGUSR1")(SIGPIPE, "SIGPIPE");

}

namespace ami
{

namespace bl   = boost::locale;
namespace bs   = boost::system;
namespace bf   = boost::filesystem;
namespace bmi  = boost::multi_index;
namespace bt   = boost::property_tree;
namespace cc   = config::context;
namespace ccr  = config::context::recorder;
namespace rcdv = recorder::cdv;

LOG_DEFINE(ami::Recorder)

const uint32_t Recorder::kMaxPathLen;
constexpr const char* Recorder::kMsgDataBufferPoolName;
constexpr const char* Recorder::kIndexBufferPoolName;
constexpr const char* Recorder::kTrackInfoQName;
Recorder* Recorder::single_instance_ = nullptr;
int Recorder::pipefd_[2];
bool Recorder::recovery_mode_      = false;
bool Recorder::to_signal_parent_   = false;
bool Recorder::is_launched_        = false;
bool Recorder::s_is_sig_stop       = false;
bool Recorder::s_recorder_fork_end = false;
std::mutex Recorder::recorder_map_mtx_;
std::map<std::string, Recorder*> Recorder::recorder_map_;

int Recorder::keepalive_fds_[2] = {-1, -1};

#ifdef __AMI_TEST_FRAMEWORK__
extern bool g_simulate_crc_error;
extern uint32_t g_simulate_crc_error_count;
#endif

extern void ClearRecordLogs();
extern void InitMutexAfterFork();
static void RecorderExit(int ret)
{
    if (recorder::Hook::Instance() != nullptr)
        recorder::Hook::Instance()->OnRecorderExit();
    _exit(ret);
}

ErrorCode_def Recorder::Stop(bool imm)
{
    if (!is_launched_
        || s_is_sig_stop)
    {
        return ErrorCode::kSuccess;
    }

    auto rec_pid = Instance().GetId().GetPid();
    LOG_INFO("signal recorder {1} to stop", rec_pid);

    union sigval sval;
    sval.sival_int = (imm ? kRecorderExitImmValue : kRecorderExitValue);
    if (sigqueue(rec_pid, SIGUSR1, sval) != 0)
    {
        LOG_ERROR("stop recorder {1} failed, {2}", rec_pid, errno);
        return ErrorCode::kFailure;
    }

    s_is_sig_stop = true;
    return ErrorCode::kSuccess;
}

void MPMExceptHandler(void* data)
{
    if (data != NULL)
    {
        Recorder* instance = reinterpret_cast<Recorder*>(data);
        instance->HandleMPMExcept();
        _exit(1);
    }
    _exit(1);
}

void Recorder::HandleMPMExcept()
{
    LOG_ERROR("catch memory pool manager exception, "
              "the exception was raised by attach share memory failure");
}

static std::ofstream* g_rec_temp_log_file = nullptr;
#define INIT_REC_TEMP_LOGGER(file_path)                                    \
    do                                                                     \
    {                                                                      \
        g_rec_temp_log_file = new std::ofstream(file_path, std::ios::app); \
        if (!(g_rec_temp_log_file->good()))                                \
        {                                                                  \
            delete g_rec_temp_log_file;                                    \
            g_rec_temp_log_file = nullptr;                                 \
        }                                                                  \
    } while (false)

#define REC_TEMP_LOG(msg)                               \
    do                                                  \
    {                                                   \
        if (g_rec_temp_log_file != nullptr)             \
        {                                               \
            (*g_rec_temp_log_file) << msg << std::endl; \
            g_rec_temp_log_file->flush();               \
        }                                               \
    } while (false)

static std::string& GetRecorderBinaryPath(const Property& recorder_props)
{
    static std::string recorder_path;
    static std::string empty_str;
    static std::string rec_bin_path = recorder_props.GetValue(ccr::kRecorderBinaryPath,
                                                              rcdv::kRecorderBinaryPath);
    if (rec_bin_path.empty())
    {
        if (!env_recorder_binary_path.empty())
        {
            return env_recorder_binary_path;
        }
        else
        {
            if (recorder_path.empty())
            {
                AmiCommon::GetRecorderExecutablePath(recorder_path);
            }
            rec_bin_path = recorder_path;
        }
    }
    return rec_bin_path;
}

int Recorder::s_disk_retry_interval_milli = rcdv::kDiskRetryIntervalMilli;
bool Recorder::to_exit()
{
    if (Instance().to_exit_)
        return true;

    usleep(s_disk_retry_interval_milli * 1000);
    return false;
}

static std::string s_g_recorder_app_name;
static std::string s_g_recorder_data_path;
static std::string s_g_recorder_log_dir;
static std::string s_g_recorder_binary_path;
static std::string s_g_recorder_binary_path_leaf;
static std::string s_g_recorder_pipe_fd;
static std::string s_g_recorder_keepalive_fd;
static std::string s_g_recorder_property_file_path;

namespace recorder
{
// note: use this api when recorder ws launched
std::string GetRecorderDataPath(Property& props)
{
    return s_g_recorder_data_path;
}
}

bool Recorder::PrepareExecRecorderBinary(const std::string& recorder_name,
                                         const Property& recorder_props)
{
    s_g_recorder_data_path = recorder_props.GetValue(ccr::kDataPath, rcdv::kDataPath);

    bf::path recorder_temp_directory = MakeRecorderTempDirectory(s_g_recorder_data_path,
                                                                 recorder_name);
    bs::error_code bs_ec;
    bf::create_directories(recorder_temp_directory, bs_ec);

    // INIT_REC_TEMP_LOGGER(recorder_temp_directory.string() + "/recorder.log");
    // REC_TEMP_LOG((boost::locale::format("launch the recorder at {1}")
    //              % boost::posix_time::ptime(boost::posix_time::microsec_clock::local_time())).str());

    s_g_recorder_app_name = recorder_name + "_recorder";
    s_g_recorder_pipe_fd = std::to_string(pipefd_[1]);
    s_g_recorder_keepalive_fd = std::to_string(keepalive_fds_[0]);

    bf::path recorder_binary_path = GetRecorderBinaryPath(recorder_props);
    LOG_INFO("get recorder binary path:{1}",
              recorder_binary_path.string());

    s_g_recorder_binary_path = recorder_binary_path.string();
    s_g_recorder_binary_path_leaf = recorder_binary_path.leaf().string();

    // write the recorder property to file
    srand(time(nullptr));
    int rand_num = rand();
    s_g_recorder_property_file_path = MakeRecorderPropertyFile(s_g_recorder_data_path,
                                                               recorder_name, rand_num);
    std::ofstream property_file(s_g_recorder_property_file_path, std::ios::trunc);
    if (!property_file.good())
    {
        LOG_ERROR("open property file <{1}> failed",  s_g_recorder_property_file_path);
        return false;
    }

    property_file << recorder_props.Dump() << std::endl;
    property_file.close();

    s_g_recorder_log_dir = recorder_props.GetValue(config::context::kDefaultLogDir, std::string());
    if (s_g_recorder_log_dir.empty())
    {
        struct passwd* pw = getpwuid(geteuid());
        if (pw == NULL)
        {
            LOG_ERROR("get login user name failed errno: <{1}>, desc: <{2}>", errno, strerror(errno));
            return false;
        }

        s_g_recorder_log_dir = std::string(pw->pw_dir) + "/log/";
    }

    return true;
}

#ifdef __AMI_TEST_FRAMEWORK__
extern "C" char ** environ;
#endif

void Recorder::ExecRecorderBinary(bool recovery_mode,
                                  const std::string& recorder_name,
                                  const Property& recorder_props)
{
    #ifndef __AMI_TEST_FRAMEWORK__
    execl(s_g_recorder_binary_path.c_str(),
          s_g_recorder_binary_path_leaf.c_str(),
          "--recorder-data-path", s_g_recorder_data_path.c_str(),
          "--name", s_g_recorder_app_name.c_str(),
          "--recorder-name", recorder_name.c_str(),
          "--recovery-mode", (recovery_mode ? "1" : "0"),
          "--log-dir", s_g_recorder_log_dir.c_str(),
          "--pipe-fd", s_g_recorder_pipe_fd.c_str(),
          "--keepalive-fd", s_g_recorder_keepalive_fd.c_str(),
          "--property-file", s_g_recorder_property_file_path.c_str(),
          NULL);
    #else

    execle(s_g_recorder_binary_path.c_str(),
           s_g_recorder_binary_path_leaf.c_str(),
           "--recorder-data-path", s_g_recorder_data_path.c_str(),
           "--name", s_g_recorder_app_name.c_str(),
           "--recorder-name", recorder_name.c_str(),
           "--recovery-mode", (recovery_mode ? "1" : "0"),
           "--log-dir", s_g_recorder_log_dir.c_str(),
           "--pipe-fd", s_g_recorder_pipe_fd.c_str(),
           "--keepalive-fd", s_g_recorder_keepalive_fd.c_str(),
           "--property-file", s_g_recorder_property_file_path.c_str(),
           NULL,
           environ);
    #endif

    // if return here, execl failed
    SignalParent('e'); // send 'e' to indicate that execl failed
    RecorderExit(1);
}

int32_t Recorder::RecorderMain(pid_t recorder_pid,
                               bool recovery_mode,
                               const std::string& recorder_name,
                               const std::string& recorder_data_path,
                               Property& recorder_props)
{
    static int32_t ti = recorder_props.GetValue("RecorderSuspendAfterExec", 0);
    while (ti > 0)
    {
        --ti;
        sleep(1);
    }

    LOG_INFO("file header len <{1}>, index record len <{2}>",
             sizeof(RecordFileHdr), OrdinalIndex::ValueSize());

    prctl(PR_SET_NAME, "recorder", 0, 0, 0);
    RegisterSignal();
    SerialWorker::set_delay_us(recorder_props.GetValue(ccr::kWorkerDelayMicro,
                                                       rcdv::kWorkerDelayMicro));
    s_disk_retry_interval_milli = recorder_props.GetValue(ccr::kDiskRetryIntervalMilli,
                                                          rcdv::kDiskRetryIntervalMilli);

    recovery_mode_      = recovery_mode;
    to_signal_parent_   = true;
    is_launched_        = true;
    s_recorder_fork_end = true;
    InitMutexAfterFork();
    ClearRecordLogs();

    Recorder& recorder = Instance();
    adk::MPManager::set_except_handler(MPMExceptHandler, &recorder);
    auto rc_data_path = recorder_props.GetValue(ccr::kDataPath, rcdv::kDataPath); // get recorder data path
    recorder.id_ = RecorderId(recorder_pid, recorder_name, rc_data_path); // creat RecorderId
    pipefd_[1]   = rcdv::kRecorderPipeFD;
    keepalive_fds_[0] = rcdv::kRecorderKeepalivePipeFD;

    if (true == recovery_mode_)
    {
        // attach recorder memory buffer pool, which was used by FileWriteBuffer
        // attach recorder TrackInfo queue, which was use to save FileWriteBuffer
        // init control_server if there are message lost
        if (kSuccess != recorder.Init(recorder_props, true))
        {
            LOG_ERROR("init recorder(recovery mode) failed.");
            SignalParent('f');
            RecorderExit(1);
        }

        // recovery track
        // start tracks, to process message in share memory, to sync data file and index files
        // stop tracks
        // clean share memory queues
        if (!Instance().may_lost_msg_  //不丢消息场景的状态恢复
            && (kSuccess != recorder.Recover(recorder_props)))
        {
            SignalParent('f');
            RecorderExit(1);
        }

        if (Instance().may_lost_msg_)  //丢消息场景的状态恢复
        {
            if (!Instance().CanIgnoreMsgLost())
            {
                LOG_ERROR("CAN NOT be tolerant to msg lost");
                SignalParent('f');
                RecorderExit(1);
            }

            // system crash/reboot scenario
            // start control_server directly
            // recover from data file and all index files during CreateMessageChannel
            // check DoInit(4 parameter)
            if (kSuccess != recorder.Start())
            {
                SignalParent('f');
                RecorderExit(1);
            }
        }
    }
    else
    {
        // create recorder memory buffer pool, which was used by FileWriteBuffer
        // create recorder TrackInfo queue, which was use to save FileWriteBuffer
        // init control_server
        // backup recorder share memory data
        // FIXME: backup the context share memory data together.
        //        configure the context name in Recorder property
        if ((kSuccess != recorder.Init(recorder_props))
            || (kSuccess != recorder.Start()))
        {
            LOG_ERROR("init recorder failed.");
            SignalParent('f');
            RecorderExit(1);
        }
    }

    SignalParent('r');
    recorder.Idle();
    RecorderExit(0);
    return kSuccess;
}

static bool s_quit_during_launch_                     = false;
adk::SingletonProcess* Recorder::s_singleton_process_ = nullptr;

void Recorder::StopLaunch()
{
    s_quit_during_launch_ = true;
}

int32_t Recorder::WaitRunningRecorderExit(const std::string& lock_path,
                                          const std::string& recorder_name)
{
    uint32_t retry_counter = 0;
    s_singleton_process_ = new adk::SingletonProcess(lock_path);
    while (s_singleton_process_->Lock() != adk::ErrorCode::kSuccess)
    {
        delete s_singleton_process_;
        s_singleton_process_ = nullptr;
        if (++retry_counter > kLaunchRetries)
        {
            LOG_ERROR("wait previous recorder exit timeout, errno <{1}>", errno);
            return ErrorCode::kFailure;
        }

        LOG_WARN_RATELIMITED_VERY_LOW("waiting previous recorder <{1}> to quit", recorder_name);
        usleep(500000);  // 0.5 sec

        if (s_quit_during_launch_)
        {
            return ErrorCode::kFailure;
        }

        s_singleton_process_ = new adk::SingletonProcess(lock_path);
    }
    delete s_singleton_process_;
    s_singleton_process_ = nullptr;
    return ErrorCode::kSuccess;
}

ErrorCode_def Recorder::Launch(const Property& recorder_props,
                               const std::string recorder_name,
                               RecorderId& recorder_id,
                               bool recovery_mode,
                               ami::EventHandler* on_launch_event,
                               RecordAgent* record_agent)
{
    std::lock_guard<std::mutex> lck(recorder_map_mtx_);
    auto it = recorder_map_.find(recorder_name);
    if (it != recorder_map_.end())
    {
        recorder_id = it->second->id_;
        return ErrorCode::kSuccess;
    }

    auto rc_data_path = recorder_props.GetValue(ccr::kDataPath, rcdv::kDataPath); // get recorder data directory
    rc_data_path += "/../"; // get recorder data parent directory 

    auto parent_rc_data_path = rc_data_path;
    boost::system::error_code ec;
    parent_rc_data_path = boost::filesystem::weakly_canonical(rc_data_path, ec).string(); // weakly convert to absolute path
    if (parent_rc_data_path == "./." && !ec) // if recorder data directory don't exist, will get the "./."
    {
        // weakly convert to absolute path again
        parent_rc_data_path = boost::filesystem::weakly_canonical(parent_rc_data_path, ec).string();
    }
    if (ec) // if error use original recorder data parent directory
    {
        parent_rc_data_path = rc_data_path;
    }

    std::vector<std::string> dir_vec = {parent_rc_data_path, "/dev/shm"};
    auto dir_map = AmiCommon::GetDirectoryInfo(dir_vec); //get directory info
    for (auto &item : dir_map)
    {
        //calculate used block
        double used_block = item.second.block_cnt - item.second.block_free;
        if ((used_block / item.second.block_cnt) >= 0.80) // the directory mounted file system already used more than 80%
        {
            LOG_WARN("the directory:<{1}> used by recorder data exceeds 80%", item.first);
            LOG_WARN("for more information, please check hard disk usage by command 'df -h'");
        }
    }

    std::string lock_path = (boost::format("%1%/lock/%2%")
                             % recorder_props.GetValue(ccr::kDataPath, rcdv::kDataPath)
                             % recorder_name).str();

    if (WaitRunningRecorderExit(lock_path, recorder_name) != ErrorCode::kSuccess)
        return ErrorCode::kFailure;

    const std::string &recorder_path = GetRecorderBinaryPath(recorder_props);
    if (recorder_path.empty())
    {
        LOG_ERROR("get recorder binary path failed, please set AMI_RECORDER_BINARY_PATH environmental variable or set recorder binary path at config");
        return ErrorCode::kFailure;
    }
    else if (!boost::filesystem::exists(recorder_path))
    {
        LOG_ERROR("the recorder binary path <{1}> don't exist", recorder_path);
        return ErrorCode::kFailure;
    }
    
    to_signal_parent_ = true;

    if (!recovery_mode)
    {
        LOG_INFO("launching recorder with property({1})...",
                 recorder_props.Dump());
    }
    else
    {
        LOG_INFO("launching recorder(recovery mode) with property({1})...",
                 recorder_props.Dump());
    }

    s_disk_retry_interval_milli = recorder_props.GetValue(ccr::kDiskRetryIntervalMilli,
                                                          rcdv::kDiskRetryIntervalMilli);
    recovery_mode_              = recovery_mode;

    if (pipe(pipefd_) == -1)
    {
        LOG_ERROR("create pipe failed");
        return kFailure;
    }

    keepalive_fds_[0] = -1;
    keepalive_fds_[1] = -1;
    if (pipe2(keepalive_fds_, O_NONBLOCK) != 0)
    {
        LOG_ERROR("pipe2 failed");
        return kFailure;
    }

    std::vector<int> open_files = AmiCommon::ListOpenFiles(
                                // exclude following fds
                                { 0, 1, 2, pipefd_[1], keepalive_fds_[0] });
    if (open_files.empty())
    {
        LOG_ERROR("get open files failed");
        return kFailure;
    }

    if (!PrepareExecRecorderBinary(recorder_name, recorder_props))
    {
        LOG_ERROR("PrepareExecRecorderBinary failed");
        return ErrorCode::kFailure;
    }

    if (recovery_mode)
    {
        boost::filesystem::path data_path(recorder_props.GetValue(ccr::kDataPath, rcdv::kDataPath));       
        data_path /= recorder_name;
        if (!boost::filesystem::exists(data_path))
        {
            LOG_INFO("the recorder data <{1}> was not found, fallback to Bootstrap mode", 
                boost::filesystem::weakly_canonical(data_path).string());
            recovery_mode = false;
            recovery_mode_ = false;
        }
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        LOG_ERROR("fork failed, errno = <{1}>, desc <{2}>", errno, strerror(errno));
        return ErrorCode::kFailure;
    }

    if (pid > 0)
    {  //parent
        close(keepalive_fds_[0]);
        keepalive_fds_[0] = -1;

        s_recorder_fork_end = true;
        if (on_launch_event != nullptr)
        {
            RecorderLaunchedEvent recorder_launched_event;
            recorder_launched_event.property().SetValue(event::property::kIsParent, true);
            SaveEventTime(recorder_launched_event);
            LOG_INFO("context <{1}>, event <{2}>", recorder_name, recorder_launched_event.what());
            on_launch_event->OnEvent(&recorder_launched_event);
        }

        close(pipefd_[1]);
        char ready_sig;
        int result = -1;

        LOG_INFO("waiting for recorder <pid:{1}> ready...", pid);
        result = read(pipefd_[0], &ready_sig, 1);
        if (result < 0)
        {
            LOG_ERROR("some unexpected error happend "
                      "when waiting for recorder ready.");
            return kFailure;
        }
        else if (0 == result)
        {
            LOG_ERROR("recorder exit unexpectedly.");
            return kFailure;
        }

        close(pipefd_[0]);
        if ('r' == ready_sig)
        {
            LOG_INFO("recorder is ready.");
            auto recorder = new Recorder;
            recorder->internal_keepalive_fds_[0] = keepalive_fds_[0];
            recorder->internal_keepalive_fds_[1] = keepalive_fds_[1];

            recorder_map_[recorder_name] = recorder;
            auto rc_data_path = recorder_props.GetValue(ccr::kDataPath, rcdv::kDataPath); // get recorder data path
            recorder->id_ = RecorderId(pid, recorder_name, rc_data_path); // creat RecorderId
            recorder_id = recorder->id_;
            return kSuccess;
        }
        else if ('e' == ready_sig) // exe failed
        {
            LOG_INFO("start recorder failed, "
                     "the recorder exe path: <{1}> may be incorrect",
                     s_g_recorder_binary_path);
            
            return ErrorCode::kFailure;
        }
        else 
        {
            LOG_INFO("start recorder failed.");
            return kFailure;
        }
    }
    else if (pid == 0)
    {
        //close all open file except for std::in, std::out, std::err
        close(close(pipefd_[0]));
        close(keepalive_fds_[1]);
        keepalive_fds_[1] = -1;
        pipefd_[0] = -1;

        for (auto fd : open_files)
        {
            close(fd);
        }

        //Detach shared memory
        if (record_agent)
        {
            record_agent->DetachSharedMemory();
        }

        ExecRecorderBinary(recovery_mode, recorder_name, recorder_props);
        assert(false);
        return ErrorCode::kFailure;
    }
    else
    {  //fork error
        LOG_FATAL("Fork process failed. errno: {1}", errno);
        is_launched_ = false;
        return ErrorCode::kFailure;
    }
}

ErrorCode_def Recorder::Launch(const std::string domain_server,
                               const std::string recorder_name,
                               RecorderId& recorder_id,
                               bool recovery_mode,
                                RecordAgent* record_agent)
{
    // if (is_launched_)
    // {
    //     recorder_id = Instance().GetId();
    //     return ErrorCode::kSuccess;
    // }

    Property domain_server_prop, recorder_config;
    try
    {
        domain_server_prop.SetValue(cc::kDomainServer, domain_server);
        ami::ConfigAgent config_agent;
        if (false == config_agent.Init(domain_server_prop))
        {
            return kFailure;
        }

        IF_ERR_RET(config_agent.GetRawConfig(cc::kRecorder, recorder_name, &recorder_config));
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("get config from domain server failed: {1}", e.what());
        return kFailure;
    }

    return Launch(recorder_config, recorder_name, recorder_id, recovery_mode, nullptr, record_agent);
}

void Recorder::DeleteRecorder(const std::string recorder_name, const RecorderId& recorder_id)
{
    uint64_t timeout = 10000000000;  // 10s,单位ns
    std::lock_guard<std::mutex> lck(recorder_map_mtx_);
    auto it = recorder_map_.find(recorder_name);
    if (it != recorder_map_.end() && recorder_id == it->second->id_)
    {
        // delete it->second;
        close(it->second->internal_keepalive_fds_[1]);
        it->second->internal_keepalive_fds_[1] = -1;
        adk::WaitPidUntil(recorder_id.GetPid(), timeout);
        recorder_map_.erase(it);        
        LOG_INFO("recorder is closed. recorder name: {1}, recorder pid: {2}", 
                 recorder_id.GetInsName(),
                 recorder_id.Id());        
    }
}

Recorder::Recorder() {}

Recorder::~Recorder()
{
    // TryDestroyMPs();
    // TryDestroyMQManager();
}

void Recorder::SignalHandler(int sig, siginfo_t* info, void* secret)
{
    if (sig == SIGUSR1
        && info != NULL
        && (info->si_int == kRecorderExitValue
            || info->si_int == kRecorderExitImmValue))
    {
        Recorder& recorder = Instance();
        recorder.Exit(info->si_int);
        return;
    }

    // if (g_ignored_sig_map.count(sig));
}

ErrorCode_def Recorder::Init(const Property& recorder_props, bool recovery)
{
    try
    {
        if (false == recovery)
        {
            LOG_INFO("Init, recorder_props: {1}", recorder_props.Dump(false));
        }
        else
        {
            LOG_INFO("Init(recovery mode), recorder_props: {1}",
                     recorder_props.Dump(false));
        }

        if (id_.IsVoid())
        {  //单元测试不经过Launch，如果id为空，可以设置一下
            id_ = RecorderId(getpid());
        }

        std::string record_shm_dir_name = recorder_props.GetValue(ccr::kShmDirectory, std::string());
        if (!record_shm_dir_name.empty())
        {
            // default shm file path: /dev/shm/user_name_
            // config shm file path: /dev/shm/config_direc/
            boost::system::error_code ec;
            try
            {
                if (!boost::filesystem::exists("/dev/shm/" + record_shm_dir_name, ec))
                {
                    boost::filesystem::create_directories("/dev/shm/" + record_shm_dir_name, ec);
                }
            }
            catch(...)
            {
                LOG_ERROR("recorder <{1}>, can not create folder <{2}> in '/dev/shm/' directory, detail info <{3}>",
                    Instance().GetId().GetInsName(), record_shm_dir_name, ec.message());
                return kFailure;
            }
            ShmFileDirectory::ChangeShmFilePrefix(record_shm_dir_name + "/");
        }
        worker_num_ = recorder_props.GetValue(
            ccr::kTxThreadNum, rcdv::kTxThreadNum);
        if (worker_num_ < 1)
        {
            LOG_WARN("{1}({2}) MUST >= {3}, set to {3} forcelly",
                     ccr::kTxThreadNum, worker_num_, rcdv::kTxThreadNum);
            worker_num_ = rcdv::kTxThreadNum;
        }

        AMI_TD_PARAM_JOB_BY_ENV("AMI_TEST_RECORDER_MT", [&](const char* env_param){
            if (std::to_string(worker_num_) != env_param)
            {
                LOG_ERROR("thread number error, <{1}>", worker_num_);
                abort();
            }

            LOG_INFO("thread number <{1}>", worker_num_);
        });

        for (int32_t i = 0; i < RealWorkerNum(); i++)
        {
            record_workers_.push_back(new SerialWorker());
        }

        data_path_ = RecordDataRoot(recorder_props.GetValue(ccr::kDataPath, rcdv::kDataPath), id_);

        LOG_INFO("the recorder data path is <{1}>", 
                 boost::filesystem::weakly_canonical(data_path_).string());

        std::string size_str =
            recorder_props.GetValue(ccr::kShmToUse, std::string());
        if (size_str.empty())
        {
            shm_to_use_ = ByteSize(rcdv::kShmToUse);
        }
        else
        {
            shm_to_use_ = ByteSize(size_str);
        }

        if (shm_to_use_ < ByteSize(rcdv::kShmToUseLLimit))
        {
            LOG_WARN("{1}({2}) MUST >= {3}, set to {3} forcelly",
                     ccr::kShmToUse, shm_to_use_,
                     ByteSize(rcdv::kShmToUseLLimit));
            shm_to_use_ = ByteSize(rcdv::kShmToUseLLimit);
        }

        if (shm_to_use_ > ByteSize(rcdv::kShmToUseULimit))
        {
            LOG_WARN("{1}({2}) MUST >= {3}, set to {3} forcelly",
                     ccr::kShmToUse, shm_to_use_,
                     ByteSize(rcdv::kShmToUseULimit));
            shm_to_use_ = ByteSize(rcdv::kShmToUseULimit);
        }

        if (recorder_props.HasValue(ccr::kMsgQueueSize))
        {
            msg_queue_size_ = recorder_props.GetValue(ccr::kMsgQueueSize, decltype(msg_queue_size_)::value_type());
        }

        if (recorder_props.HasValue(ccr::kUseMsgCRC))
        {
            use_msg_crc_ = recorder_props.GetValue(ccr::kUseMsgCRC, decltype(use_msg_crc_)::value_type());
        }

        if (recorder_props.HasValue(ccr::kIgnoreLostMsg))
        {
            ignore_lost_msg_ = recorder_props.GetValue(ccr::kIgnoreLostMsg, decltype(ignore_lost_msg_)::value_type());
        }

        if (false == recovery)
        {
            BackupShareMemoryData();
            IF_ERR_RET(control_server_.Init(boost::bind(&Recorder::OnRequest, this,
                                                        _1, _2, _3, _4),
                                            nullptr, boost::bind(&Recorder::OnClientNE, this)));

            IF_ERR_RET(CreateMPs());
            IF_ERR_RET(CreateMQManager(),
                       TryDestroyMPs() /*如果创建mq失败，将已经创建的mp清理掉*/);
        }
        else
        {
            IF_ERR_RET(AttachOrCreateMPs());
            IF_ERR_RET(AttachOrCreateMQManager(), {});

            if (may_lost_msg_ && CanIgnoreMsgLost())
            {
                // after this method return, the recorder directly start the control_server.
                // setup callbacks
                IF_ERR_RET(control_server_.Init(boost::bind(&Recorder::OnRequest, this, _1, _2, _3, _4),
                                                nullptr,
                                                boost::bind(&Recorder::OnClientNE, this)));
            }
        }

        if (recorder_props.HasValue(ccr::kCpuAffinity))
        {
            cpu_affinity_ = recorder_props.GetStringValue(ccr::kCpuAffinity);
        }

        if (!cpu_affinity_.empty())
        {
            adk::SetCpuAffinity(cpu_affinity_);
        }

        if (recorder_props.HasValue(ccr::kSnapshotCycleMilli))
        {
            snapshot_cycle_milli_ =
                recorder_props.GetValue(ccr::kSnapshotCycleMilli,
                                        decltype(snapshot_cycle_milli_)::value_type());

            if (*snapshot_cycle_milli_ > 0 && *snapshot_cycle_milli_ < 1000)
            {
                snapshot_cycle_milli_ = 1000u;
            }
        }
        else
        {
            snapshot_cycle_milli_ = rcdv::kSnapshotCycleMilli;
        }

        if (recorder_props.HasValue(ccr::kReportStatus))
        {
            report_status_ = recorder_props.GetBoolValue(ccr::kReportStatus);
        }
    }
    catch (const Property::InvalidJsonString& e)
    {
        LOG_ERROR("config error: {1}", e.what());
        return kFailure;
    }

    return kSuccess;
}

bool Recorder::OnRequest(const void* request_buf,
                         uint32_t request_len,
                         void* reply_buf,
                         uint32_t* reply_len)
{
    Property request(std::string(static_cast<const char*>(request_buf), request_len));
    int req_type = request.GetValue(kMessageType,
                                    ControlMessageType::kInvalidMessage);

    Property reply;
    switch (req_type)
    {
    case ControlMessageType::kConnectToRecorder:
    {
        reply.SetValue(kMQManagerName, GetMQManagerName());
        reply.SetValue(kDataPath, data_path_);
        // FIXME: one context Recovery and another Bootstrap is not support
        //        fall through
    }
    case ControlMessageType::kConnectToExistRecorder:
    {
        if (!request.GetValue(kIsAgentRecovery, false))
        {
            // bootstrap scenario
            // backup context share memory data
            BackupShareMemoryData(request.GetValue(kContextName, ""));
        }
        break;
    }
    case ControlMessageType::kCreateMergeChannels:
    {
        if (kSuccess != CreateMergeChannels(request, reply))
        {
            return false;
        }

        break;
    }
    case ControlMessageType::kCreateMessageChannel:
    {
        if (kSuccess != CreateMessageChannel(request, reply))
        {
            return false;
        }

        break;
    }
    case ControlMessageType::kCreateStatusChannel:
    {
        if (kSuccess != CreateStatusChannel(request, reply))
        {
            return false;
        }

        break;
    }
    case ControlMessageType::kDynamicSyncIdMaps:
    {
        IncreSyncIdMaps(request, reply);
        break;
    }
    default:
        return false;
    }

    std::string reply_str = reply.Dump(false);
    if (*reply_len < reply_str.length())
    {
        LOG_ERROR((bl::format("reply buffer NOT efficient({1} < {2})")
                   % *reply_len % reply_str.size())
                      .str());
        return false;
    }

    memcpy(reply_buf, reply_str.c_str(),
           (*reply_len = reply_str.length()));
    return true;
}

ErrorCode_def Recorder::CreateMQManager()
{
    /**
     * 给mq预分配的内存大小应该为
     * 'recorder可用的内存' - 'mp使用的最大内存'
     */
    const ByteSize avail_size = shm_to_use_ - ShmUsedByMPUpLimit();

    mq_manager_ = adk::MQManager::CreateExt(GetMQManagerName(),
                                            sizeof(MQMsgEntry),
                                            avail_size.RawSize());
    if (nullptr == mq_manager_)
    {
        LOG_FATAL("create mq manager '{1}' failed", GetMQManagerName());
        return ErrorCode::kFailure;
    }
    else
    {
        LOG_INFO("create mq manager '{1}' ok", GetMQManagerName());
    }

    track_info_q_ = mq_manager_->CreateSharedMPSCQueue(kTrackInfoQName,
                                                       kChannelCapacity);
    if (nullptr == track_info_q_)
    {
        LOG_FATAL("create track info q '{1}' failed", kTrackInfoQName);
        return ErrorCode::kFailure;
    }
    else
    {
        LOG_INFO("create track info q '{1}' ok", kTrackInfoQName);
    }

    return ErrorCode::kSuccess;
}

ErrorCode_def Recorder::AttachOrCreateMQManager()
{
    mq_manager_ = adk::MQManager::Attach(GetMQManagerName());
    if (nullptr == mq_manager_)
    {
        LOG_WARN("attach mq manager '{1}' failed when recovery",
                 GetMQManagerName());
        if (!may_lost_msg_)
        {
            LOG_WARN("maybe recovery with message loss");
        }
        may_lost_msg_ = true;

        if (!CanIgnoreMsgLost())
        {
            return kSuccess;
        }

        if (kSuccess == CreateMQManager())
        {
            LOG_INFO("attach fail then recreate mq manager ok '{1}' "
                     "when recovery",
                     GetMQManagerName());
            return kSuccess;
        }
        else
        {
            LOG_ERROR("attach fail then recreate mq manager '{1}' failed "
                      "when recovery",
                      GetMQManagerName());
            return kFailure;
        }
    }
    else
    {
        LOG_INFO("attach mq manager '{1}' ok when recovery",
                 GetMQManagerName());

        track_info_q_ = mq_manager_->AttachSharedMPSCQueue(kTrackInfoQName);
        if (nullptr == track_info_q_)
        {
            LOG_ERROR("attach track info q '{1}' failed when recovery",
                      kTrackInfoQName);
            return ErrorCode::kFailure;
        }
        else
        {
            track_info_q_->Consistent();
            LOG_INFO("attach track info q '{1}' ok when recovery",
                     kTrackInfoQName);
            return kSuccess;
        }
    }
}

ErrorCode_def Recorder::CreateMPs()
{
    IF_ERR_RET(mp_manager_.CreateMPTable(GetMPTableName()),
               LOG_FATAL("create mp manager '{1}' failed", GetMPTableName()));
    LOG_INFO("create mp manager '{1}' ok", GetMPTableName());

    msgdata_buffer_pool_ = mp_manager_.CreateSharedPool(GetMsgDataBufferPoolName(),
                                                        kFileBufSizeInShm, kChannelCapacity);
    if (nullptr == msgdata_buffer_pool_)
    {
        LOG_FATAL("create msgdata buffer pool '{1}' failed.",
                  GetMsgDataBufferPoolName());
        return kFailure;
    }
    else
    {
        LOG_INFO("create msgdata buffer pool '{1}' ok.",
                 GetMsgDataBufferPoolName());
    }

    return kSuccess;
}

ErrorCode_def Recorder::AttachOrCreateMPs()
{
    if (kSuccess != mp_manager_.AttachMPTable(GetMPTableName()))
    {
        LOG_WARN("attach mp manager '{1}' failed when recovery",
                 GetMPTableName());
        if (!may_lost_msg_)
        {
            LOG_WARN("maybe recovery with message loss");
        }
        may_lost_msg_ = true;

        if (!CanIgnoreMsgLost())
        {
            return kSuccess;
        }

        if (kSuccess == CreateMPs())
        {
            LOG_INFO("attach fail then recreate mp manager '{1}' ok "
                     "when recovery",
                     GetMPTableName());
            return kSuccess;
        }
        else
        {
            LOG_ERROR("attach fail then recreate mp manager '{1}' failed "
                      "when recovery",
                      GetMPTableName());
            return kFailure;
        }
    }
    else
    {
        LOG_INFO("attach mp manager '{1}' ok when recovery",
                 GetMPTableName());
        msgdata_buffer_pool_ = mp_manager_.AttachSharedPool(GetMsgDataBufferPoolName());
        if (nullptr == msgdata_buffer_pool_)
        {
            LOG_ERROR("attach msgdata buffer pool '{1}' failed when recovery",
                      GetMsgDataBufferPoolName());
            return kFailure;
        }
        else
        {
            msgdata_buffer_pool_->Consistent();
            LOG_INFO("attach msgdata buffer pool '{1}' ok.",
                     GetMsgDataBufferPoolName());
            return kSuccess;
        }
    }
}

ErrorCode_def Recorder::Recover(const Property& recorder_props)
{
    LOG_INFO("recovering...");

    for (auto it = record_workers_.begin(); it != record_workers_.end(); ++it)
    {
        (*it)->Launch();
    }
    LOG_INFO("record workers lauched.");

    IntervalLogger inv_logger(10);

    // make sure the shm pointer was exist in the queue
    // even if message track recovery failed.

    // pop the share memory pointer from the queue
    // save the pointer to local array
    
    std::list<adk::ShmPointer> shm_pointer_list;

    // ==========================================================
    // critial section begin
    do
    {
        adk::Entry* entry;
        adk::ErrorCode_def ec = track_info_q_->WaitEntry(&entry);
        if (ec == kSuccess)
        {   
            auto* shm_ptr = (adk::ShmPointer*)(entry->buffer);
            shm_pointer_list.push_back(*shm_ptr);
            saved_track_info_.insert(shm_ptr->value);
            track_info_q_->FreeEntry(entry);
            continue;
        }

        // all shm_pointer saved
        break;
    } while (true);

    // requeuing the shm_pointers
    for (adk::ShmPointer shm_pointer : shm_pointer_list)
    {
        if (kSuccess != Recorder::GetTrackInfoQ()->Push(shm_pointer))
        {
            LOG_ERROR("saving message track shm_pointer failed，pointer value <{1}>",
                shm_pointer.value);
            return kFailure;
        }
    }
    // critial section end
    // ==========================================================

    // log shm_pointer details
    {
        std::string details;
        for (adk::ShmPointer shm_pointer : shm_pointer_list)
        {
            details.append(std::to_string(shm_pointer.value) + ", ");
        }
        boost::trim_if(details, boost::is_any_of(" ,"));

        LOG_INFO("total <{1}> message track poped from the track_info_q, track value details: <{2}>", 
                 shm_pointer_list.size(), details);
    }

    std::set<uint64_t> error_tracks;
    std::string error_tracks_str;
    for (adk::ShmPointer shm_point : shm_pointer_list)
    {
        MessageTrack* track = MessageTrack::NewTrack(shm_point);
        assert(track);

        AMI_TD_PARAM_JOB_BY_ENV(
            "AMI_TEST_MAKE_CRC_ERROR",
            [](char* env_str){
                if (std::string("Immediate") == env_str)
                {
                    g_simulate_crc_error = true;
                }
            });

        int32_t ec = track->Init(shm_point, DispatchWorker());
        if (kSuccess == ec)
        {
            AddTrack(track->GetTrackPath(), track);
            track->Start();
        }
        else
        {
            if (ec != kNoResources
                && env_exit_on_track_recovery_fail)
            {
                LOG_ERROR("recovery message track '{1}' failed.",
                      track->GetTrackPath());
                // delete track;
                _exit(1);
            }
            error_tracks.insert(shm_point.value);
            error_tracks_str.append(std::to_string(shm_point.value));
            error_tracks_str.append(":");
            error_tracks_str.append(track->GetTrackDataPath());
            error_tracks_str.append(", ");
        }
    }

    boost::trim_if(error_tracks_str, boost::is_any_of(" ,"));
    LOG_INFO("total <{1}> message tracks recovery failed, details: <{2}>", 
        error_tracks.size(), error_tracks_str);

    // some tracks were not found
    // eg. the track files were deleted
    // drop the share memory track records
    if (!error_tracks.empty())
    {
        shm_pointer_list.clear();
        saved_track_info_.clear();
        // ==========================================================
        // critial section begin
        do
        {
            adk::Entry* entry;
            adk::ErrorCode_def ec = track_info_q_->WaitEntry(&entry);
            if (ec == kSuccess)
            {   
                auto* shm_ptr = (adk::ShmPointer*)(entry->buffer);
                if (!error_tracks.count(shm_ptr->value))
                {
                    // filtering the error tracks
                    shm_pointer_list.push_back(*shm_ptr);
                    saved_track_info_.insert(shm_ptr->value);
                }

                track_info_q_->FreeEntry(entry);
                continue;
            }

            // all shm_pointer saved
            break;
        } while (true);

        // requeuing the shm_pointers
        for (adk::ShmPointer shm_pointer : shm_pointer_list)
        {
            if (kSuccess != Recorder::GetTrackInfoQ()->Push(shm_pointer))
            {
                LOG_ERROR("saving message track shm_pointer failed，pointer value <{1}>",
                    shm_pointer.value);
                return kFailure;
            }
        }
        // critial section end
        // ==========================================================
    }

    LOG_INFO("{1} track recovered", TrackNum());

    LOG_INFO("recording pending messages...");
    do
    {
        usleep(1000 * 1000);

        ErrorCode_def ec = CheckTracksRecovered();
        if (kSuccess == ec)
        {
            break;
        }
        else if (kFailure == ec)
        {
            return kFailure;
        }
        else
        {
            INV_LOG_INFO(inv_logger,
                         "\n"
                         "*recovering**********"
                         "\n"
                         "{1}\n"
                         "*********************",
                         *this);
        }
    } while (true);

    LOG_INFO("pending messages recorded ok.");

    StopTracks();
    // in recovery mode
    // clear the share memory message queue and reset the repairing status
    ClearTracksMQ();

    LOG_INFO("starting control server...");
    IF_ERR_RET(control_server_.Init(boost::bind(&Recorder::OnRequest, this, _1, _2, _3, _4),
                                    nullptr,
                                    boost::bind(&Recorder::OnClientNE, this)));
    IF_ERR_RET(control_server_.Start());
    LOG_INFO("control server started ok.");

    recovery_mode_ = false;
    return kSuccess;
}

ErrorCode_def Recorder::Start()
{
    LOG_INFO("starting...");

    for (auto it = record_workers_.begin(); it != record_workers_.end(); ++it)
    {
        (*it)->Launch();
    }
    LOG_INFO("record workers lauched.");

    IF_ERR_RET(control_server_.Start());

    LOG_INFO("started ok");
    return kSuccess;
}

void Recorder::Idle()
{
    ready_to_go_ = true;
    LOG_INFO("ready to go.");

    IntervalLogger inv_logger(*snapshot_cycle_milli_ / 1000);
    while (!to_exit_)
    {
        usleep(1000 * 1000);
        INV_LOG_INFO(inv_logger,
                     "\n"
                     "*running**************"
                     "\n"
                     "{1}\n"
                     "*********************",
                     *this);

        char c;
        int ret = ::read(keepalive_fds_[0], &c, 1);
        if (ret == 0
            || (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
        {
            to_exit_ = true;
            exit_immediatly_ = false;
            LOG_INFO("parent deaded, ret = <{1}>, errno = <{2}>", ret, errno);
        }
    }

    if (!exit_immediatly_)
    {
        // in application crash scenario, force delay 500ms
        usleep(500 * 1000);

        NotifyTracksToQuit();

        do
        {
            usleep(1000);

            ErrorCode_def ec = CheckTracksQuitable();
            if (kSuccess == ec)
            {
                LOG_INFO("client not exist, exit elegantly ok");
                break;
            }
            else if (kFailure == ec)
            {
                LOG_WARN("client not exist, "
                         "exit elegantly with some error");
                break;
            }
            else
            {
                INV_LOG_INFO(inv_logger,
                             "\n"
                             "*quiting elegantly***"
                             "\n"
                             "{1}\n"
                             "*********************",
                             *this);
            }
        } while (true);
    }

    LOG_INFO("\n"
             "*leaving*************"
             "\n"
             "{1}\n"
             "*********************",
             *this);

    TearDown();

    LOG_INFO("recorder exiting completely\n");
}

void Recorder::Exit(int value)
{
    LOG_INFO("exiting immediatly...");

    exit_immediatly_ = (value == kRecorderExitImmValue);
    to_exit_         = true;
}

void Recorder::TearDown()
{
    control_server_.Stop();

    for (auto it = record_workers_.begin(); it != record_workers_.end(); ++it)
    {
        (*it)->Stop();
        // delete (*it);
    }
    record_workers_.clear();
    LOG_INFO("record workers stopped.");

    DestroyTracks();

    // TryDestroyMPs();
    // TryDestroyMQManager();
}

void Recorder::RegisterSignal()
{
    struct sigaction sa;
    sa.sa_sigaction = &Recorder::SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);
}

std::string& Recorder::GetBackupTime()
{
    boost::mutex::scoped_lock lock_guard(backup_time_mutex_);
    if (!backup_time_.empty())
        return backup_time_;

    char output[1024];
    memset(output, 0x00, sizeof(output));
    std::time_t now = std::time(nullptr);
    const size_t str_len =
        std::strftime(output, sizeof(output), "%Y%m%d_%H:%M:%S",
                      std::localtime(&now));
    backup_time_.assign(output, str_len);
    return backup_time_;
}

bf::path& Recorder::CreateRecorderShmDir()
{
    bs::error_code bs_ec;
    static bf::path shm_backup_dirpath = bf::path(data_path_)
        / bf::path(kBackupDirName)
        / bf::path((bl::format("{1}_recorder_{2}_shm_{3}")
                    % GetLoginUserName()
                    % Instance().GetId()
                    % GetBackupTime())
                       .str())
        / kShmPath;

    bf::create_directories(shm_backup_dirpath, bs_ec);
    return shm_backup_dirpath;
}

void Recorder::BackupRecorderShmData()
{
    const bf::path& to = CreateRecorderShmDir();
    bs::error_code bs_ec;

    std::string recorder_name = Instance().GetId().GetInsName();
    std::vector<std::string> rc_shm_name_vec = RecorderShareMemoryFiles(recorder_name);

    for (std::string& shm_file_name : rc_shm_name_vec)
    {
        bf::path shm_file_path = bf::path(kShmPath) / shm_file_name;

        if (bf::exists(shm_file_path, bs_ec))
        {
            bf::copy_file(shm_file_path, to / shm_file_name, bs_ec);
            if (bs_ec)
            {
                LOG_WARN("copy shm file failed, file: {1}, error message: {2}", shm_file_path, bs_ec.message());
            }
            bf::remove(shm_file_path, bs_ec);
            if (bs_ec)
            {
                LOG_WARN("remove shm file failed, file: {1}, error message: {2}", shm_file_path, bs_ec.message());
            }
        }
    }
}

bf::path Recorder::CreateContextShmDir(const std::string& ctx_name)
{
    bs::error_code bs_ec;
    bf::path shm_backup_dirpath = bf::path(data_path_)
        / bf::path(kBackupDirName)
        / bf::path((bl::format("{1}_{2}_{3}")
                    % GetLoginUserName()
                    % ctx_name
                    % GetBackupTime())
                       .str())
        / kShmPath;

    bf::create_directories(shm_backup_dirpath, bs_ec);
    return shm_backup_dirpath;
}

void Recorder::BackupContextShmDir(const std::string ctx_name)
{
    bs::error_code bs_ec;
    const bf::path& to                     = CreateContextShmDir(ctx_name);
    std::vector<std::string> ctx_shm_files = ShareMemoryFiles(ctx_name);
    for (std::string& shm_file_name : ctx_shm_files)
    {
        bf::path shm_file_path = bf::path(kShmPath) / shm_file_name;

        if (bf::exists(shm_file_path, bs_ec))
        {
            bf::copy_file(shm_file_path, to / shm_file_name, bs_ec);
        }
    }
}

void Recorder::BackupShareMemoryData(const std::string& ctx_name)
{
    try
    {
        if (ctx_name.empty())
        {
            boost::mutex::scoped_lock lock_guard(backup_mutex_);
            if (!backup_recorder_shm_data_done_)
            {
                LOG_INFO("backup recorder share memory data");
                BackupRecorderShmData();
                backup_recorder_shm_data_done_ = true;
            }
            return;
        }

        boost::mutex::scoped_lock lock_guard(backup_mutex_);
        if (backup_shm_data_done_set_.find(ctx_name)
            == backup_shm_data_done_set_.end())
        {
            LOG_INFO("backup context <{1}> share memory data", ctx_name);
            BackupContextShmDir(ctx_name);
            backup_shm_data_done_set_.insert(ctx_name);
        }
    }
    catch (...)
    {
        LOG_ERROR("exception catched, {1}",
                  boost::current_exception_diagnostic_information());
    }
}

void Recorder::TryBackupContextFiles(const std::string& track_path)
{
    if (HasSameContextTrackExist(track_path))
        return;

    bs::error_code bs_ec;
    bf::path backup_dirpath = bf::path(data_path_)
        / bf::path(kBackupDirName);
    bf::create_directories(backup_dirpath, bs_ec);

    std::string ctx_name     = CtxName(track_path);
    bf::path ctxdata_dirpath = bf::path(data_path_)
        / bf::path(ctx_name);

    if (bf::exists(ctxdata_dirpath, bs_ec))
    {
        bf::path ctx_backup_dirpath = backup_dirpath
            / bf::path((bl::format("{1}_{2}_{3}")
                        % GetLoginUserName()
                        % ctx_name
                        % GetBackupTime())
                           .str());

        try
        {
            bf::directory_iterator end_it;
            for (boost::filesystem::directory_iterator file_it(ctxdata_dirpath);
                 file_it != end_it; ++file_it)
            {
                std::string leaf_name = file_it->path().leaf().string();
                bf::rename(file_it->path(), ctx_backup_dirpath / leaf_name, bs_ec);
            }
        }
        catch (...)
        {
            LOG_ERROR("exception catched, {1}",
                      boost::current_exception_diagnostic_information());
        }
    }
}

// create maps file
// build the connection between id and objects(endpint/transport)
void Recorder::GenIdMaps(const Property& request)
{
    bs::error_code ec;
    std::string dir = data_path_ + "/" + Instance().GetId().GetInsName() + "/maps/";
    // dir is : recorder_data/ContextName/maps/
    bf::create_directories(dir, ec);
    std::string file_path = dir + "id_maps";
    if (bf::exists(file_path, ec))  // the recovery scenario
    {
        // keep the old id_maps file without change
        file_path.append("_new");
    }

    std::ofstream id_map_file(file_path);
    if (!id_map_file.good())
    {
        LOG_ERROR("open file <{1}> failed, errno <{2}>, desc <{3}>",
                  file_path, errno, ::strerror(errno));
        return;
    }
    id_map_file << request.GetValue("Recorder", Property()).Dump(true);
    id_map_file.close();
}

ErrorCode_def
Recorder::CreateMergeChannels(const Property& request, Property& reply)
{
    std::string track_path = request.GetValue(kPath, std::string());
    if (track_path.empty())
    {
        LOG_ERROR("empty track path");
        return kFailure;
    }

    if (TrackExist(track_path))  // track exist means we have recovery the track by share memory data
    {  ///不丢消息的恢复场景下重建通道
        if (!GetTrack(track_path)->HasRebuilt())
        {
            MessageTrack* track = GetTrack(track_path);
            // attach libami memory buffer pool
            // save the data file buffer on the TrackInfo share memory queue
            IF_ERR_RET(track->Init(track_path, request, reply),   
                       LOG_ERROR("rebuild recovered "             
                                 "RxMessageTrack '{1}' failed",
                                 track_path));
            LOG_INFO("rebuild recovered RxMessageTrack '{1}' ok", track_path);
            track->Start();
            return kSuccess;
        }

        LOG_ERROR("RxMessageTrack {1} already exist", track_path);
        return kFailure;
    }

    if (may_lost_msg_)
    { /*丢消息的恢复场景下重建通道*/
        // system crash/reboot scenario
        // "tx/" directory was removed
        // "rx/" directory was removed
        // "ackedsqn/" directory was removed
    }
    else
    {  ///新建通道
        // bootstrap or recovery/rejoin without message loss
        // backup the context data files
        TryBackupContextFiles(track_path);

        if (TrackNum() + 1 > kChannelCapacity)
        {
            return kNoResources;
        }
    }

    // note: create id_map_file after TryBackupContextFiles
    GenIdMaps(request);

    RxMessageTrack* track = new RxMessageTrack();
    Property req(request);
    req.SetValue(ccr::kDataPath, data_path_);

    // create directory, data file, index files
    // if there ware message loss, scan the ordinal index file to recover
    IF_ERR_RET(track->Init(track_path, req, reply, RealWorkerNum() - 1),
               (delete track);
               LOG_ERROR("create RxMessageTrack {1} failed.", track_path));

    AddTrack(track->GetTrackPath(), track);
    track->Start();

    return ErrorCode::kSuccess;
}

ErrorCode_def
Recorder::CreateMessageChannel(const Property& request, Property& reply)
{
    std::string track_path = request.GetValue(kPath, std::string());
    if (track_path.empty())
    {
        LOG_ERROR("empty track path");
        return kFailure;
    }

    if (TrackExist(track_path))
    {  ///不丢消息的恢复场景下重建通道
        if (!GetTrack(track_path)->HasRebuilt())
        {
            MessageTrack* track = GetTrack(track_path);
            IF_ERR_RET(track->Init(track_path, request, reply),
                       LOG_ERROR("rebuild recovered "
                                 "TxMessageTrack '{1}' failed",
                                 track_path));
            LOG_INFO("rebuild recovered TxMessageTrack '{1}' ok", track_path);
            track->Start();
            return kSuccess;
        }

        LOG_ERROR("TxMessageTrack {1} already exist", track_path);
        return kFailure;
    }

    if (may_lost_msg_)
    { /*丢消息的恢复场景下重建通道*/
    }
    else
    {  ///新建通道
        TryBackupContextFiles(track_path);

        if (TrackNum() + 1 > kChannelCapacity)
        {
            return kNoResources;
        }
    }

    TxMessageTrack* track = new TxMessageTrack();
    Property req(request);
    req.SetValue(ccr::kDataPath, data_path_);

    IF_ERR_RET(track->Init(track_path, req, reply, DispatchWorker()),
               (delete track);
               LOG_ERROR("create TxMessageTrack '{1}' failed.", track_path));

    AddTrack(track->GetTrackPath(), track);
    track->Start();

    return ErrorCode::kSuccess;
}

ErrorCode_def
Recorder::CreateStatusChannel(const Property& request, Property& reply)
{
    std::string track_path = request.GetValue(kPath, std::string());
    if (track_path.empty())
    {
        LOG_ERROR("empty track path");
        return kFailure;
    }

    if (TrackExist(track_path))
    {  ///不丢消息的恢复场景下重建通道
        if (!GetTrack(track_path)->HasRebuilt())
        {
            MessageTrack* track = GetTrack(track_path);
            IF_ERR_RET(track->Init(track_path, request, reply),
                       LOG_ERROR("rebuild recovered "
                                 "StMessageTrack '{1}' failed",
                                 track_path));
            LOG_INFO("rebuild recovered StMessageTrack '{1}' ok", track_path);

            track->Start();
            return kSuccess;
        }

        LOG_ERROR("StMessageTrack {1} already exist", track_path);
        return kFailure;
    }

    if (may_lost_msg_)
    { /*丢消息的恢复场景下重建通道*/
    }
    else
    {  ///新建通道
        TryBackupContextFiles(track_path);

        if (TrackNum() + 1 > kChannelCapacity)
        {
            return kNoResources;
        }
    }

    StMessageTrack* track = new StMessageTrack();
    Property req(request);
    req.SetValue(ccr::kDataPath, data_path_);

    IF_ERR_RET(track->Init(track_path, req, reply, DispatchWorker()),
               (delete track);
               LOG_ERROR("create StMessageTrack failed."));

    AddTrack(track->GetTrackPath(), track);
    track->Start();

    return ErrorCode::kSuccess;
}

void Recorder::SignalParent(char s)
{
    if (to_signal_parent_)
    {
        auto r = write(pipefd_[1], &s, 1);
        if (r <= 0)
        {
            RecorderExit(1);
        }
        close(pipefd_[1]);
    }
}

void Recorder::StopTracks()
{
    const auto& ti = track_set_.get<MsgTrack::TrackPathI>();
    for (auto& track_elem : ti)
    {
        track_elem.track->Stop();
    }
}

void Recorder::ClearTracksMQ()
{
    const auto& ti = track_set_.get<MsgTrack::TrackPathI>();
    for (auto& track_elem : ti)
    {
        track_elem.track->ClearQueueMsgAtRecovery();
    }
}

void Recorder::DestroyTracks()
{
    const auto& ti = track_set_.get<MsgTrack::TrackPathI>();
    for (auto& track_elem : ti)
    {
        delete track_elem.track;
    }

    track_set_.clear();
}

ErrorCode_def
Recorder::CheckTracksRecovered() const
{
    bool has_any_error = false;
    const auto& ti     = track_set_.get<MsgTrack::TrackPathI>();
    for (const auto& track_elem : ti)
    {
        if (!track_elem.track->IsRecoveryOk()
            && !track_elem.track->HasError())
        {
            return kTryAgain;
        }

        if (track_elem.track->HasError())
        {
            has_any_error = true;
        }
    }

    if (has_any_error)
    {
        return kFailure;
    }
    else
    {
        return kSuccess;
    }
}

void Recorder::NotifyTracksToQuit()
{
    const auto& ti = track_set_.get<MsgTrack::TrackPathI>();
    for (auto& track_elem : ti)
    {
        track_elem.track->ReadyToQuit();
    }
}

ErrorCode_def
Recorder::CheckTracksQuitable() const
{
    bool has_any_error = false;
    const auto& ti     = track_set_.get<MsgTrack::TrackPathI>();
    for (const auto& track_elem : ti)
    {
        if (!track_elem.track->CanQuitElegantly()
            && !track_elem.track->HasError())
        {
            return kTryAgain;
        }

        if (track_elem.track->HasError())
        {
            has_any_error = true;
        }
    }

    if (has_any_error)
    {
        return kFailure;
    }
    else
    {
        return kSuccess;
    }
}

std::ostream& operator<<(std::ostream& os, const Recorder& recorder)
{
    if (recorder.track_set_.empty())
    {
        os << "Recorder/" << recorder.id_.Id() << ": \n";
        os << "may_lost_msg: " << std::boolalpha
           << recorder.may_lost_msg_;
    }
    else
    {
        os << "Recorder/" << recorder.id_.Id() << ": \n";
        os << "may_lost_msg: " << std::boolalpha
           << recorder.may_lost_msg_ << '\n';

        const auto& ti =
            recorder.track_set_.get<Recorder::MsgTrack::TrackPathI>();
        size_t cnt = ti.size();
        for (const auto& track_elem : ti)
        {
            if (1 == cnt--)
            {
                os << *track_elem.track;
            }
            else
            {
                os << *track_elem.track << '\n';
            }
        }
    }

    return os;
}

void Recorder::DumpToPtree(bt::ptree& status_tree) const
{
    status_tree.put("may_lost_msg", may_lost_msg_);

    bt::ptree& track_status_tree = status_tree.add_child("tracks", bt::ptree());

    const auto& ti = track_set_.get<Recorder::MsgTrack::TrackPathI>();
    for (const auto& track_elem : ti)
    {
        track_elem.track->DumpToPtree(track_status_tree);
    }
}

void Recorder::OnClientNE()
{
    LOG_INFO("client not exist, exiting elegantly...");

    exit_immediatly_ = false;
    to_exit_         = true;
}

ByteSize Recorder::ShmUsedByMPUpLimit()
{
    /**
     * 目前每个通道只有数据文件的buffer占用共享内存用于故障恢复，其他
     * 所有的索引文件都不占用共享内存。最后*2主要考虑到adk mp manager
     * 的开销
     */
    return ByteSize(kChannelCapacity * kFileBufSizeInShm * 2);
}

ErrorCode_def Recorder::IncreSyncIdMaps(const Property& request, Property& reply)
{
    bs::error_code ec;
    std::string dir = data_path_ + "/" + Instance().GetId().GetInsName() + "/maps/";
    // dir is : recorder_data/ContextName/maps/
    bf::create_directories(dir, ec);
    std::string file_path = dir + "id_maps";

    std::fstream id_map_file(file_path, std::ios::in | std::ios::out);
    if (!id_map_file.good())
    {
        LOG_ERROR("open file <{1}> failed, errno <{2}>, desc <{3}>",
                  file_path, errno, ::strerror(errno));
        return ErrorCode::kFailure;
    }
    boost::property_tree::ptree ptree;
    try 
    {
        boost::property_tree::json_parser::read_json(id_map_file, ptree);  
    }
    catch (std::exception& e) 
    {
        LOG_ERROR("read file <{1}> failed, catch exception: <{2}>", file_path, e.what());
        return ErrorCode::kFailure;
    }
   
    Property props;
    PropertyContainer::SetPtree(&props, ptree);
    
    static std::set<uint32_t> s_idmaps;
    std::vector<Property> id_object_array = props.GetValue("IdObject", std::vector<Property>());
    if (s_idmaps.empty())
    {
        for (const auto& item_props : id_object_array)
        {
            s_idmaps.insert(item_props.GetValue("Id", (uint32_t)0));
        }    
    }
    
    Property id_objects = request.GetValue(kDynamicSyncIdMaps, Property());
    std::vector<Property> incre_id_object_array = id_objects.GetValue("IdObject", std::vector<Property>());
    
    for (const auto& item_props : incre_id_object_array)
    {
        auto id = item_props.GetValue("Id", (uint32_t)0);
        if (std::find(s_idmaps.begin(), s_idmaps.end(), id) == s_idmaps.end())
        {
            id_object_array.emplace_back(item_props);
            s_idmaps.insert(id);
        }
    }
    props.SetValue("IdObject", id_object_array);
    
    id_map_file.seekg(0);
    id_map_file << props.Dump(true);
    id_map_file.close();
    reply.SetValue(kMessageType, ControlMessageType::kDynamicSyncIdMaps);
    return ErrorCode::kSuccess;    
}

}  // namespace ami
