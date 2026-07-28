#include "test_tcp_engine.h"



class EventHandler final : public EventHandlerBase
{
public:
};

class AcceptHandler final : public AcceptHandlerBase
{
public:

};

class ConnectHandler final : public ConnectHandlerBase
{
public:
};

class MessageHandler final : public MessageHandlerBase
{
public:
};

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cout << "please input the server ip and port" << std::endl;
        return -1;
    }

    EventHandler event_handler;
    MessageHandler message_handler;
    ConnectHandler connect_handler;
    HeartbeatHandlerBase heartbeat_handler;

    Property engine_props;
    engine_props(adk::io_engine::config::kTxThreadNum, 1)
        (adk::io_engine::config::kRxThreadNum, 1)
        (adk::io_engine::config::kEventHandler, &event_handler)
        (adk::io_engine::config::kConnectHandler, &connect_handler)
        (adk::io_engine::config::kMaxConnections, kMaxConnections)
        ;

    TcpEngine* const tcp_engine = TcpEngine::Create(engine_props);
    if (nullptr == tcp_engine)
    {
        std::cout << "Create tcp engine failed" << std::endl;
        return -1;
    }

    const string server_ip = argv[1];
    const uint16_t server_port = atoi(argv[2]);

    Property client_props;
    client_props(adk::io_engine::config::endpoint::kHeartbeatHandler, &heartbeat_handler)
                (adk::io_engine::config::endpoint::kMessageHandler, &message_handler)
                (adk::io_engine::config::endpoint::kRemoteIp, server_ip)
                (adk::io_engine::config::endpoint::kRemotePort, server_port)
        ;

    auto endpoint = tcp_engine->Connect(client_props);
    if (nullptr == endpoint)
    {
        std::cout << "connect to remote failed" << std::endl;
        return -1;
    }

    sleep(20);

    endpoint->Close();
    TcpEngine::Destroy(tcp_engine);
    return 0;;
}