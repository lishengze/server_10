#include "test_tcp_engine.h"

#include <memory>

class EventHandler final : public EventHandlerBase
{
public:
    void OnEvent(Endpoint* endpoint, Event* event) override
    {
        EventHandlerBase::OnEvent(endpoint, event);
        if (event->level() > adk::io_engine::EventLevel::kInfo)
        {
            endpoint->Close();
        }
    }
};

class AcceptHandler final : public AcceptHandlerBase
{
public:
    void OnAccept(Endpoint* endpoint, Property& ep_props) override
    {
        AcceptHandlerBase::OnAccept(endpoint, ep_props);
        endpoint->set_share_ctx(endpoint);
    }
};

template<typename CallbackType>
class MessageHandler : public adk::io_engine::MessageHandler
{
public:
    int32_t OnMessage(Message* message) override
    {
        int32_t data_more = -1;
        const char* msg_data = message->const_data();
        uint32_t left_len = message->data_len();

        const char* consume_data = msg_data;

        do
        {
            if (left_len < 4)
            {
                break;
            }

            const uint32_t data_len = *((uint32_t*)consume_data);
            if (left_len >= data_len)
            {
                static_cast<CallbackType*>(this)->ProcessMsg(message->endpoint_share_ctx(), consume_data, data_len);

                left_len -= data_len;
                consume_data += data_len;
            }
            else
            {
                data_more = data_len - left_len;
                break;
            }
        } while (true);

        if (0 != left_len)
        {
            message->set_follow_up(consume_data - msg_data, data_more);
            return adk::io_engine::MessageHandler::Result::kFollowUp;
        }
        return ErrorCode::kSuccess;
    }
};

class MessageHandler1 final : public MessageHandler<MessageHandler1>
{
public:
    void ProcessMsg(void* endpoint, const char* data, uint32_t data_len)
    {
        ((Endpoint*)endpoint)->SendMsg(data, data_len);
    }
};

class MessageHandler2 final : public MessageHandler<MessageHandler2>
{
public:
    MessageHandler2()
    {
        recv_counter_ = 0;
        statistics_thread_ = std::thread([&]() {
            uint64_t counter_rec = recv_counter_;
            do 
            {
                sleep(1);
                const uint64_t temp_counter = *(volatile uint64_t*)(&recv_counter_);
                std::cout << "recv rate = " << temp_counter - counter_rec << std::endl;
                counter_rec = temp_counter;
            } while (true);
        });
    }

    void ProcessMsg(void* endpoint, const char* data, uint32_t data_len)
    {
        ++recv_counter_;
    }
private:
    uint64_t recv_counter_;
    std::thread statistics_thread_;
};

//receiver lantency test case
class MessageHandler3 final : public MessageHandler<MessageHandler3>
{
public:
    MessageHandler3()
    {
        Reset();
        counter_ = 0;
        statistics_thread_ = std::thread([&]() {
            uint64_t counter_rec = counter_;
            do
            {
                sleep(1);
                PrintStatistics();
            } while (true);
        });
    }

    void ProcessMsg(void* endpoint, const char* data, uint32_t data_len)
    {
        struct timespec current_tp;
        clock_gettime(CLOCK_REALTIME, &current_tp);
        struct timespec* message_tp = (struct timespec*)(data + sizeof(uint32_t));
        const auto time_diff = current_tp.tv_sec * 1000000000 + current_tp.tv_nsec
            - message_tp->tv_sec * 1000000000 - message_tp->tv_nsec;

        min_ = std::min<uint64_t>(min_, time_diff);
        max_ = std::max<uint64_t>(max_, time_diff);
        total_ += time_diff;
        ++counter_;
    }

    void PrintStatistics()
    {
        const auto counter = ACCESS_ONCE(counter_);
        if (0 != counter)
        {
            std::cout.precision(3);
            std::cout << boost::posix_time::second_clock::local_time()
                << " | total:" << std::setw(10) << counter
                << " | avg(us):" << std::setw(10) << std::fixed << (double)ACCESS_ONCE(total_) / (double)(counter * 1000)
                << " | min(us):" << std::setw(10) << std::fixed << (double)ACCESS_ONCE(min_) / (double)1000
                << " | max(us):" << std::setw(10) << std::fixed << (double)ACCESS_ONCE(max_) / (double)1000
                << std::endl;
        }
        else
        {
            std::cout << boost::posix_time::second_clock::local_time()
                << " | total:" << std::setw(10) << 0
                << " | avg(us):" << std::setw(10) << "NA"
                << " | min(us):" << std::setw(10) << "NA"
                << " | max(us):" << std::setw(10) << "NA"
                << std::endl;
        }
        reset_ = true;
    }

private:
    void Reset()
    {
        min_ = 0xffffffff;
        max_ = 0;
        total_ = 0;
        counter_ = 0;
        reset_ = false;
    }

    uint64_t min_;
    uint64_t max_;
    uint64_t total_;
    uint64_t counter_;
    bool reset_;
    std::thread statistics_thread_;
};

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("listen-port", boost::program_options::value<uint16_t>()->default_value(50000), "set server listen port")
        ("tx-low-latency", boost::program_options::value<bool>()->default_value(true), "set tx low latency")
        ("rx-low-latency", boost::program_options::value<bool>()->default_value(true), "set rx low latency")
        ("mode", boost::program_options::value<uint32_t>()->default_value(1), "set test mode [1: pingpong / 2: throughput]")
        ("tcp-no-delay", "set tcp no delay")
    ;

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    if (vm.count("help"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    TcpEngine* const tcp_engine = TcpEngine::Create(Property()
        (adk::io_engine::config::kIsRxLowLatency, vm["tx-low-latency"].as<bool>())
        (adk::io_engine::config::kIsTxLowLatency, vm["rx-low-latency"].as<bool>()));
    if (nullptr == tcp_engine)
    {
        std::cout << "create tcp engine failed" << std::endl;
        return -1;
    }

    Property accept_props;
    EventHandler   event_handler;
    AcceptHandler  accept_handler;

    std::unique_ptr<adk::io_engine::MessageHandler> message_handler;
    if (1 == vm["mode"].as<uint32_t>())
    {
        message_handler.reset(new MessageHandler1);
    }
    else if (2 == vm["mode"].as<uint32_t>())
    {
        message_handler.reset(new MessageHandler2);
    }
    else if (3 == vm["mode"].as<uint32_t>())
    {
        message_handler.reset(new MessageHandler3);
    }

    const uint16_t listen_port = vm["listen-port"].as<uint16_t>();

    accept_props
        (adk::io_engine::config::acceptor::kListenPort, listen_port)
        (adk::io_engine::config::endpoint::kTxMinResidentMicro, 10000000)
        (adk::io_engine::config::endpoint::kRxMinResidentMicro, 10000000)
        (adk::io_engine::config::acceptor::kEventHandler, &event_handler)
        (adk::io_engine::config::acceptor::kAcceptHandler, &accept_handler)
        (adk::io_engine::config::endpoint::kMessageHandler, message_handler.get())
        (adk::io_engine::config::endpoint::kTcpNoDelay, !!(vm.count("tcp-no-delay")))
    ;

    if (nullptr == tcp_engine->Accept(accept_props))
    {
        std::cout << "create acceptor failed, listen port <" << listen_port 
                  << ">, error info <" << tcp_engine->GetLastError() 
                  << ">" << std::endl;
        return -1;
    }

    string indicator;
    if(1 != vm["mode"].as<uint32_t>())
    {
        while (true)
        {
            sleep(3);
        }
    }
    else
    {
        while (true)
        {
            sleep(3);
            tcp_engine->CollectIndicator(indicator);
            std::cout << indicator << std::endl;
        }
    }

    TcpEngine::Destroy(tcp_engine);
    return 0;
}