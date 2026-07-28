#include "test_verbs_perf.h"

class MessagePonger
{
public:
    MessagePonger(adk::variant::SPSCQueue<char*>* const queue)
    {
        buffer_ = nullptr;
        buffer_len_ = 0;
        message_queue_ = queue->Duplicate();
        assert(message_queue_);
    }

    void OnMessageWithNoBuffer(char* buffer, uint32_t buffer_size)
    {
        while (buffer_size >= ((MessageFormat*)buffer)->message_size)
        {
            const auto message_size = ((MessageFormat*)buffer)->message_size;
            char* const temp_buffer = new char[message_size];
            memcpy(temp_buffer, buffer, message_size);

            clock_gettime(CLOCK_MONOTONIC, &((MessageFormat*)temp_buffer)->timepoint2);
            message_queue_->Push(temp_buffer);

            buffer += message_size;
            buffer_size -= message_size;
        }

        if (buffer_size > 0)
        {
            assert(buffer_size > sizeof(((MessageFormat*)buffer)->message_size));
            buffer_ = new char[((MessageFormat*)buffer)->message_size];
            memcpy(buffer_, buffer, buffer_size);
            buffer_len_ = buffer_size;
        }
    }

    void OnMessage(char* buffer, uint32_t buffer_size)
    {
        if (0 == buffer_len_)
        {
            OnMessageWithNoBuffer(buffer, buffer_size);
        }
        else
        {
            const auto missing_len = ((MessageFormat*)buffer_)->message_size - buffer_len_;
            if (buffer_size >= missing_len)
            {
                memcpy(buffer_ + buffer_len_, buffer, missing_len);
                clock_gettime(CLOCK_MONOTONIC, &((MessageFormat*)buffer_)->timepoint2);
                message_queue_->Push(buffer_);

                buffer_len_ = 0;
                OnMessageWithNoBuffer(buffer + missing_len, buffer_size - missing_len);
            }
            else
            {
                memcpy(buffer_ + buffer_len_, buffer, buffer_size);
                buffer_len_ += buffer_size;
            }
        }
    }

private:
    char*    buffer_;
    uint32_t buffer_len_;
    adk::variant::SPSCQueue<char*>* message_queue_;
};

template<typename StackType, typename EndpointType, bool kParallel>
void TestBody(StackType* tcp_stack, EndpointType* endpoint)
{
    int32_t recv_result = -1;
    volatile bool is_running = true;

    constexpr uint32_t kBufferSize = 2048;
    char buffer[kBufferSize];

    std::thread tx_thread_hdl;
    if (kParallel)
    {
        auto* const resp_queue = adk::variant::SPSCQueue<char*>::Create("resp queue", 8192);
        assert(resp_queue);

        tx_thread_hdl = std::thread([&]() {
            char* buffer_ptr = nullptr;
            do 
            {
                if (adk::ErrorCode::kSuccess == resp_queue->Pop(buffer_ptr))
                {
                    const auto message_size = ((MessageFormat*)buffer_ptr)->message_size;
                    clock_gettime(CLOCK_MONOTONIC, &((MessageFormat*)buffer_ptr)->timepoint3);
                    const auto send_res = endpoint->Send(buffer_ptr, message_size);
                    delete buffer_ptr;

                    if (!CheckTxResult(send_res, message_size, endpoint))
                    {
                        is_running = false;
                        break;
                    }
                }
                else
                {
                    ADK_PAUSE();
                }
            } while (is_running);
        });

        MessagePonger message_ponger(resp_queue);
        do
        {
            recv_result = endpoint->Recv(buffer, kBufferSize);
            if (recv_result > 0)
            {
                message_ponger.OnMessage(buffer, recv_result);
            }

            if (!CheckRxResult(recv_result, endpoint))
            {
                is_running = false;
                break;
            }

        } while (is_running);
    }
    else
    {
        do 
        {
            if ((adk::verbs::ITcpStack::DriveMode::kReactor == EndpointType::drive_mode())
                && (0 == tcp_stack->ReactorPerform()))
            {
                continue;
            }

            if (EndpointType::kZcRecvSupport)
            {
                recv_result = endpoint->ZcRecv([&](char* zc_buffer, ssize_t buffer_size) {
                    if (!CheckTxResult(endpoint->Send(zc_buffer, buffer_size), buffer_size, endpoint))
                    {
                        is_running = false;
                    }
                });
            }
            else
            {
                recv_result = endpoint->Recv(buffer, kBufferSize);
                if (recv_result > 0)
                {
                    if (!CheckTxResult(endpoint->Send(buffer, recv_result), recv_result, endpoint))
                    {
                        is_running = false;
                        break;
                    }
                }
            }

            if (!CheckRxResult(recv_result, endpoint))
            {
                is_running = false;
                break;
            }

        } while (is_running);
    }

    if (tx_thread_hdl.joinable())
    {
        tx_thread_hdl.join();
    }
}

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("message-ip", boost::program_options::value<std::string>()->default_value(std::string()), "set message ip")
        ("listen-port", boost::program_options::value<uint16_t>()->default_value(50000), "set listen port")
        ("io-parallel", "set send/recv parallel")
        ;

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    if (vm.count("help"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    const std::string message_ip = vm["message-ip"].as<std::string>();
    auto* const tcp_stack = adk::verbs::ITcpStack::Create(message_ip);
    if (nullptr == tcp_stack)
    {
        std::cout << (boost::format("Create tcp stack failed with message ip <%1%>") 
                                    % message_ip).str() << std::endl;
        return 0;
    }

    auto* const tcp_epoller = adk::verbs::ITcpEPoller::Create(tcp_stack,
                                                              adk::verbs::ITcpEPoller::PollerType::kControl);
    if (nullptr == tcp_epoller)
    {
        std::cout << "Create tcp epoller failed" << std::endl;
        return 0;
    }

    const auto listen_port = vm["listen-port"].as<uint16_t>();
    auto* const acceptor = adk::verbs::ITcpAcceptor::Create(tcp_stack, std::string(), listen_port, true, true);
    if (nullptr == acceptor)
    {
        std::cout << (boost::format("Create acceptor <%1%:%2%> failed")
                                    % std::string()
                                    % listen_port).str() << std::endl;
        return 0;
    }

    if (!tcp_epoller->EPollAdd(acceptor, acceptor))
    {
        std::cout << "EPollAdd acceptor failed" << std::endl;
        return 0;
    }

    while (true)
    {
        struct epoll_event event;
        if ((tcp_stack->ReactorPerform() > 0) && (1 == tcp_epoller->EPollWait(&event, 1, 1000)))
        {
            auto* const endpoint = acceptor->Accept();
            if (nullptr == endpoint)
            {
                continue;
            }

            std::cout << (boost::format("Accept endpoint <%1%:%2%>-<%3%:%4%>")
                                        % endpoint->local_ip()
                                        % endpoint->local_port()
                                        % endpoint->remote_ip()
                                        % endpoint->remote_port()).str() << std::endl;

            switch (tcp_stack->stack_type())
            {
            case adk::verbs::ITcpStack::StackType::kStackSk:
                if (vm.count("io-parallel"))
                {
                    TestBody<adk::verbs::TcpStackSk, 
                             adk::verbs::TcpEndpointSk, 
                             true>(static_cast<adk::verbs::TcpStackSk*>(tcp_stack),
                                   static_cast<adk::verbs::TcpEndpointSk*>(endpoint));
                }
                else
                {
                    TestBody<adk::verbs::TcpStackSk, 
                             adk::verbs::TcpEndpointSk, 
                             false>(static_cast<adk::verbs::TcpStackSk*>(tcp_stack),
                                    static_cast<adk::verbs::TcpEndpointSk*>(endpoint));
                }
                break;
            case adk::verbs::ITcpStack::StackType::kStackZf:
                TestBody<adk::verbs::TcpStackZf, 
                         adk::verbs::TcpEndpointZf, 
                         false>(static_cast<adk::verbs::TcpStackZf*>(tcp_stack),
                                static_cast<adk::verbs::TcpEndpointZf*>(endpoint));
                break;
            default:
                std::cout << (boost::format("Tcp stack type <%1%> unknown with message ip <%2%>")
                                            % static_cast<int32_t>(tcp_stack->stack_type())
                                            % message_ip).str() << std::endl;
            }

            std::cout << (boost::format("Connect <%1%:%2%>-<%3%:%4%> broken")
                                        % endpoint->local_ip()
                                        % endpoint->local_port()
                                        % endpoint->remote_ip()
                                        % endpoint->remote_port()).str() << std::endl;
            adk::verbs::ITcpEndpoint::Destroy(endpoint);
        }
    }

    return 0;
}