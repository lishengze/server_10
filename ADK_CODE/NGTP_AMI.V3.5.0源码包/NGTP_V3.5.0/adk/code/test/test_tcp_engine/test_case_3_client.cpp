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

class PreSendHandler final : public adk::io_engine::PreSendHandler
{
public:
    PreSendHandler()
    {
        clock_gettime(CLOCK_REALTIME, &last_tp_);
        ResetIndicator();
    }

    void PrintIndicator()
    {
        std::cout.precision(3);
        std::cout << boost::posix_time::second_clock::local_time();
        const auto counter = ACCESS_ONCE(counter_);

        std::cout << " |total:" << std::setw(8) << counter;
        if (0 != counter)
        {
            std::cout << "|avg(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(interval_tol_) / (double)(counter * 1000)
                      << "|min(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(interval_min_) / (double)1000
                      << "|max(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(interval_max_) / (double)1000;
        }
        else
        {
            std::cout << "|avg(us):      NA";
        }

        const auto diff_counter = ACCESS_ONCE(diff_counter_);
        std::cout << " |txs-tol:" << std::setw(8) << diff_counter;
        if (0 != diff_counter)
        {
            std::cout << "|avg(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(diff_tol_) / (double)(diff_counter * 1000)
                      << "|min(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(diff_min_) / (double)1000
                      << "|max(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(diff_max_) / (double)1000;
        }
        else
        {
            std::cout << "|avg(us):      NA";
        }

        std::cout << std::endl;
        reset_ = true;
    }

    int32_t OnTxMessageBefore(void* ep_share_ctx, adk::io_engine::Message* message) override
    {
        struct MessageHeader* message_header = (struct MessageHeader*)(message->data());
        clock_gettime(CLOCK_REALTIME, &(message_header->timestamp12));

        const auto diff = (message_header->timestamp12.tv_sec - last_tp_.tv_sec) * 1000000000
                        + (message_header->timestamp12.tv_nsec - last_tp_.tv_nsec);

        ++counter_;
        interval_min_ = std::min<uint64_t>(diff, interval_min_);
        interval_max_ = std::max<uint64_t>(diff, interval_max_);
        interval_tol_ += diff;

        last_tp_ = message_header->timestamp22;

        return ErrorCode::kSuccess;
    }

    void OnTxMessageAfter(void* ep_share_ctx, int32_t result) override
    {
        if (reset_)
        {
            ResetIndicator();
        }

        struct timespec current;
        clock_gettime(CLOCK_REALTIME, &current);

        const auto diff = (current.tv_sec - last_tp_.tv_sec) * 1000000000
            + (current.tv_nsec - last_tp_.tv_nsec);
        ++diff_counter_;
        diff_min_ = std::min<uint64_t>(diff, diff_min_);
        diff_max_ = std::max<uint64_t>(diff, diff_max_);
        diff_tol_ += diff;
    }

private:
    void ResetIndicator()
    {
        counter_ = 0;
        interval_min_ = 0xffffffff;
        interval_max_ = 0;
        interval_tol_ = 0;

        diff_counter_ = 0;
        diff_min_ = 0xffffffff;
        diff_max_ = 0;
        diff_tol_ = 0;
        reset_ = false;
    }

    struct timespec last_tp_;

    volatile bool reset_;
    uint64_t counter_;
    uint64_t interval_min_;
    uint64_t interval_max_;
    uint64_t interval_tol_;

    uint64_t diff_counter_;
    uint64_t diff_min_;
    uint64_t diff_max_;
    uint64_t diff_tol_;
};

class PreRecvHandler final : public adk::io_engine::PreRecvHandler
{
public:
    PreRecvHandler()
    {
        ResetIndicator();
        clock_gettime(CLOCK_REALTIME, &last_tp_);
    }

    void OnRxMessageBefore(void* share_ctx)
    {
        if (reset_)
        {
            ResetIndicator();
        }

        struct timespec current_tp;
        clock_gettime(CLOCK_REALTIME, &current_tp);

        const auto diff = (current_tp.tv_sec - last_tp_.tv_sec) * 1000000000
                        + (current_tp.tv_nsec - last_tp_.tv_nsec);
        ++total_counter_;
        interval_tol_ += diff;
        interval_min_ = std::min<uint64_t>(interval_min_, diff);
        interval_max_ = std::max<uint64_t>(interval_max_, diff);

        last_tp_ = current_tp;
    }

    void OnRxMessageAfter(void* share_ctx, int32_t result)
    {
        struct timespec current_tp;
        clock_gettime(CLOCK_REALTIME, &current_tp);

        const auto diff = (current_tp.tv_sec - last_tp_.tv_sec) * 1000000000
                        + (current_tp.tv_nsec - last_tp_.tv_nsec);
        if (result > 0)
        {
            ++effective_counter_;
            effective_tol_ += diff;
            effective_min_ = std::min<uint64_t>(effective_min_, diff);
            effective_max_ = std::max<uint64_t>(effective_max_, diff);
        }
        else
        {
            ++non_effect_counter_;
            non_effect_tol_ += diff;
            non_effect_min_ = std::min<uint64_t>(non_effect_min_, diff);
            non_effect_max_ = std::max<uint64_t>(non_effect_max_, diff);
        }
    }

    void PrintIndicator()
    {
        std::cout.precision(3);
        std::cout << boost::posix_time::second_clock::local_time();

        const auto total_counter = ACCESS_ONCE(total_counter_);
        std::cout << " |total:" << std::setw(8) << total_counter;
        if (0 != total_counter)
        {
            std::cout << "|avg(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(interval_tol_) / (double)(total_counter * 1000)
                      << "|min(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(interval_min_) / (double)1000
                      << "|max(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(interval_max_) / (double)1000;
        }
        else
        {
            std::cout << "|avg(us):      NA";
        }

        const auto effective_counter = ACCESS_ONCE(effective_counter_);
        std::cout << "|eff-tol:" << std::setw(8) << effective_counter;
        if (0 != effective_counter)
        {
            std::cout << "|avg(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(effective_tol_) / (double)(effective_counter * 1000)
                      << "|min(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(effective_min_) / (double)1000
                      << "|max(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(effective_max_) / (double)1000;
        }
        else
        {
            std::cout << "|avg(us):      NA";
        }

        const auto non_effect_counter = ACCESS_ONCE(non_effect_counter_);
        std::cout << "|non-tol:" << std::setw(8) << non_effect_counter;
        if (0 != non_effect_counter)
        {
            std::cout << "|avg(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(non_effect_tol_) / (double)(non_effect_counter * 1000)
                      << "|min(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(non_effect_min_) / (double)1000
                      << "|max(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(non_effect_max_) / (double)1000;
        }
        else
        {
            std::cout << "|avg(us):      NA";
        }

        std::cout << std::endl;
        reset_ = true;
    }

private:
    void ResetIndicator()
    {
        total_counter_ = 0;
        effective_counter_ = 0;
        non_effect_counter_ = 0;

        interval_tol_ = 0;
        interval_min_ = 0xffffffff;
        interval_max_ = 0;

        effective_tol_ = 0;
        effective_min_ = 0xffffffff;
        effective_max_ = 0;

        non_effect_tol_ = 0;
        non_effect_min_ = 0xffffffff;
        non_effect_max_ = 0;
        reset_ = false;
    }

    struct timespec last_tp_;

    volatile bool reset_;

    uint64_t total_counter_;
    uint64_t effective_counter_;
    uint64_t non_effect_counter_;

    uint64_t interval_tol_;
    uint64_t interval_min_;
    uint64_t interval_max_;

    uint64_t effective_tol_;
    uint64_t effective_min_;
    uint64_t effective_max_;

    uint64_t non_effect_tol_;
    uint64_t non_effect_min_;
    uint64_t non_effect_max_;
};

class MessageHandlerImpl final : public MessageHandler<MessageHandlerImpl>
{
public:
    MessageHandlerImpl()
    {
        ResetIndicator();
    }

    void OnMessage(struct MessageHeader* message)
    {
        if (reset_)
        {
            ResetIndicator();
        }

        ++counter_;

        struct timespec current_time;
        clock_gettime(CLOCK_REALTIME, &current_time);

        const auto diff_rtt = (current_time.tv_sec - message->timestamp11.tv_sec) * 1000000000
                            + (current_time.tv_nsec - message->timestamp11.tv_nsec);

        const auto diff_11_12 = (message->timestamp12.tv_sec - message->timestamp11.tv_sec) * 1000000000
                              + (message->timestamp12.tv_nsec - message->timestamp11.tv_nsec);

        const auto diff_21_22 = (message->timestamp22.tv_sec - message->timestamp21.tv_sec) * 1000000000
                              + (message->timestamp22.tv_nsec - message->timestamp21.tv_nsec);

        min_rtt_ = std::min<uint64_t>(min_rtt_, diff_rtt);
        max_rtt_ = std::max<uint64_t>(max_rtt_, diff_rtt);
        tol_rtt_ += diff_rtt;

        min_11_12_ = std::min<uint64_t>(min_11_12_, diff_11_12);
        max_11_12_ = std::max<uint64_t>(max_11_12_, diff_11_12);
        tol_11_12_ += diff_11_12;

        min_21_22_ = std::min<uint64_t>(min_21_22_, diff_21_22);
        max_21_22_ = std::max<uint64_t>(max_21_22_, diff_21_22);
        tol_21_22_ += diff_21_22;

        const auto diff_io = diff_rtt - diff_11_12 - diff_21_22;
        min_io_ = std::min<uint64_t>(min_io_, diff_io);
        max_io_ = std::max<uint64_t>(max_io_, diff_io);
        tol_io_ += diff_io;
    }

    void PrintIndicator()
    {
        const auto counter = ACCESS_ONCE(counter_);
        if (0 != counter)
        {
            std::cout.precision(3);
            std::cout << boost::posix_time::second_clock::local_time()
                << " |total:" << std::setw(8) << counter
                << "|avg(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(tol_rtt_) / (double)(counter * 1000)
                << "|min(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(min_rtt_) / (double)1000
                << "|max(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(max_rtt_) / (double)1000
                << "|avg(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(tol_11_12_) / (double)(counter * 1000)
                << "|min(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(min_11_12_) / (double)1000
                << "|max(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(max_11_12_) / (double)1000
                << "|avg(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(tol_21_22_) / (double)(counter * 1000)
                << "|min(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(min_21_22_) / (double)1000
                << "|max(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(max_21_22_) / (double)1000
                << "|avg(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(tol_io_) / (double)(counter * 1000)
                << "|min(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(min_io_) / (double)1000
                << "|max(us):" << std::setw(8) << std::fixed << (double)ACCESS_ONCE(max_io_) / (double)1000
                << std::endl;
        }
        else
        {
            std::cout << boost::posix_time::second_clock::local_time()
                      << " |total:" << std::setw(8) << 0 << " |NA" << std::endl;
        }
        reset_ = true;
    }

private:
    void ResetIndicator()
    {
        counter_ = 0;
        min_rtt_ = 0xffffffff;
        max_rtt_ = 0;
        tol_rtt_ = 0;

        min_11_12_ = 0xffffffff;
        max_11_12_ = 0;
        tol_11_12_ = 0;

        min_21_22_ = 0xffffffff;
        max_21_22_ = 0;
        tol_21_22_ = 0;

        min_io_ = 0xffffffff;
        max_io_ = 0;
        tol_io_ = 0;
        reset_ = false;
    }

    volatile bool reset_;

    uint64_t counter_;

    uint64_t min_rtt_;
    uint64_t max_rtt_;
    uint64_t tol_rtt_;

    uint64_t min_11_12_;
    uint64_t max_11_12_;
    uint64_t tol_11_12_;

    uint64_t min_21_22_;
    uint64_t max_21_22_;
    uint64_t tol_21_22_;

    uint64_t min_io_;
    uint64_t max_io_;
    uint64_t tol_io_;
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

    PreSendHandler* const pre_send_handler = new PreSendHandler;
    PreRecvHandler* const pre_recv_handler = new PreRecvHandler;

    adk::Property engine_props;
    engine_props.SetValues()
        (adk::io_engine::config::kIsRxLowLatency, vm["tx-low-latency"].as<bool>())
        (adk::io_engine::config::kIsTxLowLatency, vm["rx-low-latency"].as<bool>())
        (adk::io_engine::config::kPreSendHandler, pre_send_handler)
        (adk::io_engine::config::kPreRecvHandler, pre_recv_handler);

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
            pre_recv_handler->PrintIndicator();
            pre_send_handler->PrintIndicator();
        } while (g_is_running);
    });

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