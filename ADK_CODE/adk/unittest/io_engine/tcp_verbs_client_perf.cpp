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

#define ACCESS_ONCE(x) (*(volatile decltype(x) *)&(x))

volatile bool g_reset_flag = false;
uint64_t g_counter = 0;
uint64_t g_time_diff_min = 0xffffffff;
uint64_t g_time_diff_max = 0;
uint64_t g_time_diff_total = 0;

template<typename EndpointType>
void DoPingpongTest(adk::verbs::ITcpStack* stack, 
                    adk::verbs::ITcpEndpoint* client_endpoint, 
                    uint32_t message_size)
{
    message_size = std::max<uint32_t>(message_size, sizeof(struct timespec));
    std::cout << (boost::format("ping-pong | endpoint type <%1%> | message size <%2%>") 
                                % typeid(EndpointType).name()
                                % message_size).str() << std::endl;

    char* buffer = new char[message_size];
    auto* const endpoint = static_cast<EndpointType*>(client_endpoint);
    auto* const tcp_stack = static_cast<typename EndpointType::StackType*>(stack);

    struct timespec current_tp;

    do
    {
        if (g_reset_flag)
        {
            g_counter = 0;
            g_time_diff_min = 0xffffffff;
            g_time_diff_max = 0;
            g_time_diff_total = 0;
            g_reset_flag = false;
        }

        clock_gettime(CLOCK_MONOTONIC, (struct timespec*)buffer);
        const auto send_res = endpoint->Send(buffer, message_size);
        if (message_size != send_res)
        {
            std::cout << (boost::format("send message block <%1% != %2%>") 
                                        % send_res 
                                        % message_size).str() << std::endl;                                  
        }

        int32_t recv_result = -1;
        do
        {
            if ((adk::verbs::ITcpStack::DriveMode::kReactor ==EndpointType::drive_mode())
                && (0 == tcp_stack->ReactorPerform()))
            {
                continue;
            }

            if (EndpointType::kZcRecvSupport)
            {
                recv_result = endpoint->ZcRecv([&](char* zc_buffer, ssize_t buffer_size) {
                    if (message_size == buffer_size)
                    {
                        clock_gettime(CLOCK_MONOTONIC, &current_tp);
                        const auto time_diff = (current_tp.tv_sec - ((struct timespec*)zc_buffer)->tv_sec) * 1000000000
                                             + current_tp.tv_nsec - ((struct timespec*)zc_buffer)->tv_nsec;

                        ++g_counter;
                        g_time_diff_max = std::max<uint64_t>(time_diff, g_time_diff_max);
                        g_time_diff_min = std::min<uint64_t>(time_diff, g_time_diff_min);
                        g_time_diff_total += time_diff;
                    }
                    else
                    {
                        std::cout << (boost::format("ZcRecv incomplete message <%1% != %2%>") 
                                                    % buffer_size
                                                    % message_size).str() << std::endl;
                    }
                });
            }
            else
            {
                recv_result = endpoint->Recv(buffer, message_size);
                if (message_size == recv_result)
                {
                    clock_gettime(CLOCK_MONOTONIC, &current_tp);
                    const auto time_diff = (current_tp.tv_sec - ((struct timespec*)buffer)->tv_sec) * 1000000000
                                          + current_tp.tv_nsec - ((struct timespec*)buffer)->tv_nsec;

                    ++g_counter;
                    g_time_diff_max = std::max<uint64_t>(time_diff, g_time_diff_max);
                    g_time_diff_min = std::min<uint64_t>(time_diff, g_time_diff_min);
                    g_time_diff_total += time_diff;
                }
                else if (recv_result > 0)
                {
                    std::cout << (boost::format("Recv incomplete message <%1% != %2%>") 
                                                % recv_result
                                                % message_size).str() << std::endl;
                }
            }
        } while (recv_result < 0);
    } while (true);
}

template<typename EndpointType>
void DoStreamTest(adk::verbs::ITcpStack* stack, 
                  adk::verbs::ITcpEndpoint* client_endpoint, 
                  uint32_t message_size, 
                  uint32_t transmit_rate)
{
    message_size = std::max<uint32_t>(message_size, sizeof(struct timespec));
    std::cout << (boost::format("stream | endpoint type <%1%> | message size <%2%>") 
                                % typeid(EndpointType).name()
                                % message_size).str() << std::endl;

    char* buffer = new char[message_size];
    auto* const endpoint = static_cast<EndpointType*>(client_endpoint);
    auto* const tcp_stack = static_cast<typename EndpointType::StackType*>(stack);

    auto* const rate_control = adk::RateControl::GetInstance<adk::rate_unit::Second>((double)transmit_rate, true);
    assert(rate_control);

    struct timespec current_tp;

    do
    {
        if (g_reset_flag)
        {
            g_counter = 0;
            g_time_diff_min = 0xffffffff;
            g_time_diff_max = 0;
            g_time_diff_total = 0;
            g_reset_flag = false;
        }

        if (adk::ErrorCode::kSuccess == rate_control->TryAcquire(1))
        {
            clock_gettime(CLOCK_MONOTONIC, (struct timespec*)buffer);
            const auto send_res = endpoint->Send(buffer, message_size);
            if (message_size != send_res)
            {
                std::cout << (boost::format("send message block <%1% != %2%>") 
                                            % send_res 
                                            % message_size).str() << std::endl;
            }
        }

        int32_t recv_result = -1;
        if ((adk::verbs::ITcpStack::DriveMode::kReactor ==EndpointType::drive_mode())
            && (0 == tcp_stack->ReactorPerform()))
        {
            continue;
        }

        if (EndpointType::kZcRecvSupport)
        {
            recv_result = endpoint->ZcRecv([&](char* zc_buffer, ssize_t buffer_size) {
                if (message_size == buffer_size)
                {
                    clock_gettime(CLOCK_MONOTONIC, &current_tp);
                    const auto time_diff = (current_tp.tv_sec - ((struct timespec*)zc_buffer)->tv_sec) * 1000000000
                                            + current_tp.tv_nsec - ((struct timespec*)zc_buffer)->tv_nsec;

                    ++g_counter;
                    g_time_diff_max = std::max<uint64_t>(time_diff, g_time_diff_max);
                    g_time_diff_min = std::min<uint64_t>(time_diff, g_time_diff_min);
                    g_time_diff_total += time_diff;
                }
                else
                {
                    std::cout << (boost::format("ZcRecv incomplete message <%1% != %2%>") 
                                                % buffer_size
                                                % message_size).str() << std::endl;
                }
            });
        }
        else
        {
            recv_result = endpoint->Recv(buffer, message_size);
            if (message_size == recv_result)
            {
                clock_gettime(CLOCK_MONOTONIC, &current_tp);
                const auto time_diff = (current_tp.tv_sec - ((struct timespec*)buffer)->tv_sec) * 1000000000
                                        + current_tp.tv_nsec - ((struct timespec*)buffer)->tv_nsec;

                ++g_counter;
                g_time_diff_max = std::max<uint64_t>(time_diff, g_time_diff_max);
                g_time_diff_min = std::min<uint64_t>(time_diff, g_time_diff_min);
                g_time_diff_total += time_diff;
            }
            else if (recv_result > 0)
            {
                std::cout << (boost::format("ZcRecv incomplete message <%1% != %2%>") 
                                            % recv_result
                                            % message_size).str() << std::endl;
            }
        }
    } while (true);
}

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("message-ip", boost::program_options::value<std::string>()->default_value(std::string()), "set message ip")
        ("remote-ip", boost::program_options::value<std::string>(), "set remote ip address")
        ("remote-port", boost::program_options::value<uint16_t>()->default_value(50000), "set remote port")
        ("message-size", boost::program_options::value<uint32_t>()->default_value(128), "set message size")
        ("transmit-rate", boost::program_options::value<uint32_t>()->default_value(0), 
        "set transmit rate [==0:pingpoing / >0:stream]")
    ;

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    if (vm.count("help") || !vm.count("remote-ip"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    const auto transmit_rate = vm["transmit-rate"].as<uint32_t>();
    const auto message_size = vm["message-size"].as<uint32_t>();
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

    auto* endpoint_client = adk::verbs::ITcpEndpoint::Create(tcp_stack, true, true);
    if (nullptr == endpoint_client)
    {
        std::cout << "Create client endpoint failed" << std::endl;
        return 0;
    }

    std::cout << (boost::format("Create client endpoint <%1%> success") % endpoint_client).str() << std::endl;

    adk::OnExit<> on_exit([=]() {
        assert(endpoint_client);
        adk::verbs::ITcpEndpoint::Destroy(endpoint_client);

        assert(tcp_epoller);
        adk::verbs::ITcpEPoller::Destroy(tcp_epoller);

        assert(tcp_stack);
        adk::verbs::ITcpStack::Destroy(tcp_stack);
    });

    endpoint_client->Bind(0);

    const std::string remote_ip = vm["remote-ip"].as<std::string>();
    const auto remote_port = vm["remote-port"].as<uint16_t>();
    while (true)
    {
        const auto ec = endpoint_client->Connect(remote_ip, remote_port);
        if (static_cast<int32_t>(adk::verbs::ITcpEndpoint::ConnectResult::kSuccess) == ec)
        {
            std::cout << (boost::format("Client endpoint connect imm remote address <%1%:%2%> success") 
                          % remote_ip
                          % remote_port).str() << std::endl;
            break;
        }

        if (static_cast<int32_t>(adk::verbs::ITcpEndpoint::ConnectResult::kInProgress) != ec)
        {
            std::cout << (boost::format("Client endpoint connect remote address <%1%:%2%> failed") 
                          % remote_ip
                          % remote_port).str() << std::endl;
            return 0;
        }

        if (!tcp_epoller->EPollAdd(endpoint_client, endpoint_client))
        {
            std::cout << (boost::format("EPollAdd endpoint <%1%>") % endpoint_client).str() << std::endl;
            return 0;
        }

        struct epoll_event event;
        if ((tcp_stack->ReactorPerform() > 0) && (1 == tcp_epoller->EPollWait(&event, 1, 1000)))
        {
            if (endpoint_client->EPollResult(event.events))
            {
                std::cout << (boost::format("Client endpoint connect remote address <%1%:%2%> success") 
                                            % remote_ip
                                            % remote_port).str() << std::endl;
                break;
            }
        }

        tcp_epoller->EPollDel(endpoint_client);
    }

    std::thread ob_thread = std::thread([&]() {
        do
        {
            sleep(1);
            const auto counter = ACCESS_ONCE(g_counter);
            const auto time_diff_min = ACCESS_ONCE(g_time_diff_min);
            const auto time_diff_max = ACCESS_ONCE(g_time_diff_max);
            const auto time_diff_total = ACCESS_ONCE(g_time_diff_total);
            g_reset_flag = true;

            if (counter > 0)
            {
                std::cout << (boost::format("nr:%1% \t avg:%2%(ns) \t min:%3%(ns) \t max:%4%(ns)") 
                                            % counter
                                            % (time_diff_total / counter)
                                            % time_diff_min 
                                            % time_diff_max).str() << std::endl;
            }
            else
            {
                std::cout << "nr:0 \t avg:N/A(ns) \t min:N/A(ns) \t max:N/A(ns)" << std::endl;
            }
        } while (true);
    });

    if (0 == transmit_rate)
    {
        if (adk::verbs::ITcpStack::StackType::kStackSk == tcp_stack->stack_type())
        {
            DoPingpongTest<adk::verbs::TcpEndpointSk>(tcp_stack, 
                                                      endpoint_client, 
                                                      message_size);
        }
        else if (adk::verbs::ITcpStack::StackType::kStackZf == tcp_stack->stack_type())
        {
            DoPingpongTest<adk::verbs::TcpEndpointZf>(tcp_stack, 
                                                      endpoint_client, 
                                                      message_size);
        }
        else
        {
            std::cout << (boost::format("Tcp stack type <%1%> unknown with message ip <%2%>") 
                                        % static_cast<int32_t>(tcp_stack->stack_type())
                                        % message_ip).str() << std::endl;
        }
    }
    else
    {
        if (adk::verbs::ITcpStack::StackType::kStackSk == tcp_stack->stack_type())
        {
            DoStreamTest<adk::verbs::TcpEndpointSk>(tcp_stack,
                                                    endpoint_client, 
                                                    message_size, 
                                                    transmit_rate);
        }
        else if (adk::verbs::ITcpStack::StackType::kStackZf == tcp_stack->stack_type())
        {
            DoStreamTest<adk::verbs::TcpEndpointZf>(tcp_stack,
                                                    endpoint_client, 
                                                    message_size, 
                                                    transmit_rate);
        }
        else
        {
            std::cout << (boost::format("Tcp stack type <%1%> unknown with message ip <%2%>") 
                                        % static_cast<int32_t>(tcp_stack->stack_type())
                                        % message_ip).str() << std::endl;
        }
    }

    return 0;
}