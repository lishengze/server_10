#ifndef AAF_CONFIG_KEY_H_
#define AAF_CONFIG_KEY_H_

namespace aaf
{
namespace config
{
const std::string kLogDirPath("LogDirPath");
const std::string kLockDirPath("LockDirPath");

const std::string kIsDisableContext("IsDisableContext");
const std::string kEnableHighAvailableContext("EnableHighAvailableContext");
const std::string kEnableSingletonContext("EnableSingletonContext");
const std::string kEnableRxEndpointsAutoCreation("EnableRxEndpointsAutoCreation");  ///> enable auto creation of static configured endpoints
const std::string kIsDisableFrameworkUse("IsDisableFrameworkUse");                  ///> disable the context usage in DomainServer
const std::string kEnableQueryShmChannel("EnableQueryShmChannel");
const std::string kEnableMonitorRequest("EnableMonitorRequest");
const std::string kEnableAppNameCheck("EnableAppNameCheck");

const std::string kMonitorRequestEndpoint("MonitorRequestEndpoint");                ///> default value "MonitorRequest"
const std::string kShmChannelPortRangeLow("ShmChannelPortRangeLow");
const std::string kShmChannelPortRangeHigh("ShmChannelPortRangeHigh");
const std::string kShmChannelPort("ShmChannelPort");                                ///> default value 33666

const std::string kContextEndpintBinding("ContextEndpintBinding");                  ///> endpoint-context pair, KVPairs
const std::string kRxEndpointsToCreate("RxEndpointsToCreate");                      ///> the receive endpoints to create, StringVectorValue
const std::string kConfigServerCluster("ConfigServerCluster");                      ///> ip:port vectors, StringVectorValue
const std::string kDomainServer("DomainServer");
const std::string kHighAvailableInitStatus("HighAvailableInitStatus");  ///< InitStatus of ha context
const std::string kSingletonInitStatus("SingletonInitStatus");          ///< InitStatus of singleton context
const std::string
    kIsHighAvailableFollowerContext("HighAvailableFollowerContext");
const std::string kIsSingletonFollowerContext("SingletonFollowerContext");
const std::string kIsSingletonLateJoinMcast("IsSingletonLateJoinMcast");
}  // namespace config
namespace ada
{
namespace config
{
const std::string kBackupDirectoryPath("BackupDirectoryPath");  //配置备份快照路径
const std::string kBackupThresholdNumberMessages("BackupThresholdNumberMessages");  //配置备份消息数量间隔
const std::string kBackupThresholdTimeInterval("BackupThresholdTimeInterval");  //配置备份消时间间隔
const std::string kSqlInstructionRxEndpoint("SqlInstructionRxEndpointName");  //配置远程SQL执行rx主题
const std::string kSqlInstructionTxEndpoint("SqlInstructionTxEndpointName");  //配置远程SQL执行tx主题
const std::string kSqlResultPageSize("SqlResultPageSize");  //配置远程SQL执行返回结果的一次页面大小
}   // namespace config
}   // namespace ada
}   // namespace aaf
#endif  // AAF_CONFIG_KEY_H_
