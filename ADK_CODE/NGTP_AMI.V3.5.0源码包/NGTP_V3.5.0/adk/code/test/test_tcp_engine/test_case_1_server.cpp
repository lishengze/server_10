#include "test_case.h"

class MessageHandlerImpl final : public MessageHandler<MessageHandlerImpl>
{
public:
    MessageHandlerImpl(TcpEndpoint* endpoint)
    {
        save_counter_ = 0;
        tcp_endpoint_ = endpoint;
    }

    void OnMessage(struct MessageHeader* message)
    {
        clock_gettime(CLOCK_REALTIME, &(message->timestamp21));

        for (uint32_t offset = kMessageDataOffset; offset < message->message_len; offset += sizeof(uint64_t))
        {
            const auto recv_counter = *((uint64_t*)(((char*)message) + offset));
            if (recv_counter != save_counter_)
            {
                std::cout << "Bug on! expected value <" << save_counter_
                          << "> != recved value <" << recv_counter << ">" << std::endl;
                abort();
            }

            save_counter_ = recv_counter + 1;
        }

        clock_gettime(CLOCK_REALTIME, &(message->timestamp22));

        if (ADK_UNLIKELY(ErrorCode::kSuccess != tcp_endpoint_->SendMsg(message, message->message_len)))
        {
            std::cout << "SendMsg failed" << std::endl;
        }
    }

private:
    uint64_t     save_counter_;
    TcpEndpoint* tcp_endpoint_;
};

class AcceptHandlerImpl : public AcceptHandler<AcceptHandlerImpl>
{
public:
    adk::io_engine::MessageHandler* OnAccept(TcpEndpoint* endpoint)
    {
        return new MessageHandlerImpl(endpoint);
    }
};

int main(int argc, char* argv[])
{
    variables_map vm;
    if (!server_options(argc, argv, vm))
    {
        return -1;
    }

    adk::Property engine_props;
    engine_props.SetValues()
        (adk::io_engine::config::kIsRxLowLatency, vm["tx-low-latency"].as<bool>())
        (adk::io_engine::config::kIsTxLowLatency, vm["rx-low-latency"].as<bool>());

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
        (adk::io_engine::config::endpoint::kTxMinResidentMicro, 10000000)
        (adk::io_engine::config::endpoint::kRxMinResidentMicro, 10000000)
        (adk::io_engine::config::acceptor::kEventHandler, new EventHandlerServer)
        (adk::io_engine::config::acceptor::kAcceptHandler, new AcceptHandlerImpl)
        (adk::io_engine::config::endpoint::kTcpNoDelay, !!(vm.count("tcp-no-delay")))
        ;

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