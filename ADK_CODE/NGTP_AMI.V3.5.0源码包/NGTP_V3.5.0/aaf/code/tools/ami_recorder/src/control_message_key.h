/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_CONTROL_MESSAGE_KEY_H_
#define AMI_CONTROL_MESSAGE_KEY_H_

///< cpp std
#include <string>

namespace ami
{

const std::string kMessageType("MessageType");
const std::string kCreateMergeChannels("CreateMergeChannels");
const std::string kCreateMessageChannel("CreateMessageChannel");
const std::string kCreateStatusChannel("CreateStatusChannel");
const std::string kPath("Path");
const std::string kDataPath("DataPath");
const std::string kAppMsgMPName("AppMsgMPName");
const std::string kMQManagerName("MQManagerName");
const std::string kOriginQueueIndex("OriginQueueIndex");
const std::string kRepairQueueIndex("RepairQueueIndex");
const std::string kDataQueueIndex("DataQueueIndex");
const std::string kStatusQueueIndex("StatusQueueIndex");
const std::string kWeight("Weight");
const std::string kMsgQueueSize("MsgQueueSize");
const std::string kContextName("ContextName");
const std::string kIsAgentRecovery("IsAgentRecovery");
const std::string kDynamicSyncIdMaps("DynamicSyncIdMaps");

struct ControlMessageType
{
    static const int kInvalidMessage         = 0;
    static const int kConnectToRecorder      = 1;
    static const int kCreateMergeChannels    = 2;
    static const int kCreateMessageChannel   = 3;
    static const int kCreateStatusChannel    = 4;
    static const int kActionFail             = 5;
    static const int kConnectToExistRecorder = 6;
    static const int kDynamicSyncIdMaps      = 7;
};

}  //namespace ami

#endif /* AMI_CONTROL_MESSAGE_KEY_H_ */
