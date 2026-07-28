#include "sharding_agent.h"
#include "util.h"

#include <dlfcn.h>

#include <boost/log/expressions.hpp>
#include <boost/log/expressions/keyword_fwd.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <adk/property.h>
#include <adk/boost_logger.h>

namespace aaf
{
extern bool is_disaster_backup_inter(bool is_ha);
extern bool is_disaster_context_inter(bool is_ha);
}

class SideChannelHandler : public adk::sccl::AgentEventHandler
{
    ADK_LOG_DECLARE_AC(73010);
public:
    /**
     * @brief   有新的Proxy连接
     *
     * @param   process   proxy名称
     * @param   pid       proxy进程的进程ID
     * @param   tid       proxy线程的线程ID
     *
     * @return  true  : 接受该Proxy的数据
     *          false : 拒绝该Proxy的数据
     */
    bool OnNewProxy(const std::string& process, int32_t pid, int32_t tid) override
    {
        ADK_LOG_INFO_AC_TF("Proxy is up", "name: {1}, pid: {2}", process, pid);
        return true;
    }

    /**
     * @brief   Proxy断开连接
     *
     * @param   process   proxy名称
     * @param   pid       proxy进程的进程ID
     * @param   tid       proxy线程的线程ID
     */
    void OnProxyBroken(const std::string& process, int32_t pid, int32_t tid) override
    {
        ADK_LOG_INFO_AC_TF("Proxy is down", "name: {1}, pid: {2}", process, pid);
    }
};
ADK_LOG_DEFINE(SideChannelHandler);

namespace sharding
{

ADK_LOG_DEFINE(ShardingAgent);
ADK_LOG_DEFINE(ShmDataMgr);

using boost::interprocess::shared_memory_object;

using boost_ptree = boost::property_tree::ptree;

const bool kHaCtx = true; 
const bool kSgtCtx = false; 

std::string GetFormatLocalTime(const std::string& format)
{
    time_t time_point = time(0);
    struct tm* timeinfo;
    timeinfo = localtime(&time_point);

    char buffer[64];
    strftime(buffer, 64, format.c_str(), timeinfo);
    return std::string(buffer);
}

void ShardingAgent::ProxyLogAgent()
{
    ADK_LOG_INFO_AC_TF("start to consume sccl", "");
    adk::sccl::Entry* entry = nullptr;
    std::string log_dir = GetLogDir();

    constexpr int64_t kRotateCheckInterval = 1000 * 1000 * 1000;    // 1 second

    while (is_running())
    {
        entry = sccl_agent_->TryWaitEntry();
        if (entry == nullptr)
        {
            int64_t now_time = adk::timespec_now();
            if (ADK_UNLIKELY(now_time > log_rotate_check_time_ + kRotateCheckInterval))
            {
                std::string now_date = GetFormatLocalTime(log_date_format_);
                if (now_date != current_date_)
                {
                    current_date_ = now_date;
                    log_file_name_tri_ = (boost::format(log_file_format_tri_) 
                                        % GetLogDir()
                                        % context_name_ 
                                        % current_date_
                                        ).str();
                    strcpy(log_name_tri_shm_, log_file_name_tri_.c_str());

                    log_file_name_ = (boost::format(log_file_format_)
                                    % GetLogDir() 
                                    % context_name_
                                    % current_date_
                                    ).str();
                    strcpy(log_name_shm_, log_file_name_.c_str());
                    ADK_LOG_DEBUG_AC_TF("", "update time, log_file_name: {1}, log_file_name_tri: {2}", 
                                        log_file_name_, 
                                        log_file_name_tri_);
                }
                log_rotate_check_time_ = now_time;
            }
            usleep(100);
            continue;
        }

        ProxyInfoHead* info_head = (ProxyInfoHead*)(entry->Buffer());
        switch (info_head->info_type)
        {
        case ProxyInfoType::kLogInfo:
        {
            ProxyInfoLog* info = (ProxyInfoLog*)info_head;

            pid_t pid = info->pid;
            pid_t tid = info->tid;

            adk::log::LogLevel level  = info->level;
            adk::log::LogCode code    = info->code;
            std::string module_name   = info->module_name;
            std::string function_name = info->function_name;
            uint32_t src_line         = info->src_line;

            std::string title(info->title_message, info->title_len);
            std::string message(((char*)info->title_message) + info->title_len, info->message_len);
            ADK_LOG_RAW_PID(pid, 
                            tid,
                            level, 
                            code, 
                            module_name, 
                            function_name, 
                            src_line, 
                            title, 
                            message);
            break;
        }
        case ProxyInfoType::kInidcateInfo:
        {
            ProxyInfoInd* info = (ProxyInfoInd*)info_head;
            std::string ind_str(info->info_body, info->body_len);
            adk::Property ind_props(ind_str);

            std::string msg = ind_props.GetValue("Message", std::string());

            boost::property_tree::ptree proxy_pt;
            static std::stringstream ss;
            ss.clear();
            ss.str("");

            ss.str(msg);
            boost::property_tree::read_json(ss, proxy_pt);
            if (event_channel_ != nullptr)
            {
                event_channel_->PushIndicator(proxy_pt);  // 暂不考虑入队失败的场景
            }
            break;
        }
        case ProxyInfoType::kLogInfoTri:
        {
            if (log_file_name_tri_ != log_file_name_tri_current_)
            {
                log_file_tri_.open(log_file_name_tri_, std::ios_base::app | std::ios_base::out);
                if (!log_file_tri_.good())
                {
                    static adk::log::IntervalLogger interval_logger;
                    ADK_INV_LOG_WARN_AC_TF(interval_logger,
                                    "open tri file failed",
                                    "file <{1}>",
                                    log_file_name_tri_);
                    break;  // tri的日志文件无法新建不进行打印
                }
            }
            log_file_name_tri_current_ = log_file_name_tri_;

            ProxyInfoLog* info = (ProxyInfoLog*)info_head;

            pid_t pid = info->pid;
            pid_t tid = info->tid;

            adk::log::LogLevel level  = info->level;
            adk::log::LogCode code    = info->code;
            std::string module_name   = info->module_name;
            std::string function_name = info->function_name;
            uint32_t src_line         = info->src_line;

            std::string title(info->title_message, info->title_len);
            std::string message(((char*)info->title_message) + info->title_len, info->message_len);

            log_file_tri_ << "@ "
                      << boost::posix_time::ptime(boost::posix_time::microsec_clock::local_time()) << " "
                      << host_name_ << " "
                      << context_name_ << " "
                      << pid << " "
                      << tid << " "
                      << level << " " << g_log_level_map[level] << " "
                      << module_name << " "
                      << function_name << " "
                      << src_line << " "
                      << code << " "
                      << "| " << title << " "
                      << "| " << message << std::endl;
                      ;
            break;
        }

        default:
            static adk::log::IntervalLogger interval_logger;
            ADK_INV_LOG_WARN_AC_TF(interval_logger,
                                   "invalid info type",
                                   "<{1}>",
                                   info_head->info_type);
        }

        sccl_agent_->FreeEntry(entry);
    }
}

void ShardingAgent::ParserEnv()
{
    char* size_str = std::getenv("AAF_SHARD_SHM_DATA_SIZE");
    if (size_str != nullptr)
    {
        ADK_LOG_INFO_AC_TF("shm_data_size is changed", "from <{1}> to <{2}>", shm_data_size_, size_str);
        shm_data_size_ = boost::lexical_cast<uint64_t>(size_str);
    }

    char* latency_size_str = std::getenv("AAF_SHARD_LATENCY_DATA_SIZE");
    if (latency_size_str != nullptr)
    {
        ADK_LOG_INFO_AC_TF("latency_size is changed", "from <{1}> to <{2}>", latency_size_, latency_size_str);
        latency_size_ = boost::lexical_cast<uint64_t>(latency_size_str);
    }
}

int32_t ShardingAgent::InitAgent(aaf::GenericAmiApplication* app_instance,
                                ami::Property& ha_ctx_props,
                                ami::Property& sg_ctx_props)
{
    aaf_instance_ = app_instance;
    CopyFrom(app_instance);

    is_advance_follower_ = aaf_instance_->IsAdvanceFollower();

    context_name_      = GetApplicationName();
    domain_server_     = domain_server();
    share_memory_name_ = MakeShmContMemoryName(context_name_);
    sccl_name_         = MakeShmContChannelName(context_name_);

    host_name_ = boost::asio::ip::host_name();
    sharding_num_ = GetOptionArgument<int32_t>("sharding-num");

    if (is_advance_follower_)
    {
        sharding_num_ = std::max(sharding_num_, 1);
    }

    if (sharding_num_ <= 0 || sharding_num_ > kMaxShardingNum)
    {
        ADK_LOG_ERROR_AC_TF("invalid sharding-num",
                            "sharding-num <{1}>, max sharding count <{2}>",
                            sharding_num_,
                            kMaxShardingNum);

        return aaf::ErrorCode::kFailure;
    }

    void* se_lib_handle = dlopen("libsample_engine.so", RTLD_LAZY);
    if (nullptr != se_lib_handle)
    {
        *(void**)(&stop_save_point_) = dlsym(se_lib_handle, "SeStopSavePoint");
        if (stop_save_point_ == nullptr)
        {
            dlclose(se_lib_handle);
            ADK_LOG_INFO_AC_TF("StopSavePoint cannot be found in libsample_engine.so", 
                                "error: {1}", 
                                std::strerror(errno));
        }
        else
        {
            ADK_LOG_INFO_AC_TF("StopSavePoint is found in libsample_engine.so", "");
        }
    }
    else
    {
        ADK_LOG_INFO_AC_TF("dlopen libsample_engine.so failed", 
                            "error: {1}", 
                            std::strerror(errno));
    }

    ha_ctx_props_ = ha_ctx_props;
    sg_ctx_props_ = sg_ctx_props;

    is_calc_msg_latency_ = ha_ctx_props_.GetValue(ami::config::context::kIsCountQueueLatency, false);

    memset(ha_latency_, 0, sizeof(ha_latency_));
    memset(sg_latency_, 0, sizeof(sg_latency_));

    if (is_calc_msg_latency_)
    {
        ha_latency_[AgentLAId::kDoRoute] =
            ha_la_metric_.AddLatencySpan("RouteSpan", latency_size_);
        ha_latency_[AgentLAId::kMessageSort] =
            ha_la_metric_.AddLatencySpan("MessageSort", latency_size_);

        if (aaf_instance_->GetSingletonContext())
        {
            sg_latency_[AgentLAId::kDoRoute] =
                sg_la_metric_.AddLatencySpan("RouteSpan", latency_size_);
            sg_latency_[AgentLAId::kMessageSort] =
                sg_la_metric_.AddLatencySpan("MessageSort", latency_size_);
        }
    }

    auto ec = CreateShmContMem(share_memory_name_);
    if (ec != aaf::ErrorCode::kSuccess)
    {
        return ec;
    }

    void* log_name_ptr = mmap(NULL, kLogNameLenMax * 4 * 2, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (log_name_ptr == MAP_FAILED)
    {
        ADK_LOG_ERROR_AC_TF("mmap failed",
                            "errno: {1}, str errno: {2}, len: <{3}>",
                            errno,
                            std::strerror(errno),
                            kLogNameLenMax);
        return aaf::ErrorCode::kFailure;
    }

    log_rotate_check_time_ = adk::timespec_now();
    current_date_ = GetFormatLocalTime(log_date_format_);

    log_name_tri_shm_ = (char*)log_name_ptr;
    log_file_name_tri_ = (boost::format(log_file_format_tri_) 
                      % GetLogDir() 
                      % context_name_
                      % current_date_
                      ).str();
    strcpy(log_name_tri_shm_, log_file_name_tri_.c_str());

    log_name_shm_ = (((char*)log_name_ptr) + (kLogNameLenMax * 4));
    log_file_name_ = (boost::format(log_file_format_)
                        % GetLogDir() 
                        % context_name_
                        % current_date_
                        ).str();
    strcpy(log_name_shm_, log_file_name_.c_str());

    for (int32_t i = 1; i <= sharding_num_; ++i)
    {
        // [0] not use
        auto* sharding_ctx = new ShardingCtx();
        sharding_ctx->sharding_index = i;

        auto ec = AllocShmContMem(sharding_ctx);
        if (ec != aaf::ErrorCode::kSuccess)
        {
            return ec;
        }

        // each futex_wait alloc 2 * ADK_CACHE_LINE_SIZE
        uint32_t map_size = ADK_ROUND_UP(ADK_CACHE_LINE_SIZE * 4, ADK_PAGE_SIZE);

        void* ptr = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

        if (ptr == MAP_FAILED)
        {
            ADK_LOG_ERROR_AC_TF("mmap failed",
                                "errno: {1}, str errno: {2}, sharding_index: <{3}>",
                                errno,
                                std::strerror(errno),
                                i);
            return aaf::ErrorCode::kFailure;
        }
        sharding_ctx->ha_ctx_data.futex_wait = (int*)ptr;
        sharding_ctx->sgt_ctx_data.futex_wait = (int*)((int64_t)ptr + ADK_CACHE_LINE_SIZE * 2);
        *sharding_ctx->ha_ctx_data.futex_wait = 0;
        *sharding_ctx->sgt_ctx_data.futex_wait = 0;

        sharding_ctx_vec_[i] = sharding_ctx;
    }

    const uint32_t total_size = kFlrLauncheSize * sharding_num_;
    std::string launch_info_shm_name = MakeFlrLaunchShmName(context_name_);

    adk::ShmFactory::Destroy(launch_info_shm_name);

    void* flr_addr = adk::ShmFactory::Create(launch_info_shm_name, total_size);
    if (nullptr == flr_addr)
    {
        ADK_LOG_ERROR_AC_TF("create FlrProxyLaunchShm failed",
                            "launch info share name <{1}>, errno <{2}>",
                            launch_info_shm_name,
                            errno);
        return aaf::ErrorCode::kFailure;
    }

    // Init share memory for application data
    shm_data_manager_ = new ShmDataMgr(shm_data_size_);

    shm_data_manager_->InitShmData(context_name_, is_advance_follower_);

    if (aaf::g_sharding_channel)
    {
        aaf::g_sharding_channel->agent_ = this;
    }

    auto* shm_mgr = aaf::ShmDataManager::GetInstance();
    shm_mgr->sharding_agent_ = this;
    shm_mgr->s_shm_segment_ = shm_data_manager_->app_data_shm();

    sort_queue_ = adk::variant::SPSCQueue<SortElem>::Create("sort_queue", 65536);
    assert(sort_queue_);

    // ~/log/$APP.core_sqn
    core_file_path_ = aaf_instance_->GetLogDir() + "/" + context_name_ + ".core_sqn";

    core_sqn_file_.open(core_file_path_, std::ios_base::trunc | std::ios_base::out);
    if (!core_sqn_file_.is_open())
    {
        ADK_LOG_ERROR_AC_TF("open core file failed", "file path: {1}", core_file_path_);
        return aaf::ErrorCode::kFailure;
    }

    ADK_LOG_INFO_AC_TF("generate core file path", "file path: {1}", core_file_path_);

    ADK_LOG_INFO_AC_TF("ShardingAgent Init success",
                       "sharding-index <{1}>",
                       sharding_index_);

    return aaf::ErrorCode::kSuccess;
}

int32_t ShardingAgent::StartProxy()
{
    ADK_LOG_INFO_AC_TF("begin create ShardingProxy",
                       "sharding number <{1}>",
                       sharding_num_);

    for (int32_t i = 1; i <= sharding_num_; ++i)
    {
        // [0] not use
        auto* sharding_ctx = sharding_ctx_vec_[i];

        auto ec = StartShardingProxy(sharding_ctx);
        //
        if (ec != aaf::ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("create proxy failed",
                                "sharding-index <{1}>",
                                sharding_ctx->sharding_index);
            return ec;
        }

        if (sharding_index_ != 0)
        {
            // this is sharding proxy process
            break;
        }
    }

    if (sharding_index_ != 0)
    {
        ADK_LOG_INFO_AC_TF("ShardingProxy create success",
                           "sharding-index <{1}>",
                           sharding_index_);
        proxy_ = new ShardingProxy();
        if (aaf::g_sharding_channel)
        {
            aaf::g_sharding_channel->proxy_ = proxy_;
        }

        auto ec = proxy_->Init(this, sharding_index_);
        if (ec != aaf::ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("proxy init failed", "sharding-index <{1}>", sharding_index_);
        }
        else
        {

            proxy_->OnRun();
        }

        ADK_LOG_INFO_AC_TF("proxy goto exit", "sharding-index <{1}>", sharding_index_);
        _exit(0);
    }
    
    return aaf::ErrorCode::kSuccess;
}

int32_t ShardingAgent::CreateShmContMem(const std::string& share_memory_name)
{
    uint32_t cont_mem_num = kShardingContMemCount * sharding_num_;
    uint32_t total_memory_size = sharding::kShardingMemorySize * sharding_num_;


    ADK_LOG_INFO_AC_TF("create sharding ShmContMemory",
                        "share memory name <{1}>, sharding count <{2}>, "
                        "ContMemory num <{3}>, total-size <{4}>",
                        share_memory_name_,
                        sharding_num_,
                        cont_mem_num,
                        total_memory_size);

    adk::ShmContMemManager::Destroy(share_memory_name);
    memory_manager_ = adk::ShmContMemManager::Create(share_memory_name,
                                                     cont_mem_num,
                                                     total_memory_size);

    if (nullptr == memory_manager_)
    {
        ADK_LOG_ERROR_AC_TF("create share memory failed",
                            "share memory name <{1}>",
                            share_memory_name);
        return aaf::ErrorCode::kFailure;
    }

    return aaf::ErrorCode::kSuccess;
}

int32_t ShardingAgent::AllocShmContMem(ShardingCtx* sharding_ctx)
{
    const std::string index = std::to_string(sharding_ctx->sharding_index);

    auto* rx_cont_shm = memory_manager_->CreateShmContMemory(
        "rx_sharding_" + index,
        kEachContMemorySize,
        kEachContReserveSize);
    if (nullptr == rx_cont_shm)
    {
        ADK_LOG_ERROR_AC_TF("create rx continue memory failed",
                            "share memory sharding_index <{1}>",
                            sharding_ctx->sharding_index);
        return aaf::ErrorCode::kFailure;
    }
    sharding_ctx->ha_ctx_data.rx_cont_shm = rx_cont_shm;

    if (is_advance_follower_)
    {
        rx_cont_shm = memory_manager_->CreateShmContMemory(
            "trial_rx_sharding_" + index,
            kEachContMemorySize,
            kEachContReserveSize);
    }
    else
    {
        rx_cont_shm = memory_manager_->CreateShmContMemory(
            "sg_rx_sharding_" + index,
            kEachContMemorySize,
            kEachContReserveSize);
    }

    if (nullptr == rx_cont_shm)
    {
        ADK_LOG_ERROR_AC_TF("create rx continue memory failed",
                            "share memory sharding_index <{1}>",
                            sharding_ctx->sharding_index);
        return aaf::ErrorCode::kFailure;
    }
    sharding_ctx->sgt_ctx_data.rx_cont_shm = rx_cont_shm;

    auto* tx_cont_shm = memory_manager_->CreateShmContMemory(
        "tx_sharding_" + index,
        kEachContMemorySize,
        kEachContReserveSize);
    if (nullptr == tx_cont_shm)
    {
        ADK_LOG_ERROR_AC_TF("create tx continue memory failed",
                            "share memory sharding_index <{1}>",
                            sharding_ctx->sharding_index);
        return aaf::ErrorCode::kFailure;
    }
    sharding_ctx->ha_ctx_data.tx_cont_shm = tx_cont_shm;

    if (is_advance_follower_)
    {
        sharding_ctx->sgt_ctx_data.tx_cont_shm = nullptr;
    }
    else
    {
        tx_cont_shm = memory_manager_->CreateShmContMemory(
            "sg_tx_sharding_" + index,
            kEachContMemorySize,
            kEachContReserveSize);
        if (nullptr == tx_cont_shm)
        {
            ADK_LOG_ERROR_AC_TF("create tx continue memory failed",
                                "share memory sharding_index <{1}>",
                                sharding_ctx->sharding_index);
            return aaf::ErrorCode::kFailure;
        }
        sharding_ctx->sgt_ctx_data.tx_cont_shm = tx_cont_shm;
    }

    ADK_LOG_INFO_AC_TF("create ShmContMemory success",
                       "sharding-index <{1}>",
                       sharding_ctx->sharding_index);

    return aaf::ErrorCode::kSuccess;
}

aaf::ShardingSeqLock* ShardingAgent::CreateSeqLock(const std::string& name, uint32_t cache_size)
{
    aaf::ShardingSeqLock* sharding_seq_lock = new aaf::ShardingSeqLock();
    size_t total_size = ShmSeqLock::CalcMemSize(cache_size);

    auto* seq_lock_shm = shm_data_manager_->seq_lock_shm();

    void* addr = seq_lock_shm->allocate(total_size, std::nothrow);
    assert(addr);

    ShmSeqLock* shm_seq_lock = ShmSeqLock::Create(name, cache_size, addr);
    sharding_seq_lock->shm_seq_lock_ = shm_seq_lock;
    sharding_seq_lock->sharding_ctx_ = nullptr;
    ADK_LOG_INFO_AC_TF("create ShmSeqLock success", "SeqLock name <{1}>", name);
    sharding_lock_map_.insert({name, sharding_seq_lock});


    ADK_LOG_INFO_AC_TF("shm data address",
                        "seq_lock_shm <{1}>, app_data_shm <{2}>",
                        (void*)shm_data_manager_->seq_lock_shm()->get_address(),
                        (void*)shm_data_manager_->app_data_shm()->get_address());

    return sharding_seq_lock;
}

void ShardingAgent::ShmDataFlush()
{
    for (auto iter : sharding_lock_map_)
    {
        iter.second->sharding_ctx_ = GetProxy()->sharding_ctx();
    }

    if (!is_advance_follower_)
    {
        return;
    }

}

int32_t ShardingAgent::StartShardingProxy(ShardingCtx* sharding_ctx)
{
    if ((pipe2(sharding_ctx->proxy_to_agent_fds, O_NONBLOCK) != 0)
        || (pipe2(sharding_ctx->agent_to_proxy_fds, O_NONBLOCK) != 0))
    {
        close(sharding_ctx->proxy_to_agent_fds[0]);
        close(sharding_ctx->proxy_to_agent_fds[1]);
        close(sharding_ctx->agent_to_proxy_fds[0]);
        close(sharding_ctx->agent_to_proxy_fds[1]);
        ADK_LOG_ERROR_AC_TF("pipe2 failed",
                            "errno = <{2}>, desc <{3}>",
                            errno,
                            strerror(errno));
        return aaf::ErrorCode::kFailure;
    }

    pid_t child = fork();
    if (child < 0)
    {
        ADK_LOG_ERROR_AC_TF("fork failed", "errno: <{1}>", errno);
        return aaf::ErrorCode::kFailure;
    }
    if (child == 0)
    {
        CloseOtherPipes(sharding_ctx->sharding_index);
        // child process
        close(sharding_ctx->proxy_to_agent_fds[0]);  // close the read end
        close(sharding_ctx->agent_to_proxy_fds[1]);  // close the write end
        sharding_ctx->proxy_to_agent_fds[0] = 0;
        sharding_ctx->agent_to_proxy_fds[1] = 0;

        ADK_LOG_FORK();
        size_t cfg_size     = 0;
        size_t cfg_buf_len_ = 81920;
        char* cfg_buf_      = (char*)malloc(81920);

        // read property
        do
        {
            ssize_t cfg_read_size = read(sharding_ctx->agent_to_proxy_fds[0],
                                            cfg_buf_ + cfg_size,
                                            cfg_buf_len_ - cfg_size);

            if (cfg_read_size > 0)
            {
                cfg_size += cfg_read_size;

                if (cfg_size >= cfg_buf_len_)
                {
                    cfg_buf_len_ = cfg_buf_len_ << 1;
                    cfg_buf_     = (char*)realloc(cfg_buf_, cfg_buf_len_);  // FIXME: check error
                }
                continue;
            }

            if (cfg_read_size == 0                                                    // closed
                || (cfg_read_size < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)))  // error
            {
                ADK_LOG_ERROR_AC_TF("read props from agent pipe failed",
                                    "errno = <{1}>, desc <{2}>",
                                    errno,
                                    strerror(errno));
                return aaf::ErrorCode::kFailure;
            }
            else
            {
                if (cfg_size != 0 && cfg_buf_[cfg_size - 1] == kComEnd)
                {
                    break;
                }
                static adk::log::IntervalLogger interval_logger;
                ADK_INV_LOG_INFO_AC_TF(interval_logger,
                                        "waitting sharding agent",
                                        "reading from agent pipe");
                usleep(1000);
            }

        } while (is_running_);

        std::string prop_str(cfg_buf_, cfg_size - 1);
        ep_info_props_ = ami::Property(prop_str);
        free(cfg_buf_);

        ADK_LOG_DEBUG_AC_TF("read props from agent",
                            "sharding <{1}>, props: {2}",
                            sharding_ctx->sharding_index,
                            ep_info_props_.Dump());
        // start sharding proxy
        // construct the agent command line parameters

        sharding_index_ = sharding_ctx->sharding_index;
        return aaf::ErrorCode::kSuccess;
    }
    // else  parant wait child execl finish
    sharding_ctx->sharding_pid_ = child;

    close(sharding_ctx->proxy_to_agent_fds[1]);  // close the write end
    close(sharding_ctx->agent_to_proxy_fds[0]);  // close the read end
    sharding_ctx->proxy_to_agent_fds[1] = 0;
    sharding_ctx->agent_to_proxy_fds[0] = 0;

    return aaf::ErrorCode::kSuccess;
}

bool ShardingAgent::OnCollectIndicator(bool is_singleton, boost::property_tree::ptree& indicator)
{

    CtxInd& ctx_ind = ctx_ind_[is_singleton];

    indicator.put("nr_deliver_msgs", ctx_ind.nr_message_received);
    indicator.put("nr_route_failed_msgs", ctx_ind.nr_route_failed);
    indicator.put("nr_tx_msgs", ctx_ind.nr_tx_message_);

    if (!is_singleton)
    {
        indicator.put("nr_events", event_nr_);
        indicator.put("sort_queue_len", sort_queue_->length());
        indicator.put("sort_queue_max_qlen", ACCESS_ONCE(sort_queue_max_qlen_));
        sort_queue_max_qlen_ = 0;
    }

    boost_ptree& sharding_shm_indi_ptree = indicator.add_child("sharding_ctx", boost_ptree());
    uint64_t total_tx_qlen = 0;
    uint64_t total_rx_qlen = 0;
    for (int32_t i = 1; i <= sharding_num_; ++i)
    {
        auto* sharding_ctx = sharding_ctx_vec_[i];
        auto& ctx_data = sharding_ctx->GetCtxData(is_singleton);

        boost_ptree& shm_indi_ptree = sharding_shm_indi_ptree.push_back(
                boost_ptree::value_type("", boost_ptree()))->second;

        shm_indi_ptree.put("pid", sharding_ctx->sharding_pid_);
        shm_indi_ptree.put("sharding_index", i);
        if (ctx_data.tx_cont_shm != nullptr)
        {
            boost_ptree& tx_ptree = shm_indi_ptree.add_child("tx_cont_memory", boost_ptree());
            total_tx_qlen += ctx_data.tx_cont_shm->GetCurrQlen();
            ctx_data.tx_cont_shm->CollectIndicator(tx_ptree);
        }
        if (ctx_data.rx_cont_shm != nullptr)
        {
            boost_ptree& rx_ptree = shm_indi_ptree.add_child("rx_cont_memory", boost_ptree());
            total_rx_qlen += ctx_data.rx_cont_shm->GetCurrQlen();
            ctx_data.rx_cont_shm->CollectIndicator(rx_ptree);
        }
        if (is_advance_follower_ && (sharding_ctx->sgt_ctx_data.rx_cont_shm != nullptr))
        {
            boost_ptree& trial_rx_ptree = shm_indi_ptree.add_child("trial_rx_cont_memory", boost_ptree());
            sharding_ctx->sgt_ctx_data.rx_cont_shm->CollectIndicator(trial_rx_ptree);
        }
    }

    indicator.put("total_tx_qlen", total_tx_qlen);
    indicator.put("total_rx_qlen", total_rx_qlen);

    if (is_calc_msg_latency_)
    {
        if (is_singleton)
        {
            sg_la_metric_.CollectIndicator(indicator, "agent_sg_latency");
        }
        else
        {
            ha_la_metric_.CollectIndicator(indicator, "agent_ha_latency");
        }
    }

    return true;
}

int32_t ShardingAgent::OnAmiInitBegin()
{
    ADK_LOG_INFO_AC_TF("recv callback", "");
    if (is_advance_follower_)
    {
        shm_data_manager_->OverWriteTrialShmData();
    }

    sccl_handle_ = new SideChannelHandler();
    sccl_agent_ = adk::sccl::Agent::Create(sccl_name_, sccl_handle_, false);
    if (sccl_agent_ == nullptr)
    {
        ADK_LOG_ERROR_AC_TF("create shm_cont_channel failed, please check /dev/shm/", 
                            "name: {1}", 
                            sccl_name_);
        return aaf::ErrorCode::kFailure;
    }

    proxy_log_thread_ = adk::boost_thread("log-agent-thrd", 
                                          "Sharding Proxy log thread",
                                          boost::bind(&ShardingAgent::ProxyLogAgent, this));

    return aaf::ErrorCode::kSuccess;
}

int32_t ShardingAgent::GetMasterEndpointList(ami::Context* ami_context,
                                            bool is_ha,
                                            std::vector<ami::Property>& ep_infos)
{
    std::vector<std::string> master_tx_ep_names;
    ami_context->PropertyAt(ami::config::context::kMasterTxEndpoints).GetValue(master_tx_ep_names);
    for (const auto& tx_ep_name_item : master_tx_ep_names)
    {
        std::vector<int32_t> partitions;
        auto ec = ami_context->PropertyAt(ami::config::context::kMasterTxEndpoint, tx_ep_name_item)
                            (ami::config::endpoint::kPartitions)
                            .GetValue(partitions);
        if (ec != ami::ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("get kMasterTxEndpoint partitions failed",
                                "endpoint name: {1}",
                                tx_ep_name_item);
            return ec;
        }

        int32_t ep_id;
        ec = ami_context->PropertyAt(ami::config::context::kMasterTxEndpoint, tx_ep_name_item)
                            (ami::config::endpoint::kId)
                            .GetValue(ep_id);
        if (ec != ami::ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("get kMasterTxEndpoint id info list failed",
                                "endpoint name: {1}",
                                tx_ep_name_item);
            return ec;
        }

        ami::Property master_tx_ep_info;
        master_tx_ep_info.SetValues()
                        (ami::config::endpoint::kEndpointName, tx_ep_name_item)
                        (ami::config::context::kIsMasterContext, true)
                        (ami::config::context::kIsSingleton, !is_ha)
                        (ami::config::endpoint::kEndpointId, ep_id)
                        (ami::config::endpoint::kPartitions, partitions)
                        (ami::config::endpoint::kIsTxEndpoint, true)
                        ;
        ep_infos.push_back(master_tx_ep_info);
    }

    std::vector<std::string> master_rx_ep_names;
    ami_context->PropertyAt(ami::config::context::kMasterRxEndpoints).GetValue(master_rx_ep_names);
    for (const auto& rx_ep_name_item : master_rx_ep_names)
    {
        std::vector<int32_t> partitions;
        auto ec = ami_context->PropertyAt(ami::config::context::kMasterRxEndpoint, rx_ep_name_item)
                            (ami::config::endpoint::kPartitions)
                            .GetValue(partitions);
        if (ec != ami::ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("get kMasterRxEndpoint partitions failed",
                                "endpoint name: {1}",
                                rx_ep_name_item);
            return ec;
        }

        int32_t ep_id;
        ec = ami_context->PropertyAt(ami::config::context::kMasterRxEndpoint, rx_ep_name_item)
                            (ami::config::endpoint::kId)
                            .GetValue(ep_id);
        if (ec != ami::ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("get kMasterRxEndpoint id failed",
                                "endpoint name: {1}",
                                rx_ep_name_item);
            return ec;
        }

        ami::Property master_rx_ep_info;
        master_rx_ep_info.SetValues()
                        (ami::config::endpoint::kEndpointName, rx_ep_name_item)
                        (ami::config::context::kIsMasterContext, true)
                        (ami::config::context::kIsSingleton, !is_ha)
                        (ami::config::endpoint::kEndpointId, ep_id)
                        (ami::config::endpoint::kPartitions, partitions)
                        (ami::config::endpoint::kIsTxEndpoint, false)
                        ;
        ep_infos.push_back(master_rx_ep_info);
    }
    return aaf::ErrorCode::kSuccess;
}

int32_t ShardingAgent::OnTxEndpointCreationBegin()
{
    ami_context_ = GetContext();
    ami_context_sgt_ = GetSingletonContext();

    std::vector<ami::Property> ep_infos;
    if (ami_context_ != nullptr)
    {
        std::vector<ami::Property> ha_ep_infos;
        auto ec = ami_context_->PropertyAt(ami::config::context::kEndpointInfoList)
                    .GetValue(ha_ep_infos);
        if (ec != ami::ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("get transport info list failed", "");
            return ec;
        }
        for (auto& ha_ep_info_item : ha_ep_infos)
        {
            ha_ep_info_item.SetValue(ami::config::context::kIsSingleton, false);
            ep_infos.push_back(ha_ep_info_item);
        }
        // ep_infos.insert(ep_infos.end(), ha_ep_infos.begin(), ha_ep_infos.end());

        if (ha_ctx_props_.HasValue(ami::config::context::kMasterContext))
        {
            // this condition is same as AMI ContextImpl
            if (!aaf::is_disaster_backup_inter(kHaCtx)
                || (aaf::is_disaster_backup_inter(kHaCtx)
                    && aaf::is_disaster_context_inter(kHaCtx)))
            {
                // slave context or disaster slave context
                if (GetMasterEndpointList(ami_context_, kHaCtx, ep_infos)
                    != aaf::ErrorCode::kSuccess)
                {
                    return aaf::ErrorCode::kFailure;
                }
            }
        }

        adk::MonitorOps agent_ops;
        agent_ops.on_collection_indicator = boost::bind(&ShardingAgent::OnCollectIndicator, this, false, _1);
        agent_ops.is_collection_indicator = true;

        event_channel_ = adk::Monitor::RegisterObject("ShardingAgent", context_name_, &agent_ops);
    }

    if (ami_context_sgt_ != nullptr)
    {
        std::vector<ami::Property> sg_ep_infos;
        auto ec = ami_context_sgt_->PropertyAt(ami::config::context::kEndpointInfoList)
                    .GetValue(sg_ep_infos);
        assert(ec == ami::ErrorCode::kSuccess);
        if (ec != ami::ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("get transport info list failed", "");
            return ec;
        }
        for (auto& sg_ep_info_item : sg_ep_infos)
        {
            sg_ep_info_item.SetValue(ami::config::context::kIsSingleton, true);
            ep_infos.push_back(sg_ep_info_item);
        }

        if (sg_ctx_props_.HasValue(ami::config::context::kMasterContext))
        {
            // this condition is same as AMI ContextImpl
            if (!aaf::is_disaster_backup_inter(kSgtCtx)
                || (aaf::is_disaster_backup_inter(kSgtCtx)
                    && aaf::is_disaster_context_inter(kSgtCtx)))
            {
                // slave context or disaster slave context
                if (GetMasterEndpointList(ami_context_sgt_, kSgtCtx, ep_infos)
                    != aaf::ErrorCode::kSuccess)
                {
                    return aaf::ErrorCode::kFailure;
                }
            }
        }

        adk::MonitorOps agent_ops;
        agent_ops.on_collection_indicator = boost::bind(&ShardingAgent::OnCollectIndicator, this, true, _1);
        agent_ops.is_collection_indicator = true;

        auto event_channel_tmp = adk::Monitor::RegisterObject("ShardingAgent", context_name_ + "_Singleton", &agent_ops);
        if (event_channel_ == nullptr)
        {
            event_channel_ = event_channel_tmp;
        }
    }

    ami::Property tp_info_props;
    tp_info_props.SetValue(ami::config::context::kEndpointInfoList, ep_infos);
    std::string prop_str = tp_info_props.Dump();
    prop_str.append(1, kComEnd);       // end flag
    const char* buff = prop_str.c_str();
    
    auto ec = PipeWrite(buff, prop_str.size());
    if (ec != aaf::ErrorCode::kSuccess)
    {
        return ec;
    }

    ADK_LOG_INFO_AC_TF("write props to proxy success",
                        "props: {1}",
                        tp_info_props.Dump());

    uint32_t max_endpoint_id = 0;
    for (auto& ep_info : ep_infos)
    {
        assert(ep_info.HasValue(ami::config::context::kIsTxEndpoint) == true);

        uint32_t endpoint_id = ep_info.GetValue(ami::config::context::kEndpointId, 0);
        bool is_tx_ep = ep_info.GetValue(ami::config::context::kIsTxEndpoint, false);
        std::string endpoint_name = ep_info.GetValue(ami::config::context::kEndpointName, "");

        ADK_LOG_INFO_AC_TF("EndpointInfoList",
                            "endpoint info : <{1}>",
                            ep_info.Dump());

        if (is_tx_ep)
        {
            tx_ep_map_[endpoint_name] = endpoint_id;
            max_endpoint_id = std::max(max_endpoint_id, endpoint_id);
        }
    }
    txeps_vec_.resize(max_endpoint_id + 8, nullptr);

    return aaf::ErrorCode::kSuccess;
}

int32_t ShardingAgent::OnTxEndpointCreation(aaf::EndpointHandler* ep_hdl, const std::string& ep_name)
{
    ADK_LOG_INFO_AC_TF("recv callback", "endpoint <{1}>", ep_name);
    auto iter = tx_ep_map_.find(ep_name);
    if (iter == tx_ep_map_.end())
    {
        ADK_LOG_ERROR_AC_TF("get ep hdl failed", "ep_name: {1}", ep_name);
        return aaf::ErrorCode::kFailure;
    }

    uint32_t endpoint_id = iter->second;
    assert(txeps_vec_.size() > (size_t)endpoint_id);

    txeps_vec_[endpoint_id] = ep_hdl;

    return aaf::ErrorCode::kSuccess;
}


void ShardingAgent::DeliverFlrMessage(ami::Message* msg, ShardingCtx* sharding_ctx, int64_t ts_begin)
{
    ShardingCtx::CtxData& ctx_data = sharding_ctx->GetCtxData(false);
    ShardingCtx::CtxData& trial_ctx_data = sharding_ctx->GetCtxData(true);

    adk::ContinueMemory* const rx_cont_memory = ctx_data.rx_cont_shm;
    adk::ContinueMemory* const rx_trial_cont_memory = trial_ctx_data.rx_cont_shm;

    assert(rx_cont_memory);
    assert(rx_trial_cont_memory);

    // 高可用Context 按照消息递交的全局序重排序
    SortElem elem;
    elem.total_sqn = msg->get_total_order_seq_num();
    elem.sharding_index = sharding_ctx->sharding_index;

    do
    {
        auto ec = sort_queue_->Push(elem);

        sort_queue_max_qlen_ = std::max(sort_queue_max_qlen_, sort_queue_->length());
        if (ec == adk::ErrorCode::kSuccess)
        {
            break;
        }

        if (!is_running())
        {
            return;
        }
    } while (true);

    int64_t ts_now = 0;
    if (is_calc_msg_latency_)
    {
        ts_now = adk::timespec_now();
        ha_latency_[AgentLAId::kDoRoute]->Save(ts_begin, ts_now);
    }

    *(trial_ctx_data.futex_wait) = static_cast<int>(rx_trial_cont_memory->GetProduceNR() + 1);
    // 提前唤醒试算进程，放这里比放后面时延更加稳定
    adk::FutexWake(trial_ctx_data.futex_wait);

    adk::ContEntry* entry_ptr = nullptr;
    adk::ContEntry* entry_ptr2 = nullptr;
    const uint32_t memory_len = sizeof(struct ShmRxMessage) + msg->size();

    {
        std::lock_guard<boost::detail::spinlock> lock(rx_spinlock_);

        // push to trial process
        if (ADK_UNLIKELY(adk::ErrorCode::kSuccess
                         != rx_trial_cont_memory->AllocEntry(memory_len, &entry_ptr)))
        {
            return;
        }
        struct ShmRxMessage* rx_message = (struct ShmRxMessage*)(entry_ptr->GetBuffer());
        MessageConvert(msg, rx_message);
        rx_message->send_ts = ts_now;
        rx_trial_cont_memory->PostEntry(entry_ptr);

        // push to realtime process
        if (ADK_UNLIKELY(adk::ErrorCode::kSuccess
                         != rx_cont_memory->AllocEntry(memory_len, &entry_ptr2)))
        {
            return;
        }

        struct ShmRxMessage* rx_message2 = (struct ShmRxMessage*)(entry_ptr2->GetBuffer());
        MessageConvert(msg, rx_message2);
        rx_message2->send_ts = ts_now;
        rx_cont_memory->PostEntry(entry_ptr2);
    }
    *(ctx_data.futex_wait) = static_cast<int>(rx_cont_memory->GetProduceNR());
    // 晚唤醒实时进程，实时进程需要等待试算进程处理完成后才能处理
    adk::FutexWake(ctx_data.futex_wait);
}

template<bool IsSingleton>
void ShardingAgent::DeliverMessage(ami::Message* msg, ShardingCtx* sharding_ctx, int64_t ts_begin)
{
    boost::detail::spinlock* spinlock;

    ShardingCtx::CtxData& ctx_data = sharding_ctx->GetCtxData(IsSingleton);
    adk::ContinueMemory* rx_cont_memory = ctx_data.rx_cont_shm;
    int* proxy_wait = ctx_data.futex_wait;
    assert(rx_cont_memory);
    assert(proxy_wait != nullptr);

    adk::LatencyStatistics** latency = nullptr;
    if (IsSingleton)
    {
        spinlock = &rx_sgt_spinlock_;
        latency = sg_latency_;
    }
    else
    {
        spinlock = &rx_spinlock_;
        latency = ha_latency_;

        // 高可用Context 按照消息递交的全局序重排序
        SortElem elem;
        elem.total_sqn      = msg->get_total_order_seq_num();
        elem.sharding_index = sharding_ctx->sharding_index;

        do
        {
            auto ec = sort_queue_->Push(elem);
            sort_queue_max_qlen_ = std::max(sort_queue_max_qlen_, sort_queue_->length());
            if (ec == adk::ErrorCode::kSuccess)
            {
                break;
            }

            if (!is_running())
            {
                return;
            }
        } while (true);
    }

    int64_t ts_now = 0;
    if (is_calc_msg_latency_)
    {
        ts_now = adk::timespec_now();
        latency[AgentLAId::kDoRoute]->Save(ts_begin, ts_now);
    }

    *proxy_wait = static_cast<int>(rx_cont_memory->GetProduceNR() + 1);
    adk::FutexWake(proxy_wait);  // 提前唤醒子进程，AllocEntry一定会保证申请成功，放这里比放后面时延更加稳定
    adk::ContEntry* entry_ptr = nullptr;
    const uint32_t memory_len = sizeof(struct ShmRxMessage) + msg->size();

    std::lock_guard<boost::detail::spinlock> lock((*spinlock));
    // push to trial process
    if (ADK_UNLIKELY(adk::ErrorCode::kSuccess
        != rx_cont_memory->AllocEntry(memory_len, &entry_ptr)))
    {
        return;
    }

    struct ShmRxMessage* rx_message = (struct ShmRxMessage*)(entry_ptr->GetBuffer());
    MessageConvert(msg, rx_message);
    rx_message->send_ts = ts_now;
    rx_cont_memory->PostEntry(entry_ptr);
}

int32_t ShardingAgent::ShardingPost(const void* data, uint32_t size, int32_t sharding_index)
{
    if (sharding_index > sharding_num_)
    {
        return aaf::ErrorCode::kFailure;
    }

    ShardingCtx::CtxData& ctx_data = sharding_ctx_vec_[sharding_index]->GetCtxData(false);
    adk::ContinueMemory* rx_cont_memory = ctx_data.rx_cont_shm;
    int* proxy_wait = ctx_data.futex_wait;
    assert(rx_cont_memory);
    assert(proxy_wait != nullptr);

    adk::ContEntry* entry_ptr = nullptr;
    adk::ContEntry* entry_ptr2 = nullptr;
    const uint32_t memory_len = sizeof(struct ShmAgentHeader) + size;
    std::lock_guard<boost::detail::spinlock> lock(rx_spinlock_);
    // push to trial process
    if (ADK_UNLIKELY(adk::ErrorCode::kSuccess
        != rx_cont_memory->AllocEntry(memory_len, &entry_ptr)))
    {
        return aaf::ErrorCode::kFailure;
    }
    struct ShmAgentHeader* header = (struct ShmAgentHeader*)(entry_ptr->GetBuffer());

    header->msg_type = ShmMsgType::kShardingPost;
    header->msg_len  = memory_len;
    memcpy((char*)(header + 1), data, size);

    rx_cont_memory->PostEntry(entry_ptr);

    if (is_advance_follower_)
    {
        auto& trial_ctx_data = sharding_ctx_vec_[sharding_index]->GetCtxData(true);
        if (ADK_UNLIKELY(adk::ErrorCode::kSuccess
                         != trial_ctx_data.rx_cont_shm->AllocEntry(memory_len, &entry_ptr2)))
        {
            // on exitting
            return aaf::ErrorCode::kFailure;
        }
        struct ShmAgentHeader* header = (struct ShmAgentHeader*)(entry_ptr2->GetBuffer());

        header->msg_type = ShmMsgType::kShardingPost;
        header->msg_len  = memory_len;
        memcpy((char*)(header + 1), data, size);
        trial_ctx_data.rx_cont_shm->PostEntry(entry_ptr2);
        
        (*trial_ctx_data.futex_wait) = trial_ctx_data.rx_cont_shm->GetProduceNR();
        adk::FutexWake(trial_ctx_data.futex_wait);
    }
    return aaf::ErrorCode::kSuccess;
}

void ShardingAgent::OnShmTxMessage(ami::Context* context, struct ShmTxMessage* tx_message)
{
    assert(context);
    assert(tx_message->header.msg_len > (int32_t)sizeof(struct ShmTxMessage));
    aaf::EndpointHandler* txep = txeps_vec_[tx_message->endpoint_id];
    if (ADK_UNLIKELY(nullptr == txep))
    {
        ADK_LOG_ERROR_AC_TF("invalid endpoint id",
                            "<{1}>",
                            tx_message->endpoint_id);
        return;
    }

    const auto message_len = tx_message->header.msg_len - sizeof(struct ShmTxMessage);
    char* const message_body = tx_message->msg_body;

    ami::Message* msg = context->NewMessage(message_len);
    memcpy(msg->data(), message_body, message_len);
    msg->set_size(message_len);
    msg->msg_header.ancestor_id = tx_message->trace_record;  // 等效msg->set_trace_record(xxx);

    int32_t ec;
    if (tx_message->partition_no > 0)
    {
        ec = txep->SendMsg(msg, tx_message->partition_no);
    }
    else
    {
        ec = txep->SendMsg(msg);
    }

    if (ADK_UNLIKELY(ami::ErrorCode::kSuccess != ec))
    {
        ADK_LOG_ERROR_AC_TF("send message failed",
                            "endpoint id <{1}> partition<{2}>",
                            tx_message->endpoint_id,
                            tx_message->partition_no);
    }

    ++nr_tx_msgs_;
}

// 处理某个分片的消息，直到 ProcessMsgDone
void ShardingAgent::ProcessTxMsg(ShardingCtx* sharding_ctx)
{
    adk::ContEntry* cont_entry = nullptr;

    ami::Context* context = nullptr;
    context = ami_context_;
    auto& ctx_data = sharding_ctx->GetCtxData(false);
    adk::ContinueMemory* tx_con_mem = ctx_data.tx_cont_shm;

    uint64_t idle_counter = 0;
    while (is_running())
    {
        if (adk::ErrorCode::kSuccess != tx_con_mem->TryWaitEntry(&cont_entry))
        {
            if ((!is_low_latency_) && (++idle_counter > idle_loop_count_limit_))
            {
                usleep(idle_delay_microsec_);
            }
            else
            {
                for (int i = 0; i < 64; ++i)
                {
                    ADK_PAUSE();
                }
            }

            continue;
        }
        idle_counter = 0;

        ShmAgentHeader* agent_header = (ShmAgentHeader*)(cont_entry->GetBuffer());
        assert((uint32_t)(cont_entry->GetLength()) >= agent_header->msg_len);

        if (agent_header->msg_type == ShmMsgType::kAmiTxMsg)
        {
            struct ShmTxMessage* tx_message = (ShmTxMessage*)agent_header;
            if (is_calc_msg_latency_)
            {
                int64_t ts_now = adk::timespec_now();
                ha_latency_[AgentLAId::kMessageSort]->Save(tx_message->send_ts, ts_now);
            }
            OnShmTxMessage(context, tx_message);
            ++(ctx_ind_[0].nr_tx_message_);
        }
        else if (agent_header->msg_type == ShmMsgType::kShmProcessDoneMsg)
        {
            ShmProcessDoneMsg* process_msg = (ShmProcessDoneMsg*)agent_header;
            uint64_t process_msg_done = process_msg->total_order_sqn;

            context->ProcessMessageDone(process_msg_done);
            last_process_msg_done_ = process_msg_done;

            // 处理到 ProcessMsgDone 就结束
            tx_con_mem->FreeEntry(cont_entry);
            break;
        }
        else if (agent_header->msg_type == ShmMsgType::kShmDiscardMsg)
        {
            ShmDiscardMsg* discard_msg = (ShmDiscardMsg*)agent_header;
            assert(core_sqn_file_.is_open());
            // 必须在DiscardMessage函数前落地sqn，防止极端情况下，消息discard但sqn未落地
            core_sqn_file_ << std::to_string(discard_msg->total_order_sqn) << std::endl;

            context->ProcessMessageDiscard(discard_msg->total_order_sqn);
            ADK_LOG_INFO_AC_TF("recv ShmDiscardMsg",
                               "discard total_order_sqn <{1}>, last process_msg_done <{2}>",
                               discard_msg->total_order_sqn,
                               last_process_msg_done_);
        }
        else if(agent_header->msg_type == ShmMsgType::kShardingReq)
        {
            assert(aaf::g_sharding_channel);
            ShmShardingReq* sharding_req = (ShmShardingReq*)agent_header;
            aaf::g_sharding_channel->OnRequset(sharding_req->msg_body,
                                               sharding_req->header.msg_len - sizeof(ShmShardingReq),
                                               sharding_req->sharding_idx);
        }
        else
        {
            static adk::log::IntervalLogger interval_logger;
            ADK_INV_LOG_WARN_AC_TF(interval_logger,
                                   "invalid message type",
                                   "<{1}>",
                                   agent_header->msg_type);
        }

        tx_con_mem->FreeEntry(cont_entry);
    }
}

void ShardingAgent::SortRecvActor()
{
    ADK_LOG_INFO_AC_TF("receive actor start to run", "send msg with sort");

    adk::variant::VariantEntry* entry = nullptr;

    // ami::Context* context = ami_context_;

    uint64_t idle_counter = 0;
    while (is_running())
    {
        auto ec = sort_queue_->WaitEntry(&entry);
        if(ec != adk::ErrorCode::kSuccess)
        {
            if ((!is_low_latency_) && (++idle_counter > idle_loop_count_limit_))
            {
                usleep(idle_delay_microsec_);
            }
            else
            {
                for (int i = 0; i < 64; ++i)
                {
                    ADK_PAUSE();
                }
            }
            continue;
        }
        idle_counter = 0;
        SortElem* elem = (SortElem*)(entry->buffer);

        auto* sharding_ctx = sharding_ctx_vec_[elem->sharding_index];
        ProcessTxMsg(sharding_ctx);

        sort_queue_->FreeEntry(entry);
    }

    ADK_LOG_INFO_AC_TF("receive actor exit", "tx_message_nr <{1}>", nr_tx_msgs_);
}

template <bool IsSingleton>
void ShardingAgent::RecvActor()
{
    ADK_LOG_INFO_AC_TF("receive actor start to run", "");

    uint64_t idle_counter = 0;
    adk::ContEntry* cont_entry = nullptr;

    ami::Context* context = nullptr;
    adk::LatencyStatistics** latency;
    if (IsSingleton)
    {
        context = ami_context_sgt_;
        latency = sg_latency_;
    }
    else
    {
        context = ami_context_;
        latency = ha_latency_;
    }

    while (is_running())
    {
        uint64_t tx_msg_count = 0;
        for (int32_t index = 1; index <= sharding_num_; ++index)
        {
            auto& ctx_data = sharding_ctx_vec_[index]->GetCtxData(IsSingleton);
            adk::ContinueMemory* tx_con_mem = ctx_data.tx_cont_shm;

            if (adk::ErrorCode::kSuccess != tx_con_mem->TryWaitEntry(&cont_entry))
            {
                if(!is_running())
                {
                    break;
                }
                continue;
            }

            ShmAgentHeader* agent_header = (ShmAgentHeader*)(cont_entry->GetBuffer());
            assert((uint32_t)(cont_entry->GetLength()) >= agent_header->msg_len);

            switch (agent_header->msg_type)
            {
            case ShmMsgType::kAmiTxMsg:
            {
                struct ShmTxMessage* tx_message = (ShmTxMessage*)agent_header;
                if (is_calc_msg_latency_)
                {
                    int64_t ts_now = adk::timespec_now();
                    latency[AgentLAId::kMessageSort]->Save(tx_message->send_ts, ts_now);
                }
                OnShmTxMessage(context, tx_message);
                ++(ctx_ind_[IsSingleton].nr_tx_message_);
            }
            break;
            case ShmMsgType::kShmProcessDoneMsg:
            {
                ShmProcessDoneMsg* process_msg = (ShmProcessDoneMsg*)agent_header;
                uint64_t process_msg_done = process_msg->total_order_sqn;
                if (!IsSingleton)
                {
                    // sort
                    reorder_buff_.PutSqn(process_msg_done);
                    process_msg_done = reorder_buff_.GetNextSqn();
                }
                context->ProcessMessageDone(process_msg_done);
                last_process_msg_done_ = process_msg_done;
            }
            break;
            case ShmMsgType::kShmDiscardMsg:
            {
                ShmDiscardMsg* discard_msg = (ShmDiscardMsg*)agent_header;
                context->ProcessMessageDiscard(discard_msg->total_order_sqn);
                ADK_LOG_INFO_AC_TF("recv ShmDiscardMsg",
                                "discard total_order_sqn <{1}>, last process_msg_done <{2}>",
                                discard_msg->total_order_sqn,
                                last_process_msg_done_);
            }
            break;

            default:
                static adk::log::IntervalLogger interval_logger;
                ADK_INV_LOG_WARN_AC_TF(interval_logger,
                                    "invalid message type",
                                    "<{1}>",
                                    agent_header->msg_type);
            }

            ++tx_msg_count;
            tx_con_mem->FreeEntry(cont_entry);
        }

        if (tx_msg_count > 0)
        {
            idle_counter = 0;
            continue;
        }
        else
        {
            if ((!is_low_latency_) && (++idle_counter > idle_loop_count_limit_))
            {
                usleep(idle_delay_microsec_);
            }
            else
            {
                for (int i = 0; i < 64; ++i)
                {
                    ADK_PAUSE();
                }
            }
        }
    }

    ADK_LOG_INFO_AC_TF("receive actor exit", "tx_message_nr <{1}>", nr_tx_msgs_);
}

int32_t ShardingAgent::OnRun()
{
    // RecvActor 应该根据context数量 创建线程数
    // IsSingleton = true  单例查询结果  无需重排序 直接发送
    // IsSingleton = false 应用分片映射到不同的主题或者分区，规避排序问题

    if (GetArchforcePerformance() == aaf::AfPerformanceType::kLowUtilization)
    {
        is_low_latency_ = false;
        ADK_LOG_INFO_AC_TF("recv actor is working",
                           "performance mode on <LowUtilization>");
    }
    else
    {
        is_low_latency_ = true;
        ADK_LOG_INFO_AC_TF("recv actor is working",
                           "performance mode on <LowLatency>");
    }

    if (ami_context_ != nullptr)
    {
        recv_actor_thd_ = adk::boost_thread("ha-recv-actor",
                                        "HA Agent Send Message thread",
                                        boost::bind(&ShardingAgent::SortRecvActor, this));
    }

    if (ami_context_sgt_ != nullptr)
    {
        recv_actor_thd_sg_ = adk::boost_thread("sg-recv-actor",
                                            "SG Agent Send Message thread",
                                            boost::bind(&ShardingAgent::RecvActor<true>, this));
    }

    while (is_running())
    {
        // to do: 遍历增加foreach函数
        for (int32_t index = 1; index <= sharding_num_; ++index)
        {
            if (!IsPipeAlive(sharding_ctx_vec_[index]->proxy_to_agent_fds[0]))
            {
                ADK_LOG_INFO_AC_TF("read pipe from sharding error",
                                    "sharding_index <{1}>, errno <{2}>",
                                    index,
                                    errno);
                Stop();
                break;
            }
        }

        usleep(100);
    }

    ADK_LOG_INFO_AC_TF("sharding agent exit", "begin exit and join threads");
    if (recv_actor_thd_.joinable())
    {
        recv_actor_thd_.try_join_for(boost::chrono::milliseconds(200));
    }
    if (recv_actor_thd_sg_.joinable())
    {
        recv_actor_thd_sg_.try_join_for(boost::chrono::milliseconds(200));
    }
    if (proxy_log_thread_.joinable())
    {
        proxy_log_thread_.try_join_for(boost::chrono::milliseconds(200));
    }

    ADK_LOG_INFO_AC_TF("sharding agent exit", "exit complete");

    _exit(0);
    return aaf::ErrorCode::kSuccess;
}

void ShardingAgent::EventConvert(ShmEventType type, const std::string& prop_str, struct ShmAmiEvent* event)
{
    event->header.msg_type = ShmMsgType::kAmiEvent;
    event->header.msg_len  = sizeof(struct ShmAmiEvent) + prop_str.size();
    event->evt_type        = type;
    memcpy(event->event_body, prop_str.c_str(), prop_str.size());
}

void ShardingAgent::PushEvent(ShmEventType type, const std::string& prop_str)
{
    adk::ContEntry* entry_ptr = nullptr;
    adk::ContEntry* entry_ptr2 = nullptr;
    const uint32_t memory_len = sizeof(struct ShmAmiEvent) + prop_str.size();

    ADK_LOG_INFO_AC_TF("push event", "event type: <{1}>, pros: <{2}>", type, prop_str);
    for (int32_t index = 1; index <= sharding_num_; ++index)
    {
        auto& ctx_data = sharding_ctx_vec_[index]->GetCtxData(false);
        // to do: 事件应该按单例和高可用分别递交
        std::lock_guard<boost::detail::spinlock> lock(rx_spinlock_);  // 当前只有高可用递交事件加锁
        if (ADK_UNLIKELY(adk::ErrorCode::kSuccess
            != ctx_data.rx_cont_shm->AllocEntry(memory_len, &entry_ptr)))
        {
            // on exitting
            return;
        }
        struct ShmAmiEvent* event = (struct ShmAmiEvent*)(entry_ptr->GetBuffer());
        EventConvert(type, prop_str, event);
        ctx_data.rx_cont_shm->PostEntry(entry_ptr);
        (*ctx_data.futex_wait) = ctx_data.rx_cont_shm->GetProduceNR();
        adk::FutexWake(ctx_data.futex_wait);

        if (is_advance_follower_)
        {
            auto& trial_ctx_data = sharding_ctx_vec_[index]->GetCtxData(true);
            if (ADK_UNLIKELY(adk::ErrorCode::kSuccess
                             != trial_ctx_data.rx_cont_shm->AllocEntry(memory_len, &entry_ptr2)))
            {
                // on exitting
                return;
            }
            struct ShmAmiEvent* event = (struct ShmAmiEvent*)(entry_ptr2->GetBuffer());
            EventConvert(type, prop_str, event);
            trial_ctx_data.rx_cont_shm->PostEntry(entry_ptr2);
            (*trial_ctx_data.futex_wait) = trial_ctx_data.rx_cont_shm->GetProduceNR();
            adk::FutexWake(trial_ctx_data.futex_wait);
        }
    }
    ++event_nr_;
}

int32_t ShardingAgent::PipeWrite(const char* buff, uint32_t length)
{
    for (int32_t i = 1; i <= sharding_num_; ++i)
    {
        int agent_fd = sharding_ctx_vec_[i]->agent_to_proxy_fds[1];
        uint32_t buff_curr = 0;

        do
        {
            ssize_t ret = write(agent_fd, buff + buff_curr, length - buff_curr);
            if (ret < 0)
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    ADK_LOG_ERROR_AC_TF("write props to proxy pipe failed",
                                        "sharding index <{1}>, errno <{2}>, strerror: <{3}>",
                                        i,
                                        errno,
                                        strerror(errno));
                    return aaf::ErrorCode::kFailure;
                }
                static adk::log::IntervalLogger inv_log(2);
                ADK_INV_LOG_INFO_AC_TF(inv_log,
                                       "proxy is block",
                                       "sharding index <{1}>, errno <{2}>, strerror: <{3}>",
                                       i,
                                       errno,
                                       strerror(errno));
                usleep(0);
                continue;
            }

            buff_curr += ret;
            if (buff_curr < length)
            {
                usleep(0);
            }
            else
            {
                break;
            }
        } while (is_running());
    }

    return aaf::ErrorCode::kSuccess;
}

void ShardingAgent::OnSignal(int sig_num, int value)
{
    if (sharding_index_ && proxy_)
    {
        // sharding proxy
        proxy_->OnSignal(sig_num, value);
    }
    else
    {
        bool is_signal_exit = IsQuitSignal(sig_num, value);
        if (is_signal_exit)
        {
            // 发送两次退出信号，实时进程和试算进程都可能收到信号
            PipeWrite(&kComStop, 1);
            PipeWrite(&kComStop, 1);
            GenericApplication::Stop();
            is_running_ = false;
        }
    }
}

}  // end of namespace sharding