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

int main()
{
    Property props;
    TcpEngine* const tcp_engine = TcpEngine::Create(props);
    if (nullptr == tcp_engine)
    {
        std::cout << "create tcp engine failed" << std::endl;
        return -1;
    }

    EventHandler event_handler;
    AcceptHandler accept_handler;    
    MessageHandler message_handler;

    Property server_props;
    server_props(adk::io_engine::config::endpoint::kListenIp, "127.0.0.1")
                (adk::io_engine::config::endpoint::kListenPort, 55555)
                (adk::io_engine::config::endpoint::kEventHandler, &event_handler)
                (adk::io_engine::config::endpoint::kAcceptHandler, &accept_handler)
                (adk::io_engine::config::endpoint::kMessageHandler, &message_handler)
        ;

    Acceptor* acceptor = tcp_engine->Accept(server_props);
    if (nullptr == acceptor)
    {
        std::cout << "asynchronous to accept failed" << std::endl;
    }

    ConnectHandler connect_handler;

    Property client_props;
    client_props(adk::io_engine::config::endpoint::kRemoteIp, "127.0.0.1")
                (adk::io_engine::config::endpoint::kRemotePort, 55555)
                (adk::io_engine::config::endpoint::kIsSingleton, false)
                (adk::io_engine::config::endpoint::kEventHandler, &event_handler)
                (adk::io_engine::config::endpoint::kConnectHandler, &connect_handler)
                (adk::io_engine::config::endpoint::kMessageHandler, &message_handler)
        ;

    Endpoint* endpoint2 = tcp_engine->Connect(client_props);
    if (nullptr == endpoint2)
    {
        std::cout << "asynchronous to connect failed" << std::endl;
    }

    while (true)
    {
        if (endpoint2->IsReady())
        {
            break;
        }
        sleep(1);
    }

    uint64_t send_data = 0;
    while (true)
    {        
        Message* const message = endpoint2->NewMessage(kPayloadSize);
        assert(message);

        *((uint64_t*)(message->data())) = send_data;
        send_data += 1;

        message->set_data_len(kPayloadSize);

    retry:
        const auto ec = endpoint2->SendMsg(message);
        if (ADK_UNLIKELY(ErrorCode::kSuccess != ec))
        {
            goto retry;
        }
    }
    
    TcpEngine::Destroy(tcp_engine);
    return 0;
}
