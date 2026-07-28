#include <aaf/sharding_channel.h>
#include "sharding_agent.h"
#include "sharding_proxy.h"

namespace aaf
{

ShardingChannel* g_sharding_channel = nullptr;

ShardingChannel::ShardingChannel()
{
    g_sharding_channel = this;
}


// sharing Proxy to Agent
int32_t ShardingChannel::Request(const void* data, uint32_t size)
{
    assert(proxy_);
    return proxy_->ShardingRequest(data, size);
}


// Agent to sharing Proxy
int32_t ShardingChannel::PostTo(const void* data, uint32_t size, int32_t sharding_index)
{
    assert(agent_);
    return agent_->ShardingPost(data, size, sharding_index);
}

}  // end of namespace sharding
