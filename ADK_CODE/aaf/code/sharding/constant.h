#pragma once

#include <stdint.h>

#include <string>


namespace sharding
{
constexpr int32_t  kInvalidSocket = -1;
constexpr int32_t kInvalidShardingIndex = -1;

constexpr int32_t kMaxShardingNum = 50;     // 最大允许分片数  

constexpr uint32_t kShardingContMemCount = 4;  // 2*rx + 2*tx, each sharding 2 context

constexpr uint32_t kTxContMemoryIndex = 0;
constexpr uint32_t kRxContMemoryIndex = 1;

constexpr uint32_t kEachContMemorySize = 16 * 1024 * 1024;
constexpr uint32_t kEachContReserveSize = 1 * 1024 * 1024;

constexpr uint32_t kShardingMemorySize = (kEachContMemorySize + kEachContReserveSize) * kShardingContMemCount;

constexpr uint32_t kIdleLoopCountLimit = 8192 * 8;

// 跟跑不可能使用2个Context，所以Rx方向共享内通道存使用单例的份额即可
constexpr uint32_t kContMemoryCount = 3;  // 2*rx + 1*tx

constexpr uint32_t kTotalMemorySize = (kEachContMemorySize + kEachContReserveSize) * kContMemoryCount;

}
