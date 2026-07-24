#include "test_tcp_engine.h"

#include <boost/locale.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <adk/token_buckets.h>

volatile bool g_is_running = true;
uint32_t g_message_size = sizeof(struct timespec);

class EventHandler final : public EventHandlerBase
{
public:
    void OnEvent(Endpoint* endpoint, Event* event) override
    {
        EventHandlerBase::OnEvent(endpoint, event);
        if (event->level() > adk::io_engine::EventLevel::kInfo)
        {
            endpoint->Shutdown();
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

        std::lock_guard<std::mutex> _(lock_);
        endpoints_vec_.push_back(endpoint);
    }

    void SendMessage()
    {
        std::lock_guard<std::mutex> _(lock_);
        for (auto& endpoint_ptr : endpoints_vec_)
        {
            Message* const message = endpoint_ptr->NewMessage(g_message_size);
            message->set_data_len(g_message_size);

            *(uint32_t*)(message->data()) = g_message_size;
            clock_gettime(CLOCK_REALTIME, (struct timespec*)(message->data() + sizeof(uint32_t)));
            endpoint_ptr->SendMsg(message);
        }
    }

private:
    std::mutex             lock_;
    std::vector<Endpoint*> endpoints_vec_;
};

class MessageHandler final : public adk::io_engine::MessageHandler
{
public:
    MessageHandler()
    {
        Reset();
    }

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
        clock_gettime(CLOCK_REALTIME, &current_tp);

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
};

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("remote-ip", boost::program_options::value<string>(), "set remote ip address")
        ("remote-port", boost::program_options::value<uint16_t>()->default_value(50000), "set remote port")
        ("message-size", boost::program_options::value<uint32_t>()->default_value(128), "set message size")
        ("thread-size", boost::program_options::value<uint32_t>()->default_value(10), "test thread size")
        ("per-thread-connections", boost::program_options::value<uint32_t>()->default_value(100), "perthread test connections")
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

    TcpEngine* const tcp_engine = TcpEngine::Create(Property()
        (adk::io_engine::config::kIsRxLowLatency, vm["tx-low-latency"].as<bool>())
        (adk::io_engine::config::kIsTxLowLatency, vm["rx-low-latency"].as<bool>()));
    if (nullptr == tcp_engine)
    {
        std::cout << "create tcp engine failed" << std::endl;
        return -1;
    }

    g_message_size = vm["message-size"].as<uint32_t>();

    Property       connect_props;
    EventHandler   event_handler;
    MessageHandler message_handler;

    connect_props
        (adk::io_engine::config::endpoint::kRemoteIp, vm["remote-ip"].as<string>())
        (adk::io_engine::config::endpoint::kRemotePort, vm["remote-port"].as<uint16_t>())
        (adk::io_engine::config::endpoint::kTxMinResidentMicro, 1)
        (adk::io_engine::config::endpoint::kRxMinResidentMicro, 1)
        (adk::io_engine::config::endpoint::kEventHandler, &event_handler)
        (adk::io_engine::config::endpoint::kMessageHandler, &message_handler)
        (adk::io_engine::config::endpoint::kTcpNoDelay, !!(vm.count("tcp-no-delay")))
    ;

    std::vector<std::thread>     threads_vec;
    std::vector<ConnectHandler*> connect_handler_vec;

    const auto thread_size = vm["thread-size"].as<uint32_t>();
    const auto per_thread_connections = vm["per-thread-connections"].as<uint32_t>();
    for (uint32_t thread_index = 0; thread_index < thread_size; ++thread_index)
    {
        ConnectHandler* connect_handler = new ConnectHandler;
        connect_handler_vec.push_back(connect_handler);
        connect_props(adk::io_engine::config::endpoint::kConnectHandler, connect_handler);
        for (uint32_t sub_index = 0; sub_index < per_thread_connections; ++sub_index)
        {
            if (nullptr == tcp_engine->Connect(connect_props))
            {
                std::cout << "connect failed <" << thread_index << ":" << sub_index 
                          << "> <" << tcp_engine->GetLastError() << ">" << std::endl;
            }
        }
    }

    std::thread trd = std::thread([&]() {
        while (g_is_running)
        {
            sleep(1);
            message_handler.PrintStatistics();
        }
    });

    while (g_is_running)
    {
        for (auto& connect_handler_ptr : connect_handler_vec)
        {
            connect_handler_ptr->SendMessage();
        }
        usleep(1000);
    }

    trd.join();
    return 0;
}