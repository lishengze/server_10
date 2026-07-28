/**
 * @author t????(niuliangliang@af.local)
 */

#ifndef BRIDGE_BASE_H
#define BRIDGE_BASE_H

//< std
#include <string>         //< std::string
#include <unordered_map>  //< std::unordermap
#include <vector>
#include <chrono>  //< std::chrono
#include <thread>

//< ami adk aaf
#include <ami/message.h>  //< ami::Message
#include <aaf.h>          //< aaf::EndpointHandler
#include <adk/arch/generic.h>

#define BACKUP_NORMAL_WITH_LEADERL 0
#define BACKUP_FAST_THAN_LEADER 5
#define BACKUP_SLOW_THAN_LEADER 6
#define CONN_BREAK_AND_EXIT 7
#define SERVER_STEP_INIT 8

#ifdef TEST_FRAMEWORK_DEBUG
#include <ami_test_driver.h>
#define AMI_TD_SEND_EVENT(msg) test_driver::SendEvent(msg, 1000000)
#define AMI_TD_SEND_EVENT_ONCE(msg)           \
    static bool s_once_send = false;          \
    do                                        \
    {                                         \
        test_driver::SendEvent(msg, 1000000); \
        s_once_send = true;                   \
    } while (!s_once_send);
#define AMI_TD_SEND_EVENT_UNTIL(msg, value)   \
    static int32_t s_until_count = 0;         \
    while (++s_until_count == value)          \
    {                                         \
        test_driver::SendEvent(msg, 1000000); \
    }
#define SET_SIGNAL_HANDLE_FLAG(flag, value) ((flag) = (value))
#define SAVE_MSG(name, len, data)                   \
    std::ofstream out;                              \
    out.open(name, std::ios::out | std::ios::app);  \
    if (!out.is_open())                             \
    {                                               \
        ADK_LOG_ERROR_AC_TF("save msg failed", ""); \
    }                                               \
    else                                            \
    {                                               \
        std::string s(data, len);                   \
        out << s.c_str() << "\n";                   \
        out.flush();                                \
    }                                               \
    out.close();
#else
#define AMI_TD_SEND_EVENT(msg)
#define AMI_TD_SEND_EVENT_ONCE(msg)
#define AMI_TD_SEND_EVENT_UNTIL(msg, value)
#define SET_SIGNAL_HANDLE_FLAG(flag, value)
#define SAVE_MSG(name, len, data)
#endif

namespace ami
{
namespace bridge
{

#define BRIDGE_IDLE()                \
    if (AmiBridge::s_is_low_latency) \
    {                                \
        ADK_PAUSE();                 \
    }                                \
    else                             \
    {                                \
        usleep(0);                   \
    }

struct RxEndpointStuff
{
    Message::IDType ep_id;
    std::string ep_name;
    Message::SqnType msg_cnt      = 0;
    uint64_t total_payload_bytes  = 0;
    bool to_compress              = false;
    uint64_t total_cpayload_bytes = 0;

    RxEndpointStuff(const Message::IDType& _ep_id,
                    const std::string& _ep_name,
                    bool _to_compress)
        : ep_id(_ep_id),
          ep_name(_ep_name),
          to_compress(_to_compress)
    {
    }
};

struct TxEndpointStuff : public RxEndpointStuff
{
    bool is_loadbalance              = false;
    aaf::EndpointHandler* ep_handler = nullptr;

    TxEndpointStuff(const Message::IDType& _ep_id,
                    const std::string& _name,
                    bool _to_compress,
                    bool _is_loadbalance,
                    aaf::EndpointHandler* _ep_handler)
        : RxEndpointStuff(_ep_id, _name, _to_compress),
          is_loadbalance(_is_loadbalance),
          ep_handler(_ep_handler)
    {
    }
};

struct TransportStuff
{
    Message::IDType transport_id; /*transport id*/
    int32_t partition_no;

    TransportStuff(const Message::IDType& _tp_id,
                   int32_t _pno)
        : transport_id(_tp_id),
          partition_no(_pno)
    {
    }
};

struct PeerRxEndPointStuff
{
    Message::IDType ep_id;                        // 对端rx主题的endpoint id
    std::string ep_name;                          // 对端rx主题的enpoint name
    std::vector<int32_t> partitions;              // 对端rx主题的partition集合
    TxEndpointStuff* local_txep_stuff {nullptr};  // 本端同名tx主题的handler

    PeerRxEndPointStuff(const Message::IDType& id, const std::string& name,
                        std::vector<int32_t>& partition_vec, TxEndpointStuff* stuff)
                        : ep_id(id), ep_name(name)
                        , partitions(partition_vec), local_txep_stuff(stuff)
    {
    }
};

typedef std::unordered_map<
    Message::IDType, /*rx endpoint_id*/
    RxEndpointStuff>
    RxEndpointMapType;

typedef std::unordered_map<
    Message::IDType, /*tx endpoint_id*/
    TxEndpointStuff>
    TxEndpointMapType;

typedef std::unordered_map<
    Message::IDType, /*transport_id*/
    TransportStuff>
    TransportMapType;

typedef std::unordered_map<
    Message::IDType, /*endpoint_id*/
    std::vector<int32_t>>
    EndpointInfoMapType;

typedef std::unordered_map<
    Message::IDType, /*peer rx endpoint_id*/
    PeerRxEndPointStuff>
    PeerRxEndpointMapType;

enum TcpStatus
{
    kBroken = 0,  //????
    kConnecting,  //??????
    kConnected    //????
};

enum RoleStatus
{
    kError = 0,
    kLeader,  //??????
    kBackup   //??????
};

enum InitType
{
    kBootstrap = 0,
    kRecovery
};

enum RunType
{
    kUnknow = 0,
    kRestart,   //??
    kReconnect  //??
};

enum MsgType
{
    kErrorMsg = 0,
    kLocalSync,  //??????
    kPeerSync,   //??????
    kTcpAck,     //??????
    kAmiPacket,  //ami?????
};

}
}
#endif
