#pragma once

#include <time.h>
#include <stdint.h>

namespace sharding
{

const uint32_t module_name_len_max = 64;
const uint32_t function_name_len_max = 96;

enum ShmMsgType
{
    kAmiRxMsg=0,    // AMI -> CTE
    kAmiTxMsg,      // CTE -> AMI
    kShmProcessDoneMsg, // CTE -> AMI
    kShmDiscardMsg, // CTE -> AMI
    kAmiEvent,
    kShardingReq,   // sharding proxy -> agent
    kShardingPost,   // sharding agent -> proxy
};

enum ShmEventType
{
    kOnRoleChangeToLeader=0,
    kOnRoleChangeToMember,
    kOnRoleChangeToMaster,
    kOnRecoveryBegin,
    kOnRecoverySuccess,
    kOnNoReceiver,
    kOnReceiverUp,
    kOnDiscardMessage,
};

struct ShmAgentHeader
{
    uint32_t msg_type;
    uint32_t msg_len;
};

struct ShmAmiEvent
{
    ShmAgentHeader  header;
    ShmEventType    evt_type;
    char            event_body[];
    /*{
        "type" : ...,
        "level" : ...,
        "what" : "...",
        "property" : {
            ...
        }
    }*/
};

struct ShmRxMessage
{
    ShmAgentHeader  header;
    uint32_t        endpoint_id;
    int32_t         partition_no;
    uint32_t        transport_id;
    uint32_t        msg_tag;
    uint64_t        trace_record;
    uint64_t        total_order_sqn;
    uint64_t        topic_sqn;
    uint64_t        cont_topic_sqn;
    uint64_t        cont_endpoint_sqn;
    int64_t         send_ts;
    char            msg_body[];
};

struct ShmTxMessage
{
    ShmAgentHeader  header;
    uint32_t        endpoint_id;
    int32_t         partition_no;
    // 轨迹追踪标识 不启用可填0
    uint64_t        trace_record;
    int64_t         send_ts;
    char            msg_body[];
};

struct ShmProcessDoneMsg
{
    ShmAgentHeader  header;
    uint64_t        total_order_sqn;    // ProcessMessageDone
};

struct ShmDiscardMsg
{
    ShmAgentHeader  header;
    uint64_t        total_order_sqn;    // message to discard
};

// sharding proxy to agent
struct ShmShardingReq
{
    ShmAgentHeader  header;
    int32_t         sharding_idx;

    char            msg_body[];
};

enum ProxyInfoType
{
    kLogInfo=0,
    kLogInfoTri,
    kInidcateInfo,
};

struct ProxyInfoHead
{
    uint32_t info_type;
};
struct ProxyInfoLog
{
    ProxyInfoHead info_head;

    pid_t pid;
    pid_t tid;

    uint32_t level;
    uint32_t code;

    char module_name[module_name_len_max + 1];
    char function_name[function_name_len_max + 1];

    uint32_t src_line;

    uint64_t title_len;
    uint64_t message_len;
    char title_message[];
};
struct ProxyInfoInd
{
    ProxyInfoHead info_head;

    int32_t sharding_index;
    uint64_t body_len;
    char info_body[];
};

}   // end of namespace sharding
