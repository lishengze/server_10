#include <time.h>
#include <string.h>

#include <string>
#include <thread>
#include <iostream>
#include <typeinfo>
#include <boost/format.hpp>
#include <boost/program_options.hpp>

#include <adk/token_buckets.h>
#include <tcp_verbs/tcp_socket.h>
#include <tcp_verbs/tcp_direct_zf.h>
#include <tcp_verbs/tcp_interface.h>

template<typename EndpointType>
void DoTest(adk::verbs::ITcpStack* stack, adk::verbs::ITcpEndpoint* accept_endpoint)
{
    auto* const endpoint = static_cast<EndpointType*>(accept_endpoint);
    auto* const tcp_stack = static_cast<typename EndpointType::StackType*>(stack);

    constexpr uint32_t kBufferSize = 1024;
    char buffer[kBufferSize];
    int32_t recv_result = -1;
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
                endpoint->Send(zc_buffer, buffer_size);
            });
        }
        else
        {
            recv_result = endpoint->Recv(buffer, kBufferSize);
            if (recv_result > 0)
            {
                endpoint->Send(buffer, recv_result);
            }
        }
    } while (0 != recv_result);

    std::cout << (boost::format("Endpoint <%1%:%2%>-<%3%:%4%> recv EOF") 
                                % endpoint->local_ip()
                                % endpoint->local_port()
                                % endpoint->remote_ip()
                                % endpoint->remote_port()).str() << std::endl;
}

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("message-ip", boost::program_options::value<std::string>()->default_value(std::string()), "set message ip")
        ("listen-port", boost::program_options::value<uint16_t>()->default_value(50000), "set remote port")
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

    std::cout << (boost::format("Create tcp stack <%1%> success") % tcp_stack).str() << std::endl;
    auto* const tcp_epoller = adk::verbs::ITcpEPoller::Create(tcp_stack, 
                                                              adk::verbs::ITcpEPoller::PollerType::kControl);
    if (nullptr == tcp_epoller)
    {
        std::cout << "Create tcp epoller <Control> failed" << std::endl;
        return 0;
    }

    std::cout << (boost::format("Create tcp epoller <%1%> success") % tcp_epoller).str() << std::endl;

    const auto listen_port = vm["listen-port"].as<uint16_t>();
    auto* acceptor = adk::verbs::ITcpAcceptor::Create(tcp_stack, std::string(), listen_port, true, true);
    if (nullptr == acceptor)
    {
        std::cout << (boost::format("Create acceptor <%1%:%2%> failed") 
                                    % message_ip 
                                    % listen_port).str() << std::endl;
        return 0;
    }

    std::cout << (boost::format("Create acceptor <%1%> <%2%:%3%> success") 
                                % acceptor
                                % message_ip 
                                % listen_port).str() << std::endl;

    if (!tcp_epoller->EPollAdd(acceptor, acceptor))
    {
        std::cout << "EPollAdd acceptor failed" << std::endl;
        return 0;
    }

    adk::verbs::ITcpEndpoint* accept_endpoint = nullptr;
    while (true)
    {
        struct epoll_event event;
        if ((tcp_stack->ReactorPerform() > 0) && (1 == tcp_epoller->EPollWait(&event, 1, 1000)))
        {
            accept_endpoint = acceptor->Accept();
            std::cout << (boost::format("Accept endpoint <%1%:%2%>-<%3%:%4%>") 
                                        % accept_endpoint->local_ip()
                                        % accept_endpoint->local_port()
                                        % accept_endpoint->remote_ip()
                                        % accept_endpoint->remote_port()).str() << std::endl;


            if (nullptr != accept_endpoint)
            {
                if (adk::verbs::ITcpStack::StackType::kStackSk == tcp_stack->stack_type())
                {
                    DoTest<adk::verbs::TcpEndpointSk>(tcp_stack, accept_endpoint);
                }
                else if (adk::verbs::ITcpStack::StackType::kStackZf == tcp_stack->stack_type())
                {
                    DoTest<adk::verbs::TcpEndpointZf>(tcp_stack, accept_endpoint);
                }
                else
                {
                    std::cout << (boost::format("Tcp stack type <%1%> unknown with message ip <%2%>") 
                                                % static_cast<int32_t>(tcp_stack->stack_type())
                                                % message_ip).str() << std::endl;
                }

                adk::verbs::ITcpEndpoint::Destroy(accept_endpoint);
            }
        }
    }

    return 0;
}