#include "test_case.h"

class MessageHandlerImpl final : public MessageHandler<MessageHandlerImpl>
{
public:
    MessageHandlerImpl(TcpEndpoint* endpoint)
    {
        tcp_endpoint_ = endpoint;
    }

    void OnMessage(struct MessageHeader* message)
    {
        clock_gettime(CLOCK_REALTIME, &(message->timestamp21));
        if (ADK_UNLIKELY(ErrorCode::kSuccess != tcp_endpoint_->SendMsg(message, message->message_len)))
        {
            std::cout << "SendMsg failed" << std::endl;
        }
    }

private:
    TcpEndpoint* tcp_endpoint_;
};

class PreSendHandler : public adk::io_engine::PreSendHandler
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
        std::cout << "|txs-tol:" << std::setw(8) << diff_counter;
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
        clock_gettime(CLOCK_REALTIME, &(message_header->timestamp22));

        const auto diff = (message_header->timestamp22.tv_sec - last_tp_.tv_sec) * 1000000000
                        + (message_header->timestamp22.tv_nsec - last_tp_.tv_nsec);

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

    PreSendHandler* pre_send_handler = new PreSendHandler;
    PreRecvHandler* pre_recv_handler = new PreRecvHandler;

    adk::Property engine_props;
    engine_props.SetValues()
        (adk::io_engine::config::kIsRxLowLatency, vm["tx-low-latency"].as<bool>())
        (adk::io_engine::config::kIsTxLowLatency, vm["rx-low-latency"].as<bool>())
        (adk::io_engine::config::kPreSendHandler, pre_send_handler)
        (adk::io_engine::config::kPreRecvHandler, pre_recv_handler)
        ;

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
        for (int32_t index = 0; index < 3; ++index)
        {
            sleep(1);
            pre_send_handler->PrintIndicator();
            pre_recv_handler->PrintIndicator();
        }

        tcp_engine->CollectIndicator(indicator);
        std::cout << indicator << std::endl;
    } while (true);

    adk::io_engine::TcpEngine::Destroy(tcp_engine);
    return 0;
}