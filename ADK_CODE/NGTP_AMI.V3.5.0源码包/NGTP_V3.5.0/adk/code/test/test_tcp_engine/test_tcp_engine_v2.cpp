#include "test_tcp_engine.h"

class EventHandler final : public EventHandlerBase
{
public:    
    void OnEvent(Endpoint* endpoint, Event* event)
    {
        EventHandlerBase::OnEvent(endpoint, event);
        
        if (event->type() == EventType::kConnectFailed)
        {
            endpoint->Close();

            Endpoint* const retry_endpoint = tcp_engine_->Connect(*client_props_);
            if (nullptr != retry_endpoint)
            {
                string detail = (format("Endpoint<%1%:%2%> OnEvent: retry to connect success")
                    % retry_endpoint->endpoint_id() % retry_endpoint->sub_index()).str();
                std::cout << detail << std::endl;
            }
            else
            {
                std::cout << "OnEvent: retry to connect failed" << std::endl;
            }
        }
    }

    TcpEngine* tcp_engine_;
    Property* client_props_;
};

class AcceptHandler final : public AcceptHandlerBase
{
public:

};

class ConnectHandler final : public ConnectHandlerBase
{
public:
    void OnConnect(Endpoint* endpoint, Property& ep_props) override
    {
        ConnectHandlerBase::OnConnect(endpoint, ep_props);
    }
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
    ConnectHandler connect_handler;

    Property client_props;
    client_props(adk::io_engine::config::endpoint::kRemoteIp, "127.0.0.1")
                (adk::io_engine::config::endpoint::kRemotePort, 55555)
                (adk::io_engine::config::endpoint::kIsSingleton, true)
                (adk::io_engine::config::endpoint::kEventHandler, &event_handler)
                (adk::io_engine::config::endpoint::kConnectHandler, &connect_handler)
                (adk::io_engine::config::endpoint::kMessageHandler, &message_handler)
                (adk::io_engine::config::endpoint::kRetryConnectTimes, 1)
        ;

    event_handler.tcp_engine_ = tcp_engine;
    event_handler.client_props_ = &client_props;

    Endpoint* endpoint1 = tcp_engine->Connect(client_props);
    if (nullptr == endpoint1)
    {
        std::cout << "asynchronous to connect failed" << std::endl;
    }

    Endpoint* endpoint2 = tcp_engine->Connect(client_props);
    if (nullptr == endpoint2)
    {
        std::cout << "asynchronous to connect failed" << std::endl;
    }

    while (true)
    {
        if (endpoint1->IsReady())
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