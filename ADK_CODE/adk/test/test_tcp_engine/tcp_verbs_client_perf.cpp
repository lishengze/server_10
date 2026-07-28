#include "test_verbs_perf.h"

class PerfStats
{
public:
    PerfStats(uint32_t message_size)
    {
        Reset();

        buffer_ = new char[message_size];
        buffer_len_ = 0;
        message_size_ = message_size;
    }

    void OnPerfStatsMessage(char* buffer, uint32_t buffer_size)
    {
        if (0 == buffer_len_)
        {
            OnPerfStatsMessageWithNoBuffer(buffer, buffer_size);
        }
        else
        {
            const auto missing_len = message_size_ - buffer_len_;
            if (buffer_size >= missing_len)
            {
                memcpy(buffer_ + buffer_len_, buffer, missing_len);
                
                OnPerfStats(buffer_);
                buffer_len_ = 0;

                OnPerfStatsMessageWithNoBuffer(buffer + missing_len, buffer_size - missing_len);
            }
            else
            {
                memcpy(buffer_ + buffer_len_, buffer, buffer_size);
                buffer_len_ += buffer_size;
            }
        }
    }

    void PrintPerfStats()
    {
        const auto counter = ACCESS_ONCE(counter_);
        if (0 != counter)
        {
            const auto rr_total = (double)ACCESS_ONCE(rr_total_) / (double)1000;
            const auto rd_total = (double)ACCESS_ONCE(rd_total_) / (double)1000;

            const auto rr_avg = rr_total / counter;
            const auto rd_avg = rd_total / counter;

            std::cout.precision(3);
            std::cout << boost::posix_time::second_clock::local_time()
                      << " | nr:" << std::setw(8) << counter
                      << " | rr_avg(us):" << std::setw(8) << std::fixed << rr_avg
                      << " | rr_min(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(rr_min_) / (double)1000
                      << " | rr_max(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(rr_max_) / (double)1000
                      << " | rd_avg(us):" << std::setw(8) << std::fixed << rd_avg
                      << " | rd_min(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(rd_min_) / (double)1000
                      << " | rd_max(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(rd_max_) / (double)1000
                      << " | ar_avg(us):" << std::setw(8) << std::fixed << rr_avg - rd_avg
                      << std::endl;
        }
        else
        {
            std::cout << boost::posix_time::second_clock::local_time()
                      << " | nr:" << std::setw(8) << counter
                      << " | rr_avg(us):" << std::setw(8) << std::fixed << "N/A"
                      << " | rr_min(us):" << std::setw(8) << std::fixed << "N/A"
                      << " | rr_max(us):" << std::setw(8) << std::fixed << "N/A"
                      << " | rd_avg(us):" << std::setw(8) << std::fixed << "N/A"
                      << " | rd_min(us):" << std::setw(8) << std::fixed << "N/A"
                      << " | rd_max(us):" << std::setw(8) << std::fixed << "N/A"
                      << " | ar_avg(us):" << std::setw(8) << std::fixed << "N/A"
                      << std::endl;
        }

        reset_ = true;
    }

    void OnPerfStats(char* buffer)
    {
        if (reset_)
        {
            Reset();
        }

        ++counter_;

        MessageFormat* message_content = (MessageFormat*)buffer;

        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        const struct timespec* timepoint1 = &(message_content->timepoint1);
        const auto time_diff_rr = current_time.tv_sec * 1000000000 + current_time.tv_nsec
            - (timepoint1->tv_sec * 1000000000 + timepoint1->tv_nsec);

        const struct timespec* timepoint2 = &(message_content->timepoint2);
        const struct timespec* timepoint3 = &(message_content->timepoint3);
        const auto time_diff_rd = timepoint3->tv_sec * 1000000000 + timepoint3->tv_nsec
            - (timepoint2->tv_sec * 1000000000 + timepoint2->tv_nsec);

        rr_total_ += time_diff_rr;
        rr_min_ = std::min<uint64_t>(rr_min_, time_diff_rr);
        rr_max_ = std::max<uint64_t>(rr_max_, time_diff_rr);

        rd_total_ += time_diff_rd;
        rd_min_ = std::min<uint64_t>(rd_min_, time_diff_rd);
        rd_max_ = std::max<uint64_t>(rd_max_, time_diff_rd);
    }

private:
    void Reset()
    {
        counter_ = 0;
        rr_total_ = 0;
        rr_min_ = 0xffffffff;
        rr_max_ = 0;
        rd_total_ = 0;
        rd_min_ = 0xffffffff;
        rd_max_ = 0;
        reset_ = false;
    }

    inline void OnPerfStatsMessageWithNoBuffer(char* buffer, uint32_t buffer_size)
    {
        while (buffer_size >= message_size_)
        {
            OnPerfStats(buffer);
            buffer += message_size_;
            buffer_size -= message_size_;
        }

        if (buffer_size > 0)
        {
            memcpy(buffer_, buffer, buffer_size);
            buffer_len_ = buffer_size;
        }
    }

    volatile bool reset_;
    uint64_t      counter_;
    uint64_t      rr_total_;
    uint64_t      rr_min_;
    uint64_t      rr_max_;
    uint64_t      rd_total_;
    uint64_t      rd_min_;
    uint64_t      rd_max_;

    char*         buffer_;
    uint32_t      buffer_len_;
    uint32_t      message_size_;
};

adk::verbs::ITcpEndpoint* DoConnect(adk::verbs::ITcpStack* const tcp_stack,
                                    const std::string& remote_ip, 
                                    uint16_t remote_port)
{
    auto* const endpoint = adk::verbs::ITcpEndpoint::Create(tcp_stack, true, true);
    if (nullptr == endpoint)
    {
        std::cout << "Create client endpoint failed" << std::endl;
        return nullptr;
    }

    endpoint->Bind(0);

    auto* const tcp_epoller = adk::verbs::ITcpEPoller::Create(tcp_stack, 
                                                              adk::verbs::ITcpEPoller::PollerType::kControl);
    if (nullptr == tcp_epoller)
    {
        std::cout << "Create tcp epoller <Control> failed" << std::endl;
        return nullptr;
    }

    bool result = true;
    while (true)
    {
        const auto ec = endpoint->Connect(remote_ip, remote_port);
        if (static_cast<int32_t>(adk::verbs::ITcpEndpoint::ConnectResult::kSuccess) == ec)
        {
            break;
        }

        if (static_cast<int32_t>(adk::verbs::ITcpEndpoint::ConnectResult::kInProgress) != ec)
        {
            result = false;
            std::cout << (boost::format("Client endpoint connect remote address <%1%:%2%> failed")
                                        % remote_ip
                                        % remote_port).str() << std::endl;
            break;
        }

        if (!tcp_epoller->EPollAdd(endpoint, endpoint))
        {
            result = false;
            std::cout << "EPollAdd endpoint failed" << std::endl;
            break;
        }

        struct epoll_event event;
        constexpr int32_t kTimeoutMs = 30000;
        if ((tcp_stack->ReactorPerform() > 0) && (1 == tcp_epoller->EPollWait(&event, 1, kTimeoutMs)))
        {
            if (endpoint->EPollResult(event.events))
            {
                break;
            }
        }

        tcp_epoller->EPollDel(endpoint);
    }

    adk::verbs::ITcpEPoller::Destroy(tcp_epoller);
    if (!result)
    {
        adk::verbs::ITcpEndpoint::Destroy(endpoint);
        return nullptr;
    }

    std::cout << (boost::format("Client <%1%:%2%> connect to address <%3%:%4%> success")
                                % endpoint->local_ip()
                                % endpoint->local_port()
                                % endpoint->remote_ip()
                                % endpoint->remote_port()).str() << std::endl;
    return endpoint;
}

template<typename StackType, typename EndpointType, bool kParallel>
void TestBodyPingpong(StackType* tcp_stack, 
                      EndpointType* endpoint, 
                      uint32_t message_size)
{
    message_size = std::max<uint32_t>(message_size, 8 + sizeof(struct timespec) * 3);
    char* const send_buffer = new char[message_size];
    assert(send_buffer);

    memset(send_buffer, 0, message_size);

    volatile bool is_running = true;
    PerfStats perf_stats(message_size);
    MessageFormat* const message_content = (MessageFormat*)send_buffer;

    std::thread tx_thread_hdl;
    std::thread ob_thread_hdl = std::thread([&perf_stats, &is_running]() {
        do
        {
            sleep(1);
            perf_stats.PrintPerfStats();
        } while (is_running);
    });

    char* const recv_buffer = new char[message_size];
    assert(recv_buffer);

    if (kParallel)
    {
        volatile uint64_t counter = 0;
        tx_thread_hdl = std::thread([&]() {
            do 
            {
                if (0 == (counter & 1))
                {
                    ++counter;
                    message_content->message_size = message_size;
                    clock_gettime(CLOCK_MONOTONIC, &message_content->timepoint1);
                    if (!CheckTxResult(endpoint->Send(send_buffer, message_size), message_size, endpoint))
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

        do 
        {
            const auto recv_result = endpoint->Recv(recv_buffer, message_size);
            if (message_size == recv_result)
            {
                perf_stats.OnPerfStats(recv_buffer);
                ++counter;
            }
            else
            {
                if (!CheckRxResult(recv_result, endpoint))
                {
                    is_running = false;
                }
            }
        } while (is_running);
    }
    else
    {
        do
        {
            message_content->message_size = message_size;
            clock_gettime(CLOCK_MONOTONIC, &message_content->timepoint1);
            if (!CheckTxResult(endpoint->Send(send_buffer, message_size), message_size, endpoint))
            {
                is_running = false;
                break;
            }

            do 
            {
                if ((adk::verbs::ITcpStack::DriveMode::kReactor == EndpointType::drive_mode())
                    && (0 == tcp_stack->ReactorPerform()))
                {
                    continue;
                }

                int32_t recv_result = -1;
                if (EndpointType::kZcRecvSupport)
                {
                    recv_result = endpoint->ZcRecv([&](char* zc_buffer, ssize_t buffer_size) {
                        perf_stats.OnPerfStatsMessage(zc_buffer, buffer_size);
                    });

                    if (recv_result > 0)
                    {
                        break;
                    }
                }
                else
                {
                    recv_result = endpoint->Recv(recv_buffer, message_size);
                    if (recv_result > 0)
                    {
                        perf_stats.OnPerfStatsMessage(recv_buffer, recv_result);
                        break;
                    }
                }

                if (!CheckRxResult(recv_result, endpoint))
                {
                    is_running = false;
                    break;
                }

            } while (true);
        } while (is_running);
    }

    ob_thread_hdl.join();
    if (tx_thread_hdl.joinable())
    {
        tx_thread_hdl.join();
    }
}

template<typename StackType, typename EndpointType, bool kParallel>
void TestStreamBody(StackType* tcp_stack, 
                    EndpointType* endpoint, 
                    uint32_t message_size, 
                    uint32_t transmit_rate)
{
    auto* const token_buket = adk::RateControl::GetInstance<adk::rate_unit::Second>(static_cast<double>(transmit_rate), true);
    assert(token_buket);

    message_size = std::max<uint32_t>(message_size, sizeof(MessageFormat));
    char* const send_buffer = new char[message_size];
    assert(send_buffer);

    memset(send_buffer, 0, message_size);

    volatile bool is_running = true;
    PerfStats perf_stats(message_size);

    std::thread tx_thread_hdl;
    std::thread ob_thread_hdl = std::thread([&perf_stats, &is_running]() {
        do 
        {
            sleep(1);
            perf_stats.PrintPerfStats();
        } while (is_running);
    });

    constexpr uint32_t recv_buffer_len = 4096;
    char* const recv_buffer = new char[recv_buffer_len];
    assert(recv_buffer);

    MessageFormat* const message_content = (MessageFormat*)send_buffer;

    if (kParallel)
    {
        tx_thread_hdl = std::thread([&]() {
            do 
            {
                if (adk::ErrorCode::kSuccess == token_buket->TryAcquire(1))
                {
                    message_content->message_size = message_size;
                    clock_gettime(CLOCK_MONOTONIC, &message_content->timepoint1);
                    if (!CheckTxResult(endpoint->Send(send_buffer, message_size), message_size, endpoint))
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
    }

    do 
    {
        if (!kParallel)
        {
            if (adk::ErrorCode::kSuccess == token_buket->TryAcquire(1))
            {
                message_content->message_size = message_size;
                clock_gettime(CLOCK_MONOTONIC, &message_content->timepoint1);
                if (!CheckTxResult(endpoint->Send(send_buffer, message_size), message_size, endpoint))
                {
                    is_running = false;
                    break;
                }
            }
        }

        if ((adk::verbs::ITcpStack::DriveMode::kReactor == EndpointType::drive_mode())
            && (0 == tcp_stack->ReactorPerform()))
        {
            continue;
        }

        int32_t recv_result = -1;
        if (EndpointType::kZcRecvSupport)
        {
            recv_result = endpoint->ZcRecv([&](char* zc_buffer, ssize_t buffer_size) {
                perf_stats.OnPerfStatsMessage(zc_buffer, buffer_size);
            });
        }
        else
        {
            recv_result = endpoint->Recv(recv_buffer, recv_buffer_len);
            if (recv_result > 0)
            {
                perf_stats.OnPerfStatsMessage(recv_buffer, recv_result);
            }
        }

        if (!CheckRxResult(recv_result, endpoint))
        {
            is_running = false;
            break;
        }

    } while (is_running);

    ob_thread_hdl.join();
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
        ("remote-ip", boost::program_options::value<std::string>(), "set remote ip address")
        ("remote-port", boost::program_options::value<uint16_t>()->default_value(50000), "set remote port")
        ("io-parallel", "set send/recv parallel")
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

    const std::string message_ip = vm["message-ip"].as<std::string>();
    auto* const tcp_stack = adk::verbs::ITcpStack::Create(message_ip);
    if (nullptr == tcp_stack)
    {
        std::cout << (boost::format("Create tcp stack failed with message ip <%1%>")
                                    % message_ip).str() << std::endl;
        return 0;
    }

    auto* const endpoint = DoConnect(tcp_stack, 
                                     vm["remote-ip"].as<std::string>(), 
                                     vm["remote-port"].as<uint16_t>());
    if (nullptr == endpoint)
    {
        return 0;
    }

    const auto message_size = vm["message-size"].as<uint32_t>();
    const auto transmit_rate = vm["transmit-rate"].as<uint32_t>();
    const auto stack_type = tcp_stack->stack_type();
    if (adk::verbs::ITcpStack::StackType::kStackSk == stack_type)
    {
        if (0 == transmit_rate)
        {
            if (vm.count("io-parallel"))
            {
                TestBodyPingpong<adk::verbs::TcpStackSk, 
                                 adk::verbs::TcpEndpointSk, 
                                 true>(static_cast<adk::verbs::TcpStackSk*>(tcp_stack),
                                       static_cast<adk::verbs::TcpEndpointSk*>(endpoint), 
                                       message_size);
            }
            else
            {
                TestBodyPingpong<adk::verbs::TcpStackSk, 
                                 adk::verbs::TcpEndpointSk, 
                                 false>(static_cast<adk::verbs::TcpStackSk*>(tcp_stack),
                                        static_cast<adk::verbs::TcpEndpointSk*>(endpoint),
                                        message_size);
            }
        }
        else
        {
            if (vm.count("io-parallel"))
            {
                TestStreamBody<adk::verbs::TcpStackSk, 
                               adk::verbs::TcpEndpointSk, 
                               true>(static_cast<adk::verbs::TcpStackSk*>(tcp_stack),
                                     static_cast<adk::verbs::TcpEndpointSk*>(endpoint),
                                     message_size, 
                                     transmit_rate);
            }
            else
            {
                TestStreamBody<adk::verbs::TcpStackSk, 
                               adk::verbs::TcpEndpointSk, 
                               false>(static_cast<adk::verbs::TcpStackSk*>(tcp_stack),
                                      static_cast<adk::verbs::TcpEndpointSk*>(endpoint),
                                      message_size, 
                                      transmit_rate);
            }
        }
    }
    else if (adk::verbs::ITcpStack::StackType::kStackZf == stack_type)
    {
        if (0 == transmit_rate)
        {
            TestBodyPingpong<adk::verbs::TcpStackZf, 
                             adk::verbs::TcpEndpointZf, 
                             false>(static_cast<adk::verbs::TcpStackZf*>(tcp_stack),
                                    static_cast<adk::verbs::TcpEndpointZf*>(endpoint), 
                                    message_size);
        }
        else
        {
            TestStreamBody<adk::verbs::TcpStackZf, 
                           adk::verbs::TcpEndpointZf, 
                           false>(static_cast<adk::verbs::TcpStackZf*>(tcp_stack),
                                  static_cast<adk::verbs::TcpEndpointZf*>(endpoint),
                                  message_size, 
                                  transmit_rate);
        }
    }
    else
    {
        std::cout << (boost::format("Unknown stack type <%1%>")
                                     % static_cast<int32_t>(tcp_stack->stack_type())).str() << std::endl;
    }

    adk::verbs::ITcpEndpoint::Destroy(endpoint);
    adk::verbs::ITcpStack::Destroy(tcp_stack);
    return 0;
}