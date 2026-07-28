#include "test_case.h"

class ConnectHandlerImpl final : public ConnectHandler<ConnectHandlerImpl>
{
public:
    ConnectHandlerImpl()
    {
        is_connect_success_ = false;
    }

    void OnConnect(adk::io_engine::Endpoint* endpoint)
    {
        is_connect_success_ = true;
    }

    bool is_connect_success() const
    {
        return is_connect_success_;
    }

private:
    volatile bool is_connect_success_;
};

class MessageHandlerImpl final : public MessageHandler<MessageHandlerImpl>
{
public:
    MessageHandlerImpl()
    {
        ResetIndicator();
        save_counter_ = 0;
    }

    void OnMessage(struct MessageHeader* message)
    {
        if (reset_)
        {
            ResetIndicator();
        }

        ++recv_counter_;
        const auto diff = calc_time_diff(message);
        time_diff_min_ = std::min<uint64_t>(diff, time_diff_min_);
        time_diff_max_ = std::max<uint64_t>(diff, time_diff_max_);
        time_diff_tol_ += diff;

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
    }

    uint64_t calc_time_diff(struct MessageHeader* message)
    {
        struct timespec current_time;
        clock_gettime(CLOCK_REALTIME, &current_time);

        const auto rtt = (current_time.tv_sec - message->timestamp11.tv_sec) * 1000000000 
                       + (current_time.tv_nsec - message->timestamp11.tv_nsec);
        const auto exc = (message->timestamp22.tv_sec - message->timestamp21.tv_sec) * 1000000000
                       + (message->timestamp22.tv_nsec - message->timestamp21.tv_nsec);
        return rtt - exc;
    }

    void PrintIndicator()
    {
        const auto recv_counter = ACCESS_ONCE(recv_counter_);
        if (0 != recv_counter)
        {
            std::cout.precision(3);
            std::cout << boost::posix_time::second_clock::local_time()
                << " | rtt | total:" << std::setw(10) << recv_counter
                << " | avg(us):" << std::setw(10) << std::fixed << (double)ACCESS_ONCE(time_diff_tol_) / (double)(recv_counter * 1000)
                << " | min(us):" << std::setw(10) << std::fixed << (double)ACCESS_ONCE(time_diff_min_) / (double)1000
                << " | max(us):" << std::setw(10) << std::fixed << (double)ACCESS_ONCE(time_diff_max_) / (double)1000
                << std::endl;
        }
        else
        {
            std::cout << boost::posix_time::second_clock::local_time()
                << " | rtt | total:" << std::setw(10) << 0
                << " | avg(us):" << std::setw(10) << "NA"
                << " | min(us):" << std::setw(10) << "NA"
                << " | max(us):" << std::setw(10) << "NA"
                << std::endl;
        }
        reset_ = true;
    }

private:
    void ResetIndicator()
    {
        recv_counter_ = 0;
        time_diff_min_ = 0xffffffff;
        time_diff_max_ = 0;
        time_diff_tol_ = 0;
        reset_ = false;
    }

    volatile bool reset_;
    uint64_t recv_counter_;
    uint64_t time_diff_min_;
    uint64_t time_diff_max_;
    uint64_t time_diff_tol_;

    uint64_t save_counter_;
};

int main(int argc, char* argv[])
{
    variables_map vm;
    if (!client_options(argc, argv, vm))
    {
        return -1;
    }

    const auto message_size = std::max<uint32_t>(vm["message-size"].as<uint32_t>(), kMinMessageSize);
    std::cout << "message size(bytes): " << message_size << std::endl;

    const auto rate_limit = vm["transmit-rate"].as<double>();
    adk::TokenBucket* rate_control = adk::RateControl::GetInstance<adk::rate_unit::Second>(rate_limit, rate_limit < 3000001);
    if (nullptr == rate_control)
    {
        std::cout << "Create rate control failed" << std::endl;
        return 0;
    }

    std::cout << "rate limit(bytes/s): " << rate_limit << std::endl;

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

    auto* const connect_handler = new ConnectHandlerImpl;
    auto* const message_handler = new MessageHandlerImpl;
    adk::Property ep_props;
    ep_props.SetValues()
        (adk::io_engine::config::endpoint::kRemoteIp, vm["remote-ip"].as<std::string>())
        (adk::io_engine::config::endpoint::kRemotePort, vm["remote-port"].as<uint16_t>())
        (adk::io_engine::config::endpoint::kTxMinResidentMicro, 1000000)
        (adk::io_engine::config::endpoint::kRxMinResidentMicro, 1000000)
        (adk::io_engine::config::endpoint::kEventHandler, new EventHandlerClient)
        (adk::io_engine::config::endpoint::kConnectHandler, connect_handler)
        (adk::io_engine::config::endpoint::kMessageHandler, message_handler)
        (adk::io_engine::config::endpoint::kTcpNoDelay, !!(vm.count("tcp-no-delay")));

    g_is_running = true;
    auto* const endpoint = tcp_engine->Connect(ep_props);
    if (nullptr == endpoint)
    {
        std::cout << "connect failed <" << tcp_engine->GetLastError() << ">" << std::endl;
        return 0;
    }

    while (g_is_running && !connect_handler->is_connect_success())
    {
        usleep(0);
    }

    std::thread ob_thread([&]() {
        do 
        {
            sleep(1);
            message_handler->PrintIndicator();
        } while (g_is_running);
    });

    uint64_t loop_counter = 0;
    while (g_is_running)
    {
        auto* const message = endpoint->NewMessage(message_size);
        message->set_data_len(message_size);

    retry_acq:
        if (adk::ErrorCode::kSuccess != rate_control->TryAcquire(1))
        {
            for (uint32_t index = 0; index < 32; ++index)
            {
                ADK_PAUSE();
            }
            goto retry_acq;
        }

        for (uint32_t offset = kMessageDataOffset; offset < message_size; offset += sizeof(uint64_t))
        {
            *((uint64_t*)(message->data() + offset)) = loop_counter++;
        }

        ((struct MessageHeader*)(message->data()))->message_len = message_size;
        clock_gettime(CLOCK_REALTIME, &(((struct MessageHeader*)(message->data()))->timestamp11));
        if (ADK_UNLIKELY(ErrorCode::kSuccess != endpoint->SendMsg(message)))
        {
            std::cout << "SendMsg failed ... " << std::endl;
            break;
        }
    }

    g_is_running = false;
    ob_thread.join();
    adk::io_engine::TcpEngine::Destroy(tcp_engine);
    return 0;
}