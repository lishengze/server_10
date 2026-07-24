#include <aaf/sharding_shm_manager.h>
#include "sharding_agent.h"
#include "sharding_proxy.h"

namespace aaf
{

boost::interprocess::managed_shared_memory* ShmDataManager::s_shm_segment_ = nullptr;

void ShardingSeqLock::Lock()
{
    assert(shm_seq_lock_ && sharding_ctx_);
    auto& ctx_data = sharding_ctx_->GetCtxData(false);
    shm_seq_lock_->Lock(static_cast<int64_t>(ctx_data.last_deliver_msg_sqn),
                        sharding_ctx_->sharding_index);
}

void ShardingSeqLock::UnLock()
{
    auto& ctx_data = sharding_ctx_->GetCtxData(false);
    shm_seq_lock_->UnLock(static_cast<int64_t>(ctx_data.last_deliver_msg_sqn),
                          sharding_ctx_->sharding_index);
}

ShmDataManager* ShmDataManager::GetInstance()
{
    static ShmDataManager s_shm_data_mgr_inst;
    return &s_shm_data_mgr_inst;
}

ShardingSeqLock* ShmDataManager::CreateSeqLock(const std::string& name, uint32_t cache_size)
{
    ShardingSeqLock* sharding_seq_lock = sharding_agent_->CreateSeqLock(name, cache_size);
    return sharding_seq_lock;
}

void* ShmDataManager::Allocate(uint32_t size)
{
    return s_shm_segment_->allocate(size);
}

void ShmDataManager::Deallocate(void* ptr)
{
    s_shm_segment_->deallocate(ptr);
}

}  // end of namespace sharding
