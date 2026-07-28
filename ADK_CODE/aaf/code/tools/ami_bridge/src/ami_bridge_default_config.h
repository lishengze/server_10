/**
 * @author 牛亮亮(niuliangliang@af.local)
 */

#ifndef AMI_BRIDGE_DEFAULT_VALUE_H_
#define AMI_BRIDGE_DEFAULT_VALUE_H_

namespace ami
{
namespace bridge
{
namespace cdv
{  // config default value

constexpr size_t kSnapshotCycleMilli      = 10u * 1000u;
constexpr size_t kTcpHeartbeatInvMilli    = 1000;
constexpr size_t kTcpHeartbeatInvMilliMin = 10;
constexpr size_t kTcpTimeoutMultiplier    = 5;
constexpr size_t kTcpTimeoutMultiplierMin = 2;
constexpr size_t kAmiRxMsgQLenMin         = 81920u;
constexpr size_t kPacketsRecordQLenMin    = 81920u;
constexpr size_t kTcpAckCntMax            = 8192u;

}
}
}  // namespace amk::bridge::cdv

#endif /* AMI_BRIDGE_DEFAULT_VALUE_H_ */
