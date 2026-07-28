#include <adk/pipeline.h>
#include <adk_pack/pipeline_sharding.h>
#include <adk/util.h>
#include <adk/arch/generic.h>
#include <adk/shm.h>
#include <boost/filesystem.hpp>
#include <sched.h>
#include <unistd.h>
#include <sys/types.h>

namespace adk_impl
{

namespace bf = boost::filesystem;

StageNameMaker g_stage_name_maker;

std::mutex* Pipeline::s_checkpoint_mut_ = new std::mutex();

static inline std::string MakePipelineShmName(const std::string& department,
                                              const std::string& pipeline_name)
{
    return GetLoginUserName() + "_pipeline_" + department + "_" + pipeline_name;
}

void DoChangeToRealtime(int32_t priority, int32_t policy)
{
    if (priority == 0)
        return ;

    struct sched_param param;
    memset(&param, 0x00, sizeof(param));
    
    param.sched_priority = priority;
    ::sched_setscheduler(0, policy, &param);
}

void DoRestoreToOther()
{
    struct sched_param param;
    memset(&param, 0x00, sizeof(param));

    param.sched_priority = 0;
    ::sched_setscheduler(0, SCHED_OTHER, &param);
}

std::string DoChangeCpuAffinity(const std::string& core_list)
{
    std::string orig_cpu_list;
    if (GetCpuAffinity(orig_cpu_list) != ErrorCode::kSuccess)
    {
        return orig_cpu_list;
    }

    SetCpuAffinity(core_list);

    return orig_cpu_list;
}

void DoRestoreCpuAffinity(const std::string& core_list)
{
    if (core_list.empty())
    {
        return;
    }
    
    SetCpuAffinity(core_list);
}

std::string GetPipelinePath()
{
    const char* pipeline_path_env =std::getenv("ADK_PIPELINE_PATH");

    std::string record_path;
    if (pipeline_path_env != nullptr)
    {
        record_path = std::string(pipeline_path_env);
        record_path.append("/pipeline/");
    }
    else
    {
        record_path = GetLoginUserHome() + "recorder_data/pipeline/";
    }

    return record_path;
}

const std::string& Pipeline::GetLastError()
{
    return last_err_;
}

void Pipeline::SetLastError(const std::string& err)
{
    last_err_ = err;
}

Pipeline::Pipeline(const std::string& department, const std::string& pipeline_name, uint32_t bussiness_size)
    : use_pipeline_shm_(true),
      business_info_size_(bussiness_size),
      department_(department),
      pipeline_name_(pipeline_name)
{
}

int32_t Pipeline::BackupCheckPoint(const std::string& record_path, const std::string& pipeline_shm_name)
{
    char output[80];
    boost::system::error_code ec;
    memset(output, 0x00, sizeof(output));
    std::time_t now = std::time(nullptr);
    const size_t str_len = std::strftime(output,
                                         sizeof(output),
                                         "%Y-%m-%d_%H_%M_%S",
                                         std::localtime(&now));
    std::string backup_time(output, str_len);

    // record_path/ATP_TE_Cash/${Date}/
    bf::path backup_path = bf::path(record_path)
        / bf::path(department_ + "_" + pipeline_name_)
        / bf::path(backup_time);
    
    bf::create_directories(backup_path, ec);
    if (ec)
    {
        SetLastError("create backup directory <" + backup_path.string() + "> failed:" + ec.message());
        return ErrorCode::kFailure;
    }
    bf::copy_file("/dev/shm/" + pipeline_shm_name,
                  backup_path / pipeline_shm_name,
                  bf::copy_option::overwrite_if_exists);
    return ErrorCode::kSuccess;
}

int32_t Pipeline::InitPipelineShm()
{
    const std::string pipeline_shm_name = MakePipelineShmName(department_, pipeline_name_);

    const uint32_t hdr_size = ADK_ROUND_UP(sizeof(ShmCheckPointHeader), ADK_PAGE_SIZE);
    const uint32_t checkpoint_size = ADK_ROUND_UP((business_info_size_ + kShmCheckPointSize), ADK_PAGE_SIZE);

    const uint32_t shm_size = hdr_size + ADK_ROUND_UP(checkpoint_size * nr_checkpoints_, ADK_PAGE_SIZE);

    std::string pipeline_path = GetPipelinePath();

    std::lock_guard<std::mutex> checkpoint_lock(*s_checkpoint_mut_);
    auto cur_shm_size = adk_impl::ShmFactory::Size(pipeline_shm_name); // get the current shm size

    if (cur_shm_size != -1ul)        // shm object exist
    {
        if (BackupCheckPoint(pipeline_path, pipeline_shm_name) != ErrorCode::kSuccess)
        {
            return ErrorCode::kFailure;
        }
        if (adk_impl::ShmFactory::Destroy(pipeline_shm_name) != 0)
        {
            SetLastError("Destroy expired share memory failed, name=" + pipeline_shm_name);
            return ErrorCode::kFailure;
        }
    }

    shm_checkpoint_hdr_ = (ShmCheckPointHeader*)ShmFactory::Create(pipeline_shm_name, shm_size);
    if (nullptr == shm_checkpoint_hdr_)
    {
        SetLastError("Create share memory failed, name=" + pipeline_shm_name);
        return ErrorCode::kFailure;
    }
    else
    {
        std::memset((char*)shm_checkpoint_hdr_, 0, shm_size);
        shm_checkpoint_hdr_->Initialize(nr_checkpoints_, checkpoint_size);
        shm_checkpoint_hdr_->check_points_offset = hdr_size;
    }

    return ErrorCode::kSuccess;
}

bool Pipeline::Start()
{
    if (!pending_list_.empty())
        return false;

    if (use_pipeline_shm_ && !adk::pipeline::sharding::g_is_shading)
    {
        nr_checkpoints_ = stage_workers_.size();
        if (InitPipelineShm() != ErrorCode::kSuccess)
        {
            return false;
        }
    }

    for (auto it = stage_workers_.begin(); it != stage_workers_.end(); ++it)
    {
        if (it->second->prev_stage_.empty())
        {
            entrance_stages_.insert(it->first);
        }
    }

    for (auto it = stage_workers_.begin(); it != stage_workers_.end(); ++it)
    {
        if (!(it->second->InitThunk()))
        {
            return false;
        }
    }

    std::lock_guard<std::mutex> checkpoint_lock(*s_checkpoint_mut_);
    for (auto it = stage_workers_.begin(); it != stage_workers_.end(); ++it)
    {
        it->second->ChangeToRealtime();
        std::string core_list = it->second->ChangeCpuAffinity();

        if (!it->second->is_inplace())
        {
            boost::thread* ptr = new boost::thread();
            *ptr               = boost_thread(it->second->get_thread_name().c_str(),
                                "stage worker thread",
                                boost::bind(&IPrevStageWorker::WrapRun, it->second));
            it->second->set_worker_thread(ptr);
        }

        if (nr_checkpoints_ > 0)
        {
            if (!it->second->is_inplace())
            {
                while (!it->second->is_running_)
                {
                    usleep(0);
                }
            }

            ShmCheckPoint* chk_point = shm_checkpoint_hdr_->NewCheckPoint(it->first);
            chk_point->thread_id = it->second->tid_;
            chk_point->business_info_size = business_info_size_;
            it->second->shm_chk_point_ = chk_point;
            
        }
        DoRestoreToOther();
        DoRestoreCpuAffinity(core_list);
    }
    return true;
}

} // adk
