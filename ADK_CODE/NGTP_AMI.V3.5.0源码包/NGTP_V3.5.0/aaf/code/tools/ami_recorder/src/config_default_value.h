/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_CONFIG_DEFAULT_VALUE_H_
#define AMI_CONFIG_DEFAULT_VALUE_H_

#include <adk/arch/generic.h>

namespace ami
{
namespace recorder
{
namespace cdv
{  // config default value

constexpr int kTxThreadNum(1);
constexpr const char* kDataPath("recorder_data");
constexpr unsigned long long kShmToUse(512ull * 1024u * 1024u);
constexpr unsigned long long kShmToUseULimit(64ull * 1024u * 1024u * 1024u);
constexpr unsigned long long kShmToUseLLimit(128ull * 1024u * 1024u);
constexpr size_t kSnapshotCycleMilli(3u * 1000u);
constexpr size_t kMsgQueueSize(8192u);
constexpr size_t kMsgQueueSizeLLimit(1u);
constexpr bool kUseMsgCRC(false);
constexpr unsigned int kWorkerDelayMicro(1);
const std::string kRecorderBinaryPath("");
constexpr int kRecorderPipeFD         = 998;
constexpr int kRecorderKeepalivePipeFD  = 999;
constexpr int kDiskRetryIntervalMilli = 100;

}
}
}  // namespace amk::recorder::cdv

#endif /* AMI_CONFIG_DEFAULT_VALUE_H_ */
