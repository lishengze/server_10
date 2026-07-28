#ifndef TCP_HANDLER_H
#define TCP_HANDLER_H

#include <adk/io_engine.h>
#include <adk/log_code_base.h>
#include <adk/log.h>

#include "bridge_msg.h"
#include "bridge_tier_channel.h"

namespace ami
{
namespace bridge
{

using adk::io_engine::Event;
//using adk::io_engine::Message;
//using adk::io_engine::Property;
using adk::io_engine::Acceptor;
using adk::io_engine::Endpoint;
using adk::io_engine::EventLevel;
using adk::io_engine::EventType;
using adk::io_engine::TcpEngine;

//声明AmiBridge类
class AmiBridge;

class TcpHeartbeatHandler : public adk::io_engine::HeartbeatHandler
{
public:
    TcpHeartbeatHandler(AmiBridge* pbridge);
    void SendHBMsg(Endpoint* endpoint);
    uint32_t GetPeriodMilli();
    AmiBridge* bridge_;

    static const int32_t kLogCodeHeartBeat = 91000;
    ADK_LOG_DECLARE_AC(kLogCodeHeartBeat);
};

class TcpEventHandler : public adk::io_engine::EventHandler
{
public:
    TcpEventHandler(AmiBridge* pbridge);
    void OnEvent(Endpoint* endpoint, Event* event) override;

    AmiBridge* bridge_;
    static const int32_t kLogCodeEnvent = 92000;
    ADK_LOG_DECLARE_AC(kLogCodeEnvent);
};

class TcpAcceptHandler : public adk::io_engine::AcceptHandler
{
public:
    TcpAcceptHandler(AmiBridge* pbridge);
    void OnAccept(Endpoint* endpoint, adk::io_engine::Property& ep_props) override;
    int32_t GetPeriodMilli();

    AmiBridge* bridge_;
    static const int32_t kLogCodeAccept = 93000;
    ADK_LOG_DECLARE_AC(kLogCodeAccept);
};

class TcpConnectHandler : public adk::io_engine::ConnectHandler
{
public:
    TcpConnectHandler(AmiBridge* pbridge);
    void OnConnect(Endpoint* endpoint, adk::io_engine::Property& ep_props) override;

    AmiBridge* bridge_;
    static const int32_t kLogCodeConnect = 94000;
    ADK_LOG_DECLARE_AC(kLogCodeConnect);
};

class TcpMessageHandler : public adk::io_engine::MessageHandler
{
public:
    TcpMessageHandler(AmiBridge* pbridge);
    int32_t CheckEndpoints(PeerSyncMsg* handshake_msg);
    int32_t OnMessage(adk::io_engine::Message* message) override;
    int32_t HandleSyncMsg(PeerSyncMsg* peer_sync_msg, uint32_t& recv_len);
    int32_t HandleAckMsg(TcpAckMsg* ack_msg, uint32_t& recv_len);
    int32_t HandleAmiMsg(AmiMsgPacket* rx_packet, uint32_t& recv_len);

    AmiBridge* bridge_;
    static const int32_t kLogCodeMessage = 95000;
    ADK_LOG_DECLARE_AC(kLogCodeMessage);
};

}
}
#endif
