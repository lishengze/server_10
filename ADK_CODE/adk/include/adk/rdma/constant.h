#ifndef ADK_IMPL_RDMA_CONSTANT_H_
#define ADK_IMPL_RDMA_CONSTANT_H_

// #define _ADK_RDMA_DEBUG_

#include <iostream>

#ifdef _ADK_RDMA_DEBUG_
#define PRINT_DEBUG(info) std::cout << info << std::endl;
#else
#define PRINT_DEBUG(info)
#endif

namespace adk_impl
{

namespace rdma
{

namespace constant
{

constexpr uint32_t kTimeoutMilli = 2000;

constexpr uint32_t kTxQueuePairDepth = 8192;
constexpr uint32_t kTxMessagePoolSize = kTxQueuePairDepth;

constexpr uint32_t kTxSignalBatchSize = 1024;
constexpr uint32_t kTxSignalBatchSizeMask = kTxSignalBatchSize - 1;
constexpr uint32_t kMaxTxRecycleSize = 32;

constexpr uint32_t kRxQueuePairDepth = 8192;
constexpr uint32_t kMaxRxBatchSize = 128;

constexpr uint32_t kListenBacklog = 8;

constexpr uint32_t IbvTxBits(uint32_t flag)
{
    return flag > 1 ? 1 + IbvTxBits(flag >> 1) : 0;
}

constexpr uint32_t kIbvSendInlineBits = IbvTxBits(IBV_SEND_INLINE);
constexpr uint32_t kIbvSendSignaledBits = IbvTxBits(IBV_SEND_SIGNALED);

}

}

}
#endif