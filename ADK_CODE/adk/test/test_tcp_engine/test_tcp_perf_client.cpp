#include "test_tcp_engine.h"

#include <boost/locale.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <adk/token_buckets.h>

volatile bool g_is_running = true;
Endpoint* g_tcp_endpoint = nullptr;

class EventHandler final : public EventHandlerBase
{
public:
    void OnEvent(Endpoint* endpoint, Event* event) override
    {
        EventHandlerBase::OnEvent(endpoint, event);
        if (event->level() > adk::io_engine::EventLevel::kInfo)
        {
            endpoint->Close();
            g_is_running = false;
        }
    }
};

class ConnectHandler final : public ConnectHandlerBase
{
public:
    void OnConnect(Endpoint* endpoint, Property& ep_props) override
    {
        ConnectHandlerBase::OnConnect(endpoint, ep_props);
        g_tcp_endpoint = endpoint;
    }
};

class MessageHandlerBaseEx : public adk::io_engine::MessageHandler
{
public:
    MessageHandlerBaseEx()
    {
        Reset();
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

protected:
    void Reset()
    {
        min_ = 0xffffffff;
        max_ = 0;
        total_ = 0;
        counter_ = 0;
        reset_ = false;
    }

    bool reset_;
    uint64_t min_;
    uint64_t max_;
    uint64_t total_;
    uint64_t counter_;
};

template<bool kPingPong>
class MessageHandler final : public MessageHandlerBaseEx
{
public:
    int32_t OnMessage(Message* message) override
    {
        if (ACCESS_ONCE(reset_))
        {
            Reset();
        }

        const char* msg_data = message->const_data();
        const char* consume_data = msg_data;

        int32_t data_more = -1;
        uint32_t left_len = message->data_len();

        struct timespec current_tp;
        clock_gettime(CLOCK_MONOTONIC, &current_tp);

        do
        {
            if (left_len < 4)
            {
                break;
            }

            const uint32_t data_len = *((uint32_t*)consume_data);
            if (left_len >= data_len)
            {
                struct timespec* message_tp = (struct timespec*)(consume_data + sizeof(uint32_t));
                const auto time_diff = current_tp.tv_sec * 1000000000 + current_tp.tv_nsec
                                     - message_tp->tv_sec * 1000000000 - message_tp->tv_nsec;

                min_ = std::min<uint64_t>(min_, time_diff);
                max_ = std::max<uint64_t>(max_, time_diff);
                total_ += time_diff;
                ++counter_;

                if (kPingPong)
                {
                    Message* message = g_tcp_endpoint->NewMessage(data_len);
                    message->set_data_len(data_len);

                    *(uint32_t*)(message->data()) = data_len;
                    clock_gettime(CLOCK_MONOTONIC, (struct timespec*)(message->data() + sizeof(uint32_t)));
                    g_tcp_endpoint->SendMsg(message);
                }

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

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("message-ip", boost::program_options::value<string>()->default_value(string()), "set message ip address")
        ("duplex-io", "duplex io actor")
        ("remote-ip", boost::program_options::value<string>(), "set remote ip address")
        ("remote-port", boost::program_options::value<uint16_t>()->default_value(50000), "set remote port")
        ("message-size", boost::program_options::value<uint32_t>()->default_value(128), "set message size")
        ("transmit-rate", boost::program_options::value<uint32_t>()->default_value(0), "set transmit rate")
        ("tx-low-latency", boost::program_options::value<bool>()->default_value(true), "set tx low latency")
        ("rx-low-latency", boost::program_options::value<bool>()->default_value(true), "set rx low latency")
        ("tcp-no-delay", "set tcp no delay")
    ;

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);
    if (vm.count("help") || !vm.count("remote-ip"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    adk::TokenBucket* rate_control = nullptr;
    MessageHandlerBaseEx* message_handler = nullptr;
    const auto rate_limit = vm["transmit-rate"].as<uint32_t>();
    if (0 != rate_limit)
    {
        rate_control = adk::RateControl::GetInstance<adk::rate_unit::Second>((double)rate_limit, 
                                                                             rate_limit < 1000000);
        if (nullptr == rate_control)
        {
            std::cout << "Create rate control failed" << std::endl;
            return 0;
        }

        message_handler = new MessageHandler<false>;
    }
    else
    {
        message_handler = new MessageHandler<true>;
    }

    TcpEngine* const tcp_engine = TcpEngine::Create(Property()
        (adk::io_engine::config::kMessageIp, vm["message-ip"].as<string>())
     	(adk::io_engine::config::kUseDuplexIOActor, vm.count("duplex-io"))
        (adk::io_engine::config::kIsRxLowLatency, vm["tx-low-latency"].as<bool>())
        (adk::io_engine::config::kIsTxLowLatency, vm["rx-low-latency"].as<bool>()));
    if (nullptr == tcp_engine)
    {
        std::cout << "create tcp engine failed" << std::endl;
        return -1;
    }

    EventHandler   event_handler;
    ConnectHandler connect_handler;
    if (nullptr == tcp_engine->Connect(Property()
        (adk::io_engine::config::endpoint::kRemoteIp, vm["remote-ip"].as<string>())
        (adk::io_engine::config::endpoint::kRemotePort, vm["remote-port"].as<uint16_t>())
        (adk::io_engine::config::endpoint::kTxMinResidentMicro, 1000000)
        (adk::io_engine::config::endpoint::kRxMinResidentMicro, 1000000)
        (adk::io_engine::config::endpoint::kEventHandler, &event_handler)
        (adk::io_engine::config::endpoint::kConnectHandler, &connect_handler)
        (adk::io_engine::config::endpoint::kMessageHandler, message_handler)
        (adk::io_engine::config::endpoint::kTcpNoDelay, !!(vm.count("tcp-no-delay")))))
    {
        std::cout << "connect failed <" << tcp_engine->GetLastError() << ">" << std::endl;
        return 0;
    }

    while (nullptr == g_tcp_endpoint)
    {
        usleep(0);
    }

    std::thread ob_thd = std::thread([&]() {
        while (g_is_running)
        {
            sleep(1);
            message_handler->PrintStatistics();
        }
    });

    const auto message_size = std::max<uint32_t>(vm["message-size"].as<uint32_t>(),
                                                 sizeof(uint32_t) + sizeof(struct timespec));
    if (nullptr != rate_control)
    {
        while (g_is_running)
        {
            Message* message = g_tcp_endpoint->NewMessage(message_size);
            message->set_data_len(message_size);

        retry_acq:
            if (adk::ErrorCode::kSuccess == rate_control->TryAcquire(1))
            {
                *(uint32_t*)(message->data()) = message_size;
                clock_gettime(CLOCK_MONOTONIC, (struct timespec*)(message->data() + sizeof(uint32_t)));
                g_tcp_endpoint->SendMsg(message);
            }
            else
            {
                ADK_PAUSE();
                goto retry_acq;
            }
        }
    }
    else
    {
        Message* message = g_tcp_endpoint->NewMessage(message_size);
        message->set_data_len(message_size);

        *(uint32_t*)(message->data()) = message_size;
        clock_gettime(CLOCK_MONOTONIC, (struct timespec*)(message->data() + sizeof(uint32_t)));
        g_tcp_endpoint->SendMsg(message);
    }

    ob_thd.join();
    return 0;
}