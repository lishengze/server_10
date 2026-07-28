#include "sharding_proxy.h"
#include "../src/signal_safe_log.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/file.h>

#include <boost/property_tree/json_parser.hpp>

#include <adk/arch/synchronize.h>
#include <adk/property.h>

using aaf::SignalSafeLogger;

namespace sharding
{

ADK_LOG_DEFINE(ShardingProxy);

using boost_ptree = boost::property_tree::ptree;

void ShardingProxy::AppLogger::Log(adk::log::LogLevel level,
                 adk::log::LogCode code,
                 const std::string& module_name,
                 const std::string& function_name,
                 uint32_t src_line,
                 const std::string& title,
                 const std::string& message)
{
    boost::lock_guard<boost::mutex> log_lock(log_mutex_);  // 写日志锁
    if (level < adk::log::g_logger->min_log_level())
    {
        return;
    }

    if (log_file_ < 0 || log_name_current_ != log_name_)
    {
        if (log_file_ >= 0)
        {
            close(log_file_);
            log_file_ = -1;
        }
        log_file_ = open(log_name_, O_APPEND | O_WRONLY | O_CREAT, 0666);
        log_name_current_ = log_name_;
        aaf::SignalSafeLogger::s_logger_inst_->SetLogFile(sharding_proxy_->aaf_instance_->GetLogDir(),
                                          sharding_proxy_->log_file_name_);
    }

    uint64_t alloc_len = sizeof(ProxyInfoLog) + title.length() + message.length();

    void* buff = nullptr;
    while (sharding_proxy_->is_running_ && sharding_proxy_->sccl_proxy_->status_valid())
    {
        if (ADK_UNLIKELY(alloc_len >= adk::sccl::kReserveSize))   // 当前agent使用默认参数启动，修改参数后此处需要同步修改
        {
            break;
        }

        buff = sharding_proxy_->sccl_proxy_->AllocBuffer(alloc_len);    // 日志太大超过1M，且agent还活着，会导致申请一直失败，且while循环满足条件，导致死锁
        if (buff != nullptr)
        {
            break;
        }
        // usleep(0);
    }

    if (ADK_UNLIKELY(!buff))
    {
        if (log_file_ < 0)
        {
            return;
        }

        static std::stringstream ss;
        ss.clear();
        ss.str("");
        ss << "@ "
           << boost::posix_time::ptime(boost::posix_time::microsec_clock::local_time()) << " "
           << sharding_proxy_->sharding_agent_->host_name_ << " "
           << sharding_proxy_->context_name_ << " "
           << pid() << " "
           << tid() << " "
           << level << " " << g_log_level_map[level] << " "
           << module_name << " "
           << function_name << " "
           << src_line << " "
           << code << " "
           << "| " << title << " "
           << "| " << message 
           << std::endl;

        std::string log_str = ss.str();
        write(log_file_, (void *)log_str.c_str(), log_str.length());  // 暂时不处理写失败及未写完的场景

        return;
    }
    ProxyInfoLog* info        = (ProxyInfoLog*)buff;
    info->info_head.info_type = ProxyInfoType::kLogInfo;
    info->pid = pid();
    info->tid = tid();

    info->level = level;
    info->code  = code;

    uint32_t module_name_len = std::min((uint32_t)module_name.length(), module_name_len_max);
    memcpy(info->module_name, module_name.c_str(), module_name_len);
    info->module_name[module_name_len] = '\0';

    uint32_t function_name_len = std::min((uint32_t)function_name.length(), function_name_len_max);
    memcpy(info->function_name, function_name.c_str(), function_name_len);
    info->function_name[function_name_len] = '\0';

    info->src_line = src_line;

    info->title_len = title.length();
    info->message_len = message.length();
    memcpy(info->title_message, title.c_str(), info->title_len);
    memcpy((((char*)info->title_message) + info->title_len), message.c_str(), info->message_len);

    if (sharding_proxy_->is_trial_)
    {
        info->info_head.info_type = ProxyInfoType::kLogInfoTri;
    }

    sharding_proxy_->sccl_proxy_->PostBuffer(buff, alloc_len);
}

void ShardingProxy::AppLogger::Receive(adk::IMonitorSinker::Type type,
                                       uint64_t query_key,
                                       const boost::property_tree::ptree& msg_tree)
{
    oss_.clear();
    oss_.str("");
    boost::property_tree::json_parser::write_json(oss_, msg_tree, false);

    adk::Property ind_props;
    ind_props.SetValues()
            ("QueryKey", query_key)
            ("Type", adk::IMonitorSinker::GetTypeDesc(type))
            ("Message", oss_.str())
            ;
    std::string ind_buffer = ind_props.Dump();

    uint64_t alloc_len = sizeof(ProxyInfoInd) + ind_buffer.length();
    void* buff = sharding_proxy_->sccl_proxy_->AllocBuffer(alloc_len);
    if (ADK_UNLIKELY(!buff))  // todo: agent挂了，指标暂时不考虑直接写文件
    {
        return;
    }
    ProxyInfoInd* info        = (ProxyInfoInd*)buff;
    info->info_head.info_type = ProxyInfoType::kInidcateInfo;
    info->body_len            = ind_buffer.length();
    memcpy(info->info_body, ind_buffer.c_str(), ind_buffer.length());
    info->sharding_index = sharding_proxy_->sharding_index_;
    sharding_proxy_->sccl_proxy_->PostBuffer(buff, alloc_len);
}

int32_t ShardingProxy::Init(ShardingAgent* sharding_agent, int32_t sharding_index)
{
    
    sharding_agent_      = sharding_agent;
    sharding_index_      = sharding_index;
    aaf_instance_        = sharding_agent->aaf_instance_;
    sharding_ctx_        = sharding_agent->sharding_ctx_vec_[sharding_index];
    shm_data_manager_    = sharding_agent_->shm_data_manager_;
    ep_info_props_       = sharding_agent_->ep_info_props_;
    is_advance_follower_ = sharding_agent_->is_advance_follower_;

    keep_alive_fd_ = sharding_ctx_->agent_to_proxy_fds[0];

    context_name_  = aaf_instance_->GetApplicationName();
    domain_server_ = aaf_instance_->domain_server();

    log_file_name_ = aaf_instance_->GetApplicationName();

    sccl_proxy_name_ = "sharding_" + std::to_string(sharding_index_);
    sccl_proxy_ = adk::sccl::Proxy::Create(sharding_agent_->sccl_name_, sccl_proxy_name_);
    if (sccl_proxy_ == nullptr)
    {
        ADK_LOG_ERROR_AC_TF("shm cont channel create failed", "sccl proxy name: <{1}>", sccl_proxy_name_);
        return aaf::ErrorCode::kFailure;
    }

    proxy_logger_ = new AppLogger(this);
    ADK_LOG_USE_LOGGER(proxy_logger_);
    proxy_logger_->log_name_ = sharding_agent_->log_name_shm_;

    memset(ha_latency_, 0, sizeof(ha_latency_));
    memset(sg_latency_, 0, sizeof(sg_latency_));

    uint64_t latency_size = sharding_agent_->latency_size_;

    if (sharding_agent_->is_calc_msg_latency_)
    {
        ha_latency_[ProxyLAId::kWake] =
            ha_la_metric_.AddLatencySpan("WakeSpan", latency_size);

        ha_latency_[ProxyLAId::kOnMessage] =
            ha_la_metric_.AddLatencySpan("OnMessage", latency_size);

        if (is_advance_follower_)
        {
            ha_latency_[ProxyLAId::kWaitTrial] =
                ha_la_metric_.AddLatencySpan("WaitTrial", latency_size);
        }

        if (aaf_instance_->GetSingletonContext())
        {
            sg_latency_[ProxyLAId::kWake] =
                sg_la_metric_.AddLatencySpan("WakeSpan", latency_size);
            sg_latency_[ProxyLAId::kOnMessage] =
                sg_la_metric_.AddLatencySpan("OnMessage", latency_size);
        }
    }

    if (adk::EnableShareMemoryDump(nullptr) != adk::ErrorCode::kSuccess)
    {
        ADK_LOG_INFO_AC_TF("coredump share memory is disabled", "");
    }

    ParserEnv();

    sharding_agent_->ShmDataFlush();

    const int32_t total_len = sizeof(ami::AmiMessage) + sizeof(ami::Message);

    size_t page_size = ADK_PAGE_SIZE;
    void* addr1      = memalign(page_size, total_len);
    void* addr2      = memalign(page_size, total_len);

    ami_msg_ = (ami::AmiMessage*)addr1;
    ami_msg_->reset();
    ami_msg_->message()->ResetAppMessage();

    ami_msg_sg_ = (ami::AmiMessage*)addr2;
    ami_msg_sg_->reset();
    ami_msg_sg_->message()->ResetAppMessage();

    adk::Monitor::Start();
    adk::Monitor::SetShardingIndex(sharding_index_, aaf_instance_->GetShardingNum());

    if (adk::Monitor::PluginSinker(proxy_logger_) != adk::ErrorCode::kSuccess)
    {
        ADK_LOG_ERROR_AC_TF("plugin proxy sinker failed", "context <{1}>", context_name_);
        return aaf::ErrorCode::kFailure;
    }

    adk::MonitorOps app_monitor_ops;
    app_monitor_ops.is_collection_indicator = true;
    app_monitor_ops.on_collection_indicator = boost::bind(
                                    &ShardingProxy::OnCollectIndicator, this, _1);
    
    adk::Monitor::RegisterObject("Sharding",
                                 context_name_ + "_Sharding_" + std::to_string(sharding_index_),
                                 &app_monitor_ops);

    /***************************  OnAmiInitBegin  ***************************/
    try
    {
        if (aaf_instance_->OnAmiInitBegin() != aaf::ErrorCode::kSuccess)
        {
            return aaf::ErrorCode::kFailure;
        }
    }
    catch (...)
    {
        ADK_LOG_ERROR_AC_TF("application throw exception in <OnAmiInitBegin>",
                            "exception <{1}>",
                            boost::current_exception_diagnostic_information());
        return aaf::ErrorCode::kFailure;
    }

    auto ec = InitFramework();
    if (ec != aaf::ErrorCode::kSuccess)
    {
        return ec;
    }

    auto& shm_seq_map = sharding_agent_->sharding_lock_map_;
    for (auto& iter : shm_seq_map)
    {
        shm_seq_lock_vec_.push_back(iter.second->shm_seq_lock_);
    }
    if (is_advance_follower_)
    {
        ADK_LOG_INFO_AC_TF("Advance Follower Init",
                           "context <{1}> is init as advance follower",
                           context_name_);
        std::string launch_info_shm_name = MakeFlrLaunchShmName(context_name_);

        void* flr_addr = adk::ShmFactory::Attach(launch_info_shm_name);
        if (nullptr == flr_addr)
        {
            ADK_LOG_ERROR_AC_TF("attach FlrProxyLaunchShm failed",
                                "launch info share name <{1}>, errno <{2}>",
                                launch_info_shm_name,
                                errno);
            return aaf::ErrorCode::kFailure;
        }

        flr_launch_ = (FlrProxyLaunchShm*)((uint64_t)flr_addr + kFlrLauncheSize * (sharding_index_ -1));

        flr_launch_->Init(sharding_index_);

        ec = StartTrialProcess();
        if (ec != aaf::ErrorCode::kSuccess)
        {
            return ec;
        }
    }

    ADK_LOG_INFO_AC_TF("ShardingProxy init success",
                       "context <{1}>, sharding index <{2}>",
                       context_name_,
                       sharding_index_);
    is_running_ = true;
    return aaf::ErrorCode::kSuccess;
}

int32_t ShardingProxy::AttachShmContMem()
{
    share_memory_name_ = MakeShmContMemoryName(context_name_);
    adk::ShmContMemManager* memory_manager = adk::ShmContMemManager::Attach(share_memory_name_);

    if (nullptr == memory_manager)
    {
        ADK_LOG_ERROR_AC_TF("attach share memory failed",
                            "share memory name <{1}>",
                            share_memory_name_);
        return aaf::ErrorCode::kFailure;
    }

    const std::string index_str = std::to_string(sharding_index_);
    sharding_ctx_->ha_ctx_data.tx_cont_shm = memory_manager->AttachShmContMemory("tx_sharding_" + index_str);
    if (nullptr == sharding_ctx_->ha_ctx_data.tx_cont_shm)
    {
        ADK_LOG_ERROR_AC_TF("attach tx continue memory failed",
                            "share memory name <{1}>",
                            share_memory_name_);
        return aaf::ErrorCode::kFailure;
    }

    sharding_ctx_->sgt_ctx_data.tx_cont_shm = memory_manager->AttachShmContMemory("sg_tx_sharding_" + index_str);
    if (nullptr == sharding_ctx_->sgt_ctx_data.tx_cont_shm)
    {
        ADK_LOG_ERROR_AC_TF("attach singleton tx continue memory failed",
                            "share memory name <{1}>",
                            share_memory_name_);
        return aaf::ErrorCode::kFailure;
    }

    sharding_ctx_->ha_ctx_data.rx_cont_shm = memory_manager->AttachShmContMemory("rx_sharding_" + index_str);
    if (nullptr == sharding_ctx_->ha_ctx_data.rx_cont_shm)
    {
        ADK_LOG_ERROR_AC_TF("attach rx continue memory failed",
                            "share memory name <{1}>",
                            share_memory_name_);
        return aaf::ErrorCode::kFailure;
    }

    sharding_ctx_->sgt_ctx_data.rx_cont_shm = memory_manager->AttachShmContMemory("sg_rx_sharding_" + index_str);
    if (nullptr == sharding_ctx_->sgt_ctx_data.rx_cont_shm)
    {
        ADK_LOG_ERROR_AC_TF("attach singleton rx continue memory failed",
                            "share memory name <{1}>",
                            share_memory_name_);
        return aaf::ErrorCode::kFailure;
    }
    return aaf::ErrorCode::kSuccess;
}

void ShardingProxy::ParserEnv()
{
    char* type = std::getenv("AAF_SIDE_RECV_POLLING");
    if (type != nullptr)
    {
        is_polling_ = true;
    }
}

void ShardingProxy::PollingMessage(bool is_singleton)
{
    ami::AmiMessage* ami_msg = nullptr;
    int32_t expect_proxy_wait = 0;

    ShardingCtx::CtxData* ctx_data = nullptr;

    adk::LatencyStatistics** latency;

    if (is_singleton)
    { 
        ctx_data = &(sharding_ctx_->sgt_ctx_data);
        ami_msg = ami_msg_sg_;
        latency = sg_latency_;
    }
    else
    {
        ctx_data = &(sharding_ctx_->ha_ctx_data);
        ami_msg = ami_msg_;
        latency = ha_latency_;
    }

    int* proxy_wait = ctx_data->futex_wait;
    adk::ContinueMemory* rx_cont_shm = ctx_data->rx_cont_shm;

    ADK_LOG_INFO_AC_TF("recv actor is working",
                       "Polling mode <{1}>, is_advance_follower <{2}>, is_trial <{3}>",
                       is_polling_,
                       is_advance_follower_,
                       is_trial_);

    timespec timeout = {0, 100ul * 1000ul};
    adk::ContEntry* cont_entry = nullptr;
    int64_t ts_wake, ts_wait, ts_on_message;
    const bool is_calc_la = sharding_agent_->is_calc_msg_latency_;

    while (aaf_instance_->is_running())
    {
        if (adk::ErrorCode::kSuccess == rx_cont_shm->TryWaitEntry(&cont_entry))
        {
            ShmAgentHeader* agent_header = (ShmAgentHeader*)(cont_entry->GetBuffer());
            assert((uint32_t)(cont_entry->GetLength()) >= agent_header->msg_len);

            if (is_calc_la)
            {
                ts_wake = adk::timespec_now();
                ts_wait = ts_wake;
            }

            if (is_advance_follower_)
            {
                if (ADK_UNLIKELY(!WaitTrialProcess(ctx_data->nr_shm_processed_sqn)))
                {
                    // 实时进程和试算进程都会从此处返回继续执行 
                    // 更新局部变量，重新 TryWaitEntry
                    ctx_data = &(sharding_ctx_->GetCtxData(false));
                    proxy_wait = ctx_data->futex_wait;
                    rx_cont_shm = ctx_data->rx_cont_shm;
                    assert(flr_launch_->nr_shm_processed_sqn >= ctx_data->nr_shm_processed_sqn);
                    continue;
                }
                if (is_calc_la)
                {
                    ts_wait = adk::timespec_now();
                    latency[ProxyLAId::kWaitTrial]->Save(ts_wake, ts_wait);
                }
            }

            switch (agent_header->msg_type)
            {
            case ShmMsgType::kAmiRxMsg:
            {
                ShmRxMessage* shm_rx_msg = (ShmRxMessage*)agent_header;
                ++(ctx_data->nr_msg_delivered);
                ctx_data->last_deliver_msg_sqn = shm_rx_msg->total_order_sqn;
                if (is_trial_)
                {
                    assert(flr_launch_->nr_shm_processed_sqn >= ctx_data->nr_shm_processed_sqn);
                    flr_launch_->nr_msg_deliverd_sqn = shm_rx_msg->total_order_sqn;
                    DeliverMessage(shm_rx_msg, ami_msg);
                    ts_on_message = adk::timespec_now();
                    ADK_BARRIER();
                    flr_launch_->nr_msg_processed_sqn = shm_rx_msg->total_order_sqn;
                }
                else
                {
                    DeliverMessage(shm_rx_msg, ami_msg);
                    ts_on_message = adk::timespec_now();
                    ProcessMessageDone(ctx_data, shm_rx_msg->total_order_sqn);
                }
                if (is_calc_la)
                {
                    latency[ProxyLAId::kWake]->Save(shm_rx_msg->send_ts, ts_wake);
                    latency[ProxyLAId::kOnMessage]->Save(ts_wait, ts_on_message);
                }
                if (!is_singleton)
                {
                    for (auto* seq_lock : shm_seq_lock_vec_)
                    {
                        seq_lock->UnLock(ctx_data->last_deliver_msg_sqn, sharding_ctx_->sharding_index);
                    }
                }

                ++(ctx_data->nr_msg_processed);
                break;
            }
            case ShmMsgType::kAmiEvent:
                DeliveEvent((ShmAmiEvent*)agent_header);
                break;
            case ShmMsgType::kShardingPost:
            {
                assert(aaf::g_sharding_channel);
                aaf::g_sharding_channel->OnResponse((char*)(agent_header + 1),
                                                    agent_header->msg_len - sizeof(ShmAgentHeader));
                break;
            }
            default:
                static adk::log::IntervalLogger interval_logger;
                ADK_INV_LOG_WARN_AC_TF(interval_logger,
                                       "invalid message type: ",
                                       "<{1}>",
                                       agent_header->msg_type);
            }

            ++(ctx_data->nr_shm_processed_sqn);
            if (is_trial_)
            {
                assert(flr_launch_->nr_msg_deliverd_sqn == flr_launch_->nr_msg_processed_sqn);
                flr_launch_->nr_shm_processed_sqn = ctx_data->nr_shm_processed_sqn;
                assert(flr_launch_->nr_shm_processed_sqn >= ctx_data->nr_shm_processed_sqn);
            }

            rx_cont_shm->FreeEntry(cont_entry);
            continue;
        }
        else
        {
            if (!IsAgentAlive())
            {
                return;
            }

            if (is_advance_follower_
                && ADK_UNLIKELY(!CheckAndFixTrial(ctx_data->nr_shm_processed_sqn)))
            {
                continue;
            }
        }

        if (!is_polling_)
        {
            expect_proxy_wait = static_cast<int>(rx_cont_shm->GetConsumeNR());
            adk::FutexWait(proxy_wait, expect_proxy_wait, &timeout);
        }
    }
}

int32_t ShardingProxy::OnRun()
{
    ADK_LOG_INFO_AC_TF("instance begin to run",
                       "sharding index <{1}>",
                       sharding_index_);

    if (is_advance_follower_)
    {
        PollingMessage(false);

        if (is_trial_)
        {
            ADK_LOG_INFO_AC_TF("trial exit",
                               "sharding index <{1}>",
                               sharding_index_);
            write(trial_fds_[1], &kComStop, 1);
        }
        return aaf::ErrorCode::kSuccess;
    }

    // 根据context数量创建 线程递交消息
    if (aaf_instance_->GetContext() != nullptr)
    {
        rx_threads_[0] = adk::boost_thread("ha-rx",
                                           "ha rx",
                                           boost::bind(&ShardingProxy::PollingMessage, this, false));
    }

    if (aaf_instance_->GetSingletonContext() != nullptr)
    {
        rx_threads_[1] = adk::boost_thread("sg-rx",
                                           "singleton rx",
                                           boost::bind(&ShardingProxy::PollingMessage, this, true));
    }

    while (IsAgentAlive() && aaf_instance_->is_running())  // 某一分片退出 -> agent退出 -> 当前分片退出
    {
        usleep(100);
    }
    aaf_instance_->Stop();

    if (rx_threads_[0].joinable())
    {
        // application may be dead-locked on ShmSeqLock
        rx_threads_[0].try_join_for(boost::chrono::milliseconds(200));
    }
    if (rx_threads_[1].joinable())
    {
        rx_threads_[1].try_join_for(boost::chrono::milliseconds(200));
    }

    return aaf::ErrorCode::kSuccess;
}

// 模拟aaf框架，完成应用初始化  回调 On Tx/Rx EndpointCreation
int32_t ShardingProxy::InitFramework()
{
    assert(aaf_instance_);
    ami::Property ami_props = aaf_instance_->ha_context_property();
    ADK_LOG_INFO_AC_TF("ami context property",
                       "props <{1}>",
                       ami_props.Dump());

    std::vector<ami::Property> ep_infos;
    ep_infos = ep_info_props_.GetValue(ami::config::context::kEndpointInfoList, ep_infos);

    for (auto& ep_info : ep_infos)
    {
        assert(ep_info.HasValue(ami::config::context::kIsTxEndpoint) == true);

        uint32_t endpoint_id       = ep_info.GetValue(ami::config::context::kEndpointId, 0);
        std::string endpoint_name = ep_info.GetValue(ami::config::context::kEndpointName, "");

        bool is_tx_ep = ep_info.GetValue(ami::config::context::kIsTxEndpoint, false);
        std::vector<int32_t> partitions;
        partitions = ep_info.GetValue(ami::config::endpoint::kPartitions, partitions);
        assert(partitions.size() > 0);
        bool is_singleton = ep_info.GetValue(ami::config::context::kIsSingleton, false);

        if (is_tx_ep)
        {
            ShardingTxEndpointInfo* tx_info = nullptr;
            auto iter = tx_endpoint_info_map_.find(endpoint_name);
            if (iter == tx_endpoint_info_map_.end())
            {
                tx_info = new ShardingTxEndpointInfo();
                tx_endpoint_info_map_.insert({endpoint_name, tx_info});
            }
            else
            {
                tx_info = iter->second;
                assert(tx_info->endpoint_id == endpoint_id);
            }
            tx_info->endpoint_id = endpoint_id;
            tx_info->partitions  = partitions;
            tx_info->is_singleton = is_singleton;
        }
        else
        {
            RxEndpointInfo* rx_info = nullptr;

            auto iter = rx_endpoint_info_map_.find(endpoint_id);
            if (iter == rx_endpoint_info_map_.end())
            {
                rx_info = new RxEndpointInfo();
                rx_info->endpoint_name = endpoint_name;
                rx_info->endpoint_id   = endpoint_id;
                rx_info->is_singleton  = is_singleton;
                rx_endpoint_info_map_.insert({endpoint_id, rx_info});
            }
            else
            {
                rx_info = iter->second;
                assert(rx_info->endpoint_id == endpoint_id);
            }
        }
    }

    /***************************  OnTxEndpointCreation  ***************************/
    try
    {
        if (aaf_instance_->OnTxEndpointCreationBegin() != aaf::ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("OnTxEndpointCreationBegin return with failure", "");
            return aaf::ErrorCode::kFailure;
        }
    }
    catch (...)
    {
        ADK_LOG_ERROR_AC_TF("application throw exception in <OnTxEndpointCreationBegin>",
                            "exception <{1}>",
                            boost::current_exception_diagnostic_information());
        return aaf::ErrorCode::kFailure;
    }

    for (const auto& tx_iter : tx_endpoint_info_map_)
    {
        ShardingTxEndpointInfo* tx_info = tx_iter.second;
        assert(tx_info);

        tx_info->shd_tx_ep = new ShardingTxHandler(this, tx_info->endpoint_id, tx_info->is_singleton);

        if (aaf_instance_->OnTxEndpointCreation(tx_info->shd_tx_ep, tx_iter.first)
            != aaf::ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("OnTxEndpointCreation return with error",
                                "endpoint name <{1}>",
                                tx_iter.first);
            return aaf::ErrorCode::kFailure;
        }
        ADK_LOG_INFO_AC_TF("OnTxEndpointCreation success",
                           "endpoint name <{1}>, endpoint id <{2}>",
                           tx_iter.first,
                           tx_info->endpoint_id);
    }

    /***************************  OnRxEndpointCreation  ***************************/
    default_msg_handler_->application_instance_ = aaf_instance_;
    default_msg_handler_sgt_->application_instance_ = aaf_instance_;
    // default use GenericAmiApplication::OnMessage

    try
    {
        if (aaf_instance_->OnRxEndpointCreationBegin() != aaf::ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("OnRxEndpointCreationBegin return with failure", "");
            return aaf::ErrorCode::kFailure;
        }
    }
    catch (...)
    {
        ADK_LOG_ERROR_AC_TF("application throw exception in <OnRxEndpointCreationBegin>",
                            "exception <{1}>",
                            boost::current_exception_diagnostic_information());
        return aaf::ErrorCode::kFailure;
    }

    std::vector<std::string> rx_ep_vec;
    rx_ep_vec = ami_props.GetValue(ami::config::context::kRxEndpoints, rx_ep_vec);

    for (const auto& rx_iter : rx_endpoint_info_map_)
    {
        RxEndpointInfo* rx_info = rx_iter.second;
        assert(rx_info);
        ami::MessageHandler* msg_handler = nullptr;

        if (aaf_instance_->OnRxEndpointCreation(rx_info->endpoint_name, &msg_handler, !rx_info->is_singleton)
            != aaf::ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("OnRxEndpointCreation return with error",
                                "endpoint name <{1}>",
                                rx_info->endpoint_name);
            return aaf::ErrorCode::kFailure;
        }

        if (msg_handler != nullptr)
        {
            rx_info->msg_handler = msg_handler;
        }
        else
        {
            if (rx_info->is_singleton)
            {
                rx_info->msg_handler = default_msg_handler_sgt_;
            }
            else
            {
                rx_info->msg_handler = default_msg_handler_;
            }
        }
        ADK_LOG_INFO_AC_TF("OnRxEndpointCreation success",
                           "endpoint name <{1}>, message handler is empty <{2}>, singleton <{3}>",
                           rx_info->endpoint_name,
                           (msg_handler == nullptr),
                           rx_info->is_singleton);
    }

    try
    {
        if (aaf_instance_->OnAmiInitEnd() != aaf::ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("OnAmiInitEnd return with failure", "");
            return aaf::ErrorCode::kFailure;
        }
    }
    catch (...)
    {
        ADK_LOG_ERROR_AC_TF("application throw exception in <OnAmiInitEnd>",
                            "exception <{1}>",
                            boost::current_exception_diagnostic_information());
        return aaf::ErrorCode::kFailure;
    }

    return aaf::ErrorCode::kSuccess;
}

template <bool IsSingleton>
int32_t ShardingProxy::SendMsg(const void* buf,
                              uint32_t len,
                              int32_t partition_no,
                              uint32_t endpoint_id,
                              ami::TraceRecord record)
{
    ShardingCtx::CtxData& ctx_data = sharding_ctx_->GetCtxData(IsSingleton);
    if (is_trial_)
    {
        ++ctx_data.nr_tx_msgs;
        return aaf::ErrorCode::kSuccess;
    }

    adk::ContEntry* entry_ptr = nullptr;
    const uint32_t memory_len = sizeof(ShmTxMessage) + len;

    adk::ContinueMemory* tx_cont_memory = ctx_data.tx_cont_shm;

    std::lock_guard<boost::detail::spinlock> lock((ctx_data.tx_spinlock));  // 多线程发送加锁
    if (ADK_UNLIKELY(adk::ErrorCode::kSuccess
                        != tx_cont_memory->AllocEntry(memory_len, &entry_ptr)))
    {
        return aaf::ErrorCode::kFailure;
    }

    ShmTxMessage* tx_msg    = (ShmTxMessage*)(entry_ptr->GetBuffer());
    tx_msg->header.msg_type = sharding::ShmMsgType::kAmiTxMsg;
    tx_msg->header.msg_len  = memory_len;
    tx_msg->endpoint_id     = endpoint_id;
    tx_msg->partition_no    = partition_no;
    tx_msg->trace_record    = record.data;
    memcpy(tx_msg->msg_body, buf, len);

    tx_msg->send_ts = adk::timespec_now();

    tx_cont_memory->PostEntry(entry_ptr);
    ++ctx_data.nr_tx_msgs;
    return aaf::ErrorCode::kSuccess;
}

int32_t ShardingProxy::ProcessMessageDone(ShardingCtx::CtxData* ctx_data,
                                          uint64_t total_order_sqn)
{
    if (is_trial_)
    {
        return aaf::ErrorCode::kSuccess;
    }
    adk::ContEntry* entry_ptr = nullptr;
    const uint32_t memory_len = sizeof(ShmProcessDoneMsg);

    adk::ContinueMemory* tx_cont_memory = ctx_data->tx_cont_shm;

    std::lock_guard<boost::detail::spinlock> lock((ctx_data->tx_spinlock));  // tx_cont_memory加锁
    if (ADK_UNLIKELY(adk::ErrorCode::kSuccess
                        != tx_cont_memory->AllocEntry(memory_len, &entry_ptr)))  // 阻塞alloc
    {
        return aaf::ErrorCode::kFailure;
    }

    ShmProcessDoneMsg* process_msg = (ShmProcessDoneMsg*)(entry_ptr->GetBuffer());
    process_msg->header.msg_type   = ShmMsgType::kShmProcessDoneMsg;
    process_msg->header.msg_len    = memory_len;
    process_msg->total_order_sqn   = total_order_sqn;

    tx_cont_memory->PostEntry(entry_ptr);
    return aaf::ErrorCode::kSuccess;
}

int32_t ShardingProxy::DiscardMessage(uint64_t total_order_sqn)
{
    if (is_trial_)
    {
        return aaf::ErrorCode::kSuccess;
    }

    ShardingCtx::CtxData& ctx_data = sharding_ctx_->GetCtxData(false);
    adk::ContEntry* entry_ptr = nullptr;
    const uint32_t memory_len = sizeof(ShmDiscardMsg);

    std::lock_guard<boost::detail::spinlock> lock(ctx_data.tx_spinlock);
    if (ADK_UNLIKELY(adk::ErrorCode::kSuccess
                        != ctx_data.tx_cont_shm->AllocEntry(memory_len, &entry_ptr)))
    {
        return aaf::ErrorCode::kFailure;
    }

    ShmDiscardMsg* discard_msg = (ShmDiscardMsg*)(entry_ptr->GetBuffer());
    discard_msg->header.msg_type = ShmMsgType::kShmDiscardMsg;
    discard_msg->header.msg_len = memory_len;
    discard_msg->total_order_sqn = total_order_sqn;

    ctx_data.tx_cont_shm->PostEntry(entry_ptr);
    return aaf::ErrorCode::kSuccess;
}

int32_t ShardingProxy::ShardingRequest(const void* data, uint32_t size)
{
    if (is_trial_)
    {
        return aaf::ErrorCode::kSuccess;
    }
    adk::ContEntry* entry_ptr = nullptr;
    const uint32_t memory_len = sizeof(ShmShardingReq) + size;

    ShardingCtx::CtxData& ctx_data = sharding_ctx_->GetCtxData(false);
    adk::ContinueMemory* tx_cont_memory = ctx_data.tx_cont_shm;

    std::lock_guard<boost::detail::spinlock> lock((ctx_data.tx_spinlock));  // tx_cont_memory加锁
    if (ADK_UNLIKELY(adk::ErrorCode::kSuccess
                        != tx_cont_memory->AllocEntry(memory_len, &entry_ptr)))  // 阻塞alloc
    {
        return aaf::ErrorCode::kFailure;
    }

    ShmShardingReq* sharding_req = (ShmShardingReq*)(entry_ptr->GetBuffer());
    sharding_req->header.msg_type   = ShmMsgType::kShardingReq;
    sharding_req->header.msg_len    = memory_len;
    sharding_req->sharding_idx   = sharding_index_;
    memcpy(sharding_req->msg_body, data, size);

    tx_cont_memory->PostEntry(entry_ptr);
    return aaf::ErrorCode::kSuccess;
}


int32_t ShardingProxy::DeliveEvent(ShmAmiEvent* ami_event)
{
    ++nr_event_cnt_;
    switch(ami_event->evt_type)
    {
        case ShmEventType::kOnRoleChangeToLeader:
        {
            aaf_instance_->OnRoleChangeToLeader();
            break;
        }
        case ShmEventType::kOnRoleChangeToMember:
        {
            aaf_instance_->OnRoleChangeToMember();
            break;
        }
        case ShmEventType::kOnRoleChangeToMaster:
        {
            aaf_instance_->OnRoleChangeToMaster();
            break;
        }
        case ShmEventType::kOnRecoveryBegin:
        {
            aaf_instance_->OnRecoveryBegin();
            break;
        }
        case ShmEventType::kOnRecoverySuccess:
        {
            aaf_instance_->OnRecoverySuccess();
            break;
        }
        case ShmEventType::kOnNoReceiver:
        {
            assert(ami_event->header.msg_len > sizeof(ShmAmiEvent));
            uint32_t prop_len = ami_event->header.msg_len - sizeof(ShmAmiEvent);
            std::string prop_str(ami_event->event_body, prop_len);
            ami::Property props(prop_str);
            aaf_instance_->OnNoReceiver(props.GetValue("endpoint_name", ""),
                                        props.GetValue("partition", ""));
            break;
        }
        case ShmEventType::kOnReceiverUp:
        {
            assert(ami_event->header.msg_len > sizeof(ShmAmiEvent));
            uint32_t prop_len = ami_event->header.msg_len - sizeof(ShmAmiEvent);
            std::string prop_str(ami_event->event_body, prop_len);
            ami::Property props(prop_str);
            aaf_instance_->OnReceiverUp(props.GetValue("endpoint_name", ""),
                                        props.GetValue("partition", ""));
            break;
        }
        case ShmEventType::kOnDiscardMessage:
        {
            assert(ami_event->header.msg_len > sizeof(ShmAmiEvent));
            uint32_t prop_len = ami_event->header.msg_len - sizeof(ShmAmiEvent);
            std::string arg_str(ami_event->event_body, prop_len);
            aaf_instance_->OnDiscardMessage(arg_str);
            break;
        }
    }

    return aaf::ErrorCode::kSuccess;
}

// true: trial process done
// false: trial process error,skip this msg; 
//        or keeplive pipe is broken.
bool ShardingProxy::WaitTrialProcess(uint64_t nr_processd_sqn)
{
    const uint64_t kWaitCounterLimit = 256;  // 350us

    uint64_t wait_conuter = 0;
    struct timespec ts_begin;
    struct timespec ts_end;

    if (is_trial_)
    {
        return true;
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &ts_begin);
    // wait trial commit sqn
    // 即便试算进程已经crash，也要先递交已经完成试算的消息
    while (nr_processd_sqn >= ACCESS_ONCE(flr_launch_->nr_shm_processed_sqn))
    {
        // wait and check trial process is running
        if (!IsTrialAlive())
        {
            // check IsPipeAlive
            if (!IsAgentAlive())
            {
                return false;
            }

            if (nr_processd_sqn < ACCESS_ONCE(flr_launch_->nr_shm_processed_sqn))
            {
                // 在检查管道的过程中，可能试算更新了 nr_shm_processed_sqn
                return true;
            }

            // 此时 cont_entry中的即是异常消息
            OnTrialError();
            // 实时进程和试算进程都会从此处返回继续执行 重新 TryWaitEntry
            clock_gettime(CLOCK_MONOTONIC_RAW, &ts_end);
            uint64_t diff = adk::time_diff(ts_end, ts_begin);
            ADK_LOG_INFO_AC_TF("create trial continue to run",
                                "shm_processd_sqn <{1}>, cost <{2}> us",
                                nr_processd_sqn,
                                (diff / 1000));

            return false;
        }
        else
        {
            static adk::log::IntervalLogger interval_logger(3);
            if (wait_conuter > kWaitCounterLimit)
            {
                ADK_INV_LOG_INFO_AC_TF(interval_logger,
                                        "waitting trial processed",
                                        "cur nr_shm_processed_sqn <{1}>",
                                        nr_processd_sqn);
                wait_conuter = 0;
            }
        }

        if (!IsAgentAlive())
        {
            return false;
        }

        for (int i = 0; i < 64; ++i)
        {
            ADK_PAUSE();
        }
        ++wait_conuter;
    }

    return true;
}

bool ShardingProxy::IsTrialAlive()
{
    if (!is_running_)
    {
        return false;
    }

    char cmd;
    if (!IsPipeAlive(trial_fds_[0], cmd))
    {
        if (cmd == kComStop)
        {
            ADK_LOG_INFO_AC_TF("recv trial exit signal",
                               "sharding index <{1}>",
                               sharding_index_);
            is_running_ = false;
            return false;
        }
        ADK_LOG_ERROR_AC_TF("read pipe error",
                            "errno <{1}>, desc <{2}>, sharding_index <{3}>",
                            errno,
                            strerror(errno),
                            sharding_index_);
        return false;
    }
    return true;
}

bool ShardingProxy::CheckAndFixTrial(uint64_t total_order_sqn)
{
    if (!is_trial_ && (IsTrialAlive() == false))
    {
        uint64_t msg_total_sqn = ACCESS_ONCE(flr_launch_->nr_msg_processed_sqn);
        uint64_t shm_total_sqn = ACCESS_ONCE(flr_launch_->nr_shm_processed_sqn);

        ADK_LOG_WARN_AC_TF("trial pipe is broken",
                           "realtime total_order_sqn <{1}>, "
                           "trial total_order_sqn <{2}> shm_processed_sqn <{3}>",
                           total_order_sqn,
                           msg_total_sqn,
                           shm_total_sqn);
        if (total_order_sqn < shm_total_sqn)
        {
            // unlikely
            return true;
        }
        OnTrialError();
        ADK_LOG_INFO_AC_TF("create trial continue to run",
                           "shm_processd_sqn <{1}>",
                           total_order_sqn);
        return false;
    }
    return true;
}

// 实时进程发现试算进程异常后的处理逻辑
int32_t ShardingProxy::OnTrialError()
{
    assert(!is_trial_);   // only realtime process should call fork
    ADK_LOG_WARN_AC_TF("trial process error",
                        "begin create new trial process <{1}>",
                        flr_launch_->nr_shm_processed_sqn);
    bool is_exist_error_msg = false;
    // 1. 跳过异常消息
    if (flr_launch_->nr_msg_processed_sqn != flr_launch_->nr_msg_deliverd_sqn)
    {
        uint64_t total_order_sqn = 0;

        ShardingCtx::CtxData& ctx_data = sharding_ctx_->GetCtxData(false);
        adk::ContinueMemory* rx_cont_shm = ctx_data.rx_cont_shm;
        adk::ContinueMemory* trial_rx_cont_shm = sharding_ctx_->trial_rx_cont_shm();
        assert(flr_launch_->nr_msg_deliverd_sqn >= flr_launch_->nr_msg_processed_sqn + 1);

        adk::ContEntry* cont_entry = nullptr;
        rx_cont_shm->WaitEntry(&cont_entry);
        ShmRxMessage* rx_msg1 = (ShmRxMessage*)(cont_entry->GetBuffer());
        total_order_sqn = rx_msg1->total_order_sqn;
        assert(rx_msg1->header.msg_type == ShmMsgType::kAmiRxMsg);
        rx_cont_shm->FreeEntry(cont_entry);

        adk::ContEntry* cont_entry2 = nullptr;
        trial_rx_cont_shm->WaitEntry(&cont_entry2);
        ShmRxMessage* rx_msg2 = (ShmRxMessage*)(cont_entry2->GetBuffer());

        assert(rx_msg2->header.msg_type == ShmMsgType::kAmiRxMsg);
        assert(total_order_sqn == rx_msg2->total_order_sqn);
        assert(total_order_sqn == flr_launch_->nr_msg_deliverd_sqn);

        trial_rx_cont_shm->FreeEntry(cont_entry2);

        ADK_LOG_INFO_AC_TF("step over exception message",
                            "ami total_order_sqn <{1}>, msg_deliverd_sqn <{2}>,"
                            " shm_processed_sqn <{3}>",
                            total_order_sqn,
                            flr_launch_->nr_msg_deliverd_sqn,
                            flr_launch_->nr_shm_processed_sqn);
        assert(ctx_data.nr_shm_processed_sqn == flr_launch_->nr_shm_processed_sqn);

        // update message sqn
        flr_launch_->nr_msg_processed_sqn = total_order_sqn;
        ++ctx_data.nr_shm_processed_sqn;
        flr_launch_->nr_shm_processed_sqn = ctx_data.nr_shm_processed_sqn;
        assert(flr_launch_->nr_msg_deliverd_sqn == flr_launch_->nr_msg_processed_sqn);

        DiscardMessage(total_order_sqn);
        ProcessMessageDone(&ctx_data, total_order_sqn);
        last_discard_msg_sqn_ = total_order_sqn;
        is_exist_error_msg = true;
    }

    // 2. 创建新的试算进程
    auto ec = StartTrialProcess();
    assert(ec == aaf::ErrorCode::kSuccess);
    if (is_exist_error_msg)
    {
        for(auto* seq_lock : shm_seq_lock_vec_)
        {
            seq_lock->UnLock(last_discard_msg_sqn_, sharding_ctx_->sharding_index);
        }
    }
    return aaf::ErrorCode::kSuccess;
}

void ShardingProxy::HandExceptionSignal(int sig_num)
{
    if (!is_trial_)
    {
        SignalSafeLogger::Log(__LINE__,
                                "realtime receive exception signal", false);
        signal(sig_num, SIG_DFL);
        raise(sig_num);
        return;
    }

    if (flr_launch_)
    {
        SignalSafeLogger::Append("deliverd_sqn <");
        SignalSafeLogger::AppendInt(flr_launch_->nr_msg_deliverd_sqn);
        SignalSafeLogger::Append(">, processed_sqn <");
        SignalSafeLogger::AppendInt(flr_launch_->nr_msg_processed_sqn);
        SignalSafeLogger::Append(">, shm_processed_sqn <");
        SignalSafeLogger::AppendInt(flr_launch_->nr_shm_processed_sqn);
        SignalSafeLogger::Append(">, sharding_index <");
        SignalSafeLogger::AppendInt(flr_launch_->sharding_index);
        SignalSafeLogger::Append("> ");

        SignalSafeLogger::Log(__LINE__,
                                "catch exception message", false);

        ADK_BARRIER();
        flr_launch_->is_exception = true;
        close(trial_fds_[1]);
        signal(sig_num, SIG_DFL);
        raise(sig_num);
    }
    else
    {
        SignalSafeLogger::Log(__LINE__,
                              "trial receive exception signal",
                              false);
    }

    if (nr_start_trial_ > 1)
    {
        _exit(0);
    }
}

int32_t ShardingProxy::StartTrialProcess()
{
    assert(!is_trial_);   // only realtime process should call fork

    adk::Monitor::Suspend();
    close(trial_fds_[0]);

    if (pipe2(trial_fds_, O_NONBLOCK) != 0)
    {
        close(trial_fds_[0]);
        close(trial_fds_[1]);

        ADK_LOG_ERROR_AC_TF("pipe2 failed",
                            "errno = <{2}>, desc <{3}>",
                            errno,
                            strerror(errno));
        return aaf::ErrorCode::kFailure;
    }
    ++nr_start_trial_;
    ADK_LOG_INFO_AC_TF("start trial process",
                        "start counter <{1}>, shm_process_sqn begin <{2}>",
                        nr_start_trial_,
                        flr_launch_->nr_shm_processed_sqn);
    assert(flr_launch_->nr_msg_deliverd_sqn == flr_launch_->nr_msg_processed_sqn);

    pid_t child = fork();
    if (child < 0)
    {
        ADK_LOG_ERROR_AC_TF("fork failed", "errno: <{1}>", errno);
        // retry ?
        return aaf::ErrorCode::kFailure;
    }

    if (child == 0)
    {
        // child process
        close(trial_fds_[0]);  // close the read end
        close(sharding_ctx_->proxy_to_agent_fds[1]); // close the write end, 
                                        // follower agent only check realtime process is alive

        sccl_proxy_name_tri_ = sccl_proxy_name_ + "_tri";
        sccl_proxy_tri_ = adk::sccl::Proxy::Create(sharding_agent_->sccl_name_, sccl_proxy_name_tri_);
        if (sccl_proxy_tri_ == nullptr)
        {
            ADK_LOG_ERROR_AC_TF("shm cont channel create failed", "sccl proxy tri name: <{1}>", sccl_proxy_name_tri_);
            return aaf::ErrorCode::kFailure;
        }

        sccl_proxy_ = sccl_proxy_tri_;
        proxy_logger_->log_name_ = sharding_agent_->log_name_tri_shm_;
        close(proxy_logger_->log_file_);
        proxy_logger_->log_file_ = -1;

        is_trial_ = true;
        sharding_ctx_->TrialFlush();

        ADK_LOG_FORK();
        log_file_name_ = aaf_instance_->GetApplicationName() + "_trial";
        ADK_LOG_SET_THRESHOLD(aaf_instance_->GetLogLevel());
        SignalSafeLogger::s_logger_inst_->SetLogFile(aaf_instance_->GetLogDir(), log_file_name_);

        if (sharding_agent_->stop_save_point_ != nullptr)
        {
            sharding_agent_->stop_save_point_();
        }

        shm_data_manager_->DeatchAll();
        shm_data_manager_->TrialAttach();
        aaf::ShmDataManager::s_shm_segment_ = shm_data_manager_->app_data_shm();

        ADK_LOG_INFO_AC_TF("trial shm data address",
                            "seq_lock_shm <{1}>, app_data_shm <{2}>",
                            (void*)shm_data_manager_->seq_lock_shm()->get_address(),
                            (void*)shm_data_manager_->app_data_shm()->get_address());
    }
    else
    {
        close(trial_fds_[1]);  // close the write end

        if (trial_pid_ != 0)
        {
            ADK_LOG_INFO_AC_TF("fork new trial process",
                                "waitting previous processe <{1}> exit",
                                trial_pid_);
            waitpid(trial_pid_, NULL, 0);
        }
        trial_pid_ = child;
        adk::Monitor::Resume();
    }

    return aaf::ErrorCode::kSuccess;
}

void ShardingProxy::OnSignal(int sig_num, int value)
{
    // initiative exit
    bool is_signal_exit = IsQuitSignal(sig_num, value);

    if (is_signal_exit)
    {
        SignalSafeLogger::Append("initiative exit signal <");
        SignalSafeLogger::AppendInt(sig_num);
        SignalSafeLogger::Append(">, signal value <");
        SignalSafeLogger::AppendInt(value);
        SignalSafeLogger::Append(">, sharding_index <");
        SignalSafeLogger::AppendInt(sharding_index_);
        SignalSafeLogger::Append(">, is_trial <");
        SignalSafeLogger::AppendInt(is_trial_);
        SignalSafeLogger::Append(">");

        SignalSafeLogger::Log(__LINE__,
                                "exit signal", false);
        _exit(0);
    }
    else if (sig_num == SIGUSR1 || sig_num == SIGUSR2)
    {
        // other aaf signal
        return;
    }

    HandExceptionSignal(sig_num);
}

bool ShardingProxy::OnCollectIndicator(boost_ptree& indicator)
{
    boost_ptree& ind_tree = indicator.add_child("sharding_proxy", boost_ptree());
    if (aaf_instance_->GetHighAvailableContext() != nullptr)
    {
        auto& ctx_data = sharding_ctx_->GetCtxData(false);
        boost_ptree& ptree = ind_tree.push_back(boost_ptree::value_type("", boost_ptree()))->second;
        ptree.put("context_name", context_name_);
        ptree.put("deliver_msgs", ctx_data.nr_msg_delivered);
        ptree.put("process_msgs", ctx_data.nr_msg_processed);
        ptree.put("tx_msgs", ctx_data.nr_tx_msgs);
        ptree.put("shm_processd_msgs", ctx_data.nr_shm_processed_sqn);
    }
    if (aaf_instance_->GetSingletonContext() != nullptr)
    {
        auto& ctx_data = sharding_ctx_->GetCtxData(true);
        boost_ptree& ptree = ind_tree.push_back(boost_ptree::value_type("", boost_ptree()))->second;
        ptree.put("context_name", context_name_ + "_Singleton");
        ptree.put("deliver_msgs", ctx_data.nr_msg_delivered);
        ptree.put("process_msgs", ctx_data.nr_msg_processed);
        ptree.put("tx_msgs", ctx_data.nr_tx_msgs);
        ptree.put("shm_processd_msgs", ctx_data.nr_shm_processed_sqn);
    }

    if (sharding_agent_->is_calc_msg_latency_)
    {
        boost_ptree& latency_tree = indicator.add_child("proxy_latency", boost_ptree());
        ha_la_metric_.CollectIndicator(latency_tree, "ha_latency");

        if (aaf_instance_->GetSingletonContext() != nullptr)
        {
            sg_la_metric_.CollectIndicator(latency_tree, "sg_latency");
        }
    }

    return true;
}

int32_t ShardingTxHandler::SendMsg(const void* data, uint32_t len, int32_t partition_no, ami::TraceRecord record)
{
    ++nr_tx_msgs_;
    if (is_singleton_)
    {
        shd_proxy_->SendMsg<true>(data, len, partition_no, endpoint_id_, record);
    }
    else
    {
        shd_proxy_->SendMsg<false>(data, len, partition_no, endpoint_id_, record);
    }
    return aaf::ErrorCode::kSuccess;
}

}  // end of namespace sharding
