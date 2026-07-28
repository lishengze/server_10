#include "test_tcp_engine.h"

class EventHandler final : public EventHandlerBase
{
public:
    void OnEvent(Endpoint* endpoint, Event* event) override
    {
        EventHandlerBase::OnEvent(endpoint, event);

        if (EventLevel::kError == event->level())
        {
            endpoint->Close();
        }
    }
};

class MessageHandler final : public adk::io_engine::MessageHandler
{
public:
    int32_t OnMessage(Message* message) override
    {
        std::cout << "OnMessage: message length <" << message->data_len() << ">" << std::endl;
        message->forward_acquire();
        message->Reply(message);        
        return ErrorCode::kSuccess;
    }
};

class AcceptHandler final : public AcceptHandlerBase
{
public:
    AcceptHandler()
    {
        accept_counter_ = 0;
    }

    void OnAccept(Endpoint* endpoint, Property& ep_props) override
    {
        ep_props(adk::io_engine::config::endpoint::kMessageHandler, new MessageHandler);
        AcceptHandlerBase::OnAccept(endpoint, ep_props);

        Property accept_props;
        accept_props(adk::io_engine::config::endpoint::kListenIp, "0.0.0.0")
                    (adk::io_engine::config::endpoint::kListenPort, listen_port_)
                    (adk::io_engine::config::endpoint::kReuseAddr, true);

        if (nullptr == tcp_engine_->Accept(accept_props))
        {
            std::cout << "AcceptHandler::OnAccept Accept failed" << std::endl;
        }

        ++accept_counter_;
    }
    
    TcpEngine* tcp_engine_;
    uint16_t   listen_port_;
    int32_t    accept_counter_;
};

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "please input the recver port" << std::endl;
        return -1;
    }

    EventHandler event_handler;
    AcceptHandler accept_handler;

    Property props;
    props(adk::io_engine::config::endpoint::kEventHandler, &event_handler)
         (adk::io_engine::config::endpoint::kAcceptHandler, &accept_handler)
        ;
    TcpEngine* const tcp_engine = TcpEngine::Create(props);
    if (nullptr == tcp_engine)
    {
        std::cout << "create tcp engine failed" << std::endl;
        return -1;
    }

    uint16_t listen_port = (uint16_t)atoi(argv[1]);

    accept_handler.tcp_engine_ = tcp_engine;
    accept_handler.listen_port_ = listen_port;

    Property accept_props;
    accept_props(adk::io_engine::config::endpoint::kListenIp, "0.0.0.0")
                (adk::io_engine::config::endpoint::kListenPort, listen_port)
                (adk::io_engine::config::endpoint::kReuseAddr, true)
                (adk::io_engine::config::endpoint::kEventHandler, &event_handler)
                (adk::io_engine::config::endpoint::kAcceptHandler, &accept_handler)
        ;

    if (nullptr == tcp_engine->Accept(accept_props))
    {
        std::cout << "create acceptor failed" << std::endl;
    }

    while (accept_handler.accept_counter_ < 1000)
    {
        sleep(1);
    }

    TcpEngine::Destroy(tcp_engine);
    return 0;
}
