#include <atomic>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <time.h>
#include <thread>
#include <mutex>
#include <boost/format.hpp>
#include <boost/date_time.hpp>

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <adk/lock_free_queue_variant.h>
#include <adk/io_engine.h>
#include <adk/error_code.h>
#include <adk/arch/generic.h>
#include <adk/rate_control.h>
#include <adk/token_buckets.h>

using std::string;
using std::thread;
using boost::format;
using adk::ErrorCode;
using adk::io_engine::Event;
using adk::io_engine::Message;
using adk::io_engine::Property;
using adk::io_engine::Acceptor;
using adk::io_engine::Endpoint;
using adk::io_engine::TcpEngine;

using adk::io_engine::EventType;
using adk::io_engine::EventLevel;

constexpr uint32_t kPayloadSize = 32;
constexpr uint32_t kMaxConnections = 1024;

volatile bool g_is_running = true;

std::atomic<int64_t> g_connect_cnt = { 0 };
std::atomic<int64_t> g_success_cnt = { 0 };
std::atomic<int64_t> g_failure_cnt = { 0 };
std::atomic<int64_t> g_message_cnt = { 0 };
std::atomic<int64_t> g_message_len = { 0 };
class EventHandler final : public adk::io_engine::EventHandler
{
public:
    EventHandler(uint32_t& finished)
    :finished_(finished)
    {}

    void OnEvent(Endpoint* endpoint, Event* event) override
    {
        //EventHandlerBase::OnEvent(endpoint, event);
        if (event->type() == EventType::kConnectFailed)
        {
            ++g_connect_cnt; 
            ++g_failure_cnt;
        }

        if (event->level() > adk::io_engine::EventLevel::kInfo)
        {
            finished_ = 2;
        }
    }

private:
    uint32_t& finished_;    
};

class PreSendHandler final : public adk::io_engine::PreSendHandler
{
public:
    int32_t OnTxMessageBefore(void* ep_share_ctx, adk::io_engine::Message* message) override
    {
       
        ++counter_;

        fprintf(stdout,"OnTxMessageBefore: counter <%ld>, message <%p>\n", counter_.load(), message);
        
        // delay time increase the probalility of recurrence
        usleep(1000);

        ++counter_;
        return adk::io_engine::PreSendHandler::PreTxResult::kCallOnce;
    }

    void OnTxMessageAfter(void* ep_share_ctx, int32_t result) override
    {
        ++counter_;

        fprintf(stdout,"OnTxMessageAfter: counter <%ld>\n", counter_.load());
        
        // delay time increase the probalility of recurrence
        usleep(1000);

        ++counter_;
    }

    uint32_t counter()
    {
        return counter_;
    }

private:
    std::mutex mutex_;
    std::atomic<int64_t>   counter_ = {0};
};

class ConnectHandler final : public adk::io_engine::ConnectHandler
{
public:
    void OnConnect(Endpoint* endpoint, Property& ep_props) override
    {
        ++g_connect_cnt; 
        ++g_success_cnt;
        finished_ = 1;
    }

    ConnectHandler(uint32_t& finished)
        :finished_(finished)
    {
    }
private:
    uint32_t& finished_ ; 
};

template<bool kPingPong>
class MessageHandler final : public adk::io_engine::MessageHandler
{
    std::atomic<uint32_t> count_;
    uint32_t& finished_;
public:
    MessageHandler(uint32_t& finished)
    :count_(0),finished_(finished)
    {

    }
    int32_t OnMessage(Message* message) override
    {
        const char* msg_data = message->const_data();
        const char* consume_data = msg_data;

        int32_t data_more = -1;
        uint32_t left_len = message->data_len();
        auto ep = message->endpoint();

        while(left_len >= 4)
        {
            const uint32_t data_len = *((uint32_t*)consume_data);
            if (left_len >= data_len)
            {
                if (kPingPong)
                {
                   
                    Message* message = ep->NewMessage(data_len);
                    message->set_data_len(data_len);

                    *(uint32_t*)(message->data()) = data_len;
                    ep->SendMsg(message);
                }
                ++count_;
                ++g_message_cnt;
                g_message_len += data_len;

                left_len -= data_len;
                consume_data += data_len;
            }
            else
            {
                data_more = data_len - left_len;
                break;
            }
        }

        if (count_ > 10000000)
        {
            finished_ = 2;
        }

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

    
    PreSendHandler  hps;
    TcpEngine* const tcp_engine = TcpEngine::Create(Property()
        (adk::io_engine::config::kTxThreadNum, 10)
        (adk::io_engine::config::kPreSendHandler, &hps)
        (adk::io_engine::config::kMessageIp, vm["message-ip"].as<string>())
        (adk::io_engine::config::kUseDuplexIOActor, vm.count("duplex-io"))
        (adk::io_engine::config::kIsRxLowLatency, vm["tx-low-latency"].as<bool>())
        (adk::io_engine::config::kIsTxLowLatency, vm["rx-low-latency"].as<bool>()));
        

    if (nullptr == tcp_engine)
    {
        std::cout << "create tcp engine failed" << std::endl;
        return -1;
    }
    
    adk::TokenBucket* rate_control = nullptr;
    auto rate_limit = vm["transmit-rate"].as<uint32_t>();
    if (rate_limit == 0)
    {
        rate_limit = 1;
    }

    rate_control = adk::RateControl::GetInstance<adk::rate_unit::Second>((double)rate_limit, rate_limit < 1000000);
    if (rate_control == nullptr)
    {
        std::cout << "Create rate control failed" << std::endl;
        return -1;
    }

    constexpr int client_count = 10;
    using MessageQueue = adk::variant::SPSCQueue<adk::io_engine::Message*>;
    MessageQueue* queues[client_count];
    for (auto i = 0 ; i != client_count; ++i)
    {
        queues[i] = MessageQueue::Create("tx_msg_queue", 4096);
        if (queues[i] == nullptr)
        {
             std::cout << "Create tx message queue failed" << std::endl;
             return -1;
        }
    }

    bool running = true;
    uint32_t message_size =  vm["message-size"].as<uint32_t>();
    std::thread producer = std::thread([&](){
        while(running)
        {
            rate_control->Acquire(1);
            auto msg = tcp_engine->NewMessage(message_size);
            if (msg == nullptr)
            {
                continue;
            }

            msg->set_data_len(message_size);

            *(uint32_t*)(msg->data()) = message_size;
            
            msg->set_fanout(client_count);
            for (auto i = 0; i != client_count; ++i)
            {
                queues[i]->Push(msg);
            }
        }
    });

    int cnt = client_count; 
    std::list<std::thread> clients;
    while (--cnt)
    {
        clients.emplace_back(std::thread([&](){
            auto idx = cnt; 
            while(running)
            {
                uint32_t  finished =0;
                EventHandler   event_handler(finished);
                ConnectHandler connect_handler(finished);

                std::unique_ptr<adk::io_engine::MessageHandler> message_handler;
                if (0 != rate_limit)
                {
                    message_handler = std::unique_ptr<adk::io_engine::MessageHandler>(new MessageHandler<false>(finished));
                }
                else
                {
                    message_handler = std::unique_ptr<adk::io_engine::MessageHandler>(new MessageHandler<true>(finished));
                }

                auto ep= tcp_engine->Connect(Property()
                    (adk::io_engine::config::endpoint::kRemoteIp, vm["remote-ip"].as<string>())
                    (adk::io_engine::config::endpoint::kRemotePort, vm["remote-port"].as<uint16_t>())
                    (adk::io_engine::config::endpoint::kTxMinResidentMicro, 1000000)
                    (adk::io_engine::config::endpoint::kRxMinResidentMicro, 1000000)
                    (adk::io_engine::config::endpoint::kReuseAddr, true)
                    (adk::io_engine::config::endpoint::kEventHandler, &event_handler)
                    (adk::io_engine::config::endpoint::kConnectHandler, &connect_handler)
                    (adk::io_engine::config::endpoint::kMessageHandler, message_handler.get())
                    (adk::io_engine::config::endpoint::kTcpNoDelay, !!(vm.count("tcp-no-delay"))));
                if (ep == nullptr)
                {
                   sleep(1);
                   continue;
                }

                while (!finished)
                {
                    usleep(1);
                }

                if (finished == 2)
                {
                    ep->Close(1);
                    std::cout<< "endpoint finished" << std::endl;
                    continue;
                }

                while (finished == 1 && running)
                {
                    adk::io_engine::Message *msg = nullptr;
                    auto err = queues[idx]->Pop(msg);
                    if (err == 0)
                    {
                        ep->SendMsg(msg);
                    }
                }
                ep->Close(1);
                std::cout<< "endpoint finished" << std::endl;
            }
        }));
    }

    while(true)
    {
        sleep(3);

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME,&ts);

        struct tm tm; 
        char buff[100] = { 0 };
        strftime(buff, sizeof(buff), "%F %T.", localtime_r(&ts.tv_sec,&tm));
        snprintf(buff + strlen(buff), sizeof(buff) + strlen(buff), "%09lu", ts.tv_nsec);

        fprintf(stdout,"[%s] connect count:%ld\n", buff,  g_connect_cnt.load());
        fprintf(stdout,"[%s] success count:%ld\n", buff,  g_success_cnt.load());
        fprintf(stdout,"[%s] failure count:%ld\n", buff,  g_failure_cnt.load());
        fprintf(stdout,"[%s] message count:%ld\n", buff,  g_message_cnt.load());
        fprintf(stdout,"[%s] message bytes:%ld\n", buff,  g_message_len.load());

        string indicator;
        tcp_engine->CollectIndicator(indicator);
        std::cerr << indicator << std::endl;
        std::cout << std::endl;
    }


    running = false;
    for ( auto & t : clients)
    {
        t.join();
    }
    return 0;
}
