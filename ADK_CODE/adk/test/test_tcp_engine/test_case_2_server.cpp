#include "test_case.h"

class MessageHandlerImpl final : public MessageHandler<MessageHandlerImpl>
{
public:
    void OnMessage(struct MessageHeader* message)
    {
    }
};

class AcceptHandlerImpl : public AcceptHandler<AcceptHandlerImpl>
{
public:
    adk::io_engine::MessageHandler* OnAccept(TcpEndpoint* endpoint)
    {
        return new MessageHandlerImpl();
    }
};

int main(int argc, char* argv[])
{
    variables_map vm;
    if (!server_options_blank(argc, argv, vm))
    {
        return -1;
    }

    adk::Property engine_props;
    if (vm.count("message-ip"))
    {
        engine_props.SetValue(adk::io_engine::config::kMessageIp, vm["message-ip"].as<std::string>());
    }

    auto* const tcp_engine = adk::io_engine::TcpEngine::Create(engine_props);
    if (nullptr == tcp_engine)
    {
        std::cout << "create tcp engine failed" << std::endl;
        return -1;
    }

    const uint16_t listen_port = vm["listen-port"].as<uint16_t>();

    adk::Property accept_props;
    accept_props.SetValues()
        (adk::io_engine::config::acceptor::kListenPort, listen_port)
        (adk::io_engine::config::acceptor::kEventHandler, new EventHandlerServer)
        (adk::io_engine::config::acceptor::kAcceptHandler, new AcceptHandlerImpl);
    if (nullptr == tcp_engine->Accept(accept_props))
    {
        std::cout << "create acceptor failed, listen port <" << listen_port
                  << ">, error info <" << tcp_engine->GetLastError()
                  << ">" << std::endl;
        return -1;
    }

    std::string indicator;
    do 
    {
        sleep(3);
        tcp_engine->CollectIndicator(indicator);
        std::cout << indicator << std::endl;
    } while (true);

    adk::io_engine::TcpEngine::Destroy(tcp_engine);
    return 0;
}