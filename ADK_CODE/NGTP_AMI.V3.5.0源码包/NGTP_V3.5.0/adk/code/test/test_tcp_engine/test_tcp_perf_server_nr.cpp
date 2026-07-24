#include <memory>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <thread>
#include <iostream>
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

std::atomic<int64_t> g_accept_cnt = { 0 };
std::atomic<int64_t> g_message_cnt = { 0 };
std::atomic<int64_t> g_message_len = { 0 };

using MessageQueue = adk::variant::SPSCQueue<adk::io_engine::Message*>;

std::mutex gclient_mtx;
std::map<adk::io_engine::Endpoint*, std::tuple<MessageQueue*, std::thread, bool>> gclients;

class EventHandler final : public adk::io_engine::EventHandler
{
public:
    void OnEvent(Endpoint* endpoint, Event* event) override
    {
        if (event->level() > adk::io_engine::EventLevel::kInfo)
        {
            std::unique_lock<std::mutex> lock(gclient_mtx);
            auto it = gclients.find(endpoint);
            if (it != gclients.end())
            {
                auto queue = std::get<0>(it->second);
                auto& thread = std::get<1>(it->second);
                auto& flag = std::get<2>(it->second);
                flag=false;

                queue->set_release_alert();
                thread.join();
                MessageQueue::Delete(queue);
            }

            gclients.erase(it);
            endpoint->Close(1);
        }
    }
};

class AcceptHandler final : public adk::io_engine::AcceptHandler
{
public:
    void OnAccept(Endpoint* endpoint, Property& ep_props) override
    {
        auto queue = MessageQueue::Create("tx_msg_queue", 512);
        if (queue == nullptr)
        {
            endpoint->Close(1);
            std::cout << "Create tx message queue failed" << std::endl;
            return;
        }

        std::unique_lock<std::mutex> lock(gclient_mtx);
        auto it = gclients.insert(std::make_pair(endpoint, std::make_tuple(queue, std::thread(),true))).first;
        lock.unlock();

        std::get<1>(it->second) = std::thread([it](){
            auto  ep = it->first;
            auto& tup = it->second;
            auto& running = std::get<2>(tup);
            auto  queue   = std::get<0>(tup);
            while (running)
            {
                adk::io_engine::Message *msg = nullptr;
                auto err = queue->Pop(msg);
                if (err == 0)
                {
                    ep->SendMsg(msg);
                }
            }
        });
    }
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
        std::lock_guard<std::mutex> _(mutex_);
        return counter_;
    }

private:
    std::mutex mutex_;
    std::atomic<int64_t>   counter_ = {0};
};


template<bool ECHO>
class MessageHandler : public adk::io_engine::MessageHandler
{
public:
    int32_t OnMessage(Message* message) override
    {
        const char* msg_data = message->const_data();
        const char* consume_data = msg_data;

        int32_t data_more = -1;
        uint32_t left_len = message->data_len();


        while(left_len >= 4)
        {
            const uint32_t data_len = *((uint32_t*)consume_data);
            if (left_len >= data_len)
            {
                if (ECHO)
                {
                    auto ep = message->endpoint();
                    Message* message = ep->NewMessage(data_len);
                    message->set_data_len(data_len);

                    *(uint32_t*)(message->data()) = data_len;
                    ep->SendMsg(message);
                }
                
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
        ("listen-port", boost::program_options::value<uint16_t>()->default_value(50000), "set server listen port")
        ("tx-low-latency", boost::program_options::value<bool>()->default_value(true), "set tx low latency")
        ("rx-low-latency", boost::program_options::value<bool>()->default_value(true), "set rx low latency")
        ("mode", boost::program_options::value<uint32_t>()->default_value(1), "set test mode [1: pingpong / 2: throughput / 3: lantency]")
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

    PreSendHandler  hps;
    TcpEngine* const tcp_engine = TcpEngine::Create(Property()
        (adk::io_engine::config::kPreSendHandler, &hps)
        (adk::io_engine::config::kTxThreadNum, 8)
        (adk::io_engine::config::kMessageIp, vm["message-ip"].as<string>())
        (adk::io_engine::config::kUseDuplexIOActor, vm.count("duplex-io"))
        (adk::io_engine::config::kIsRxLowLatency, vm["tx-low-latency"].as<bool>())
        (adk::io_engine::config::kIsTxLowLatency, vm["rx-low-latency"].as<bool>()));
    if (nullptr == tcp_engine)
    {
        std::cout << "create tcp engine failed" << std::endl;
        return -1;
    }

    bool running = true;
    uint32_t message_size = 128;
    std::thread producer = std::thread([&](){
        while(running)
        {
            auto msg = tcp_engine->NewMessage(message_size);
            if (msg == nullptr)
            {
                continue;
            }

            msg->set_data_len(message_size);

            *(uint32_t*)(msg->data()) = message_size;
            
            std::unique_lock<std::mutex> lock(gclient_mtx);
            msg->set_fanout(gclients.size());
            for (auto& client : gclients)
            {
                auto queue = std::get<0>(client.second);
                queue->Push(msg);
            }
            usleep(100000);
        }
    });

    EventHandler   event_handler;
    AcceptHandler  accept_handler;
    std::unique_ptr<adk::io_engine::MessageHandler> message_handler(new MessageHandler<true>);
   
    const uint16_t listen_port = vm["listen-port"].as<uint16_t>();
    if (nullptr == tcp_engine->Accept(Property()
        (adk::io_engine::config::acceptor::kListenPort, listen_port)
        (adk::io_engine::config::endpoint::kTxMinResidentMicro, 1000000)
        (adk::io_engine::config::endpoint::kRxMinResidentMicro, 1000000)
        (adk::io_engine::config::acceptor::kEventHandler, &event_handler)
        (adk::io_engine::config::acceptor::kAcceptHandler, &accept_handler)
        (adk::io_engine::config::endpoint::kMessageHandler, message_handler.get())
        (adk::io_engine::config::endpoint::kTcpNoDelay, !!(vm.count("tcp-no-delay")))))
    {
        std::cout << "create acceptor failed, listen port <" << listen_port 
                  << ">, error info <" << tcp_engine->GetLastError() 
                  << ">" << std::endl;
        return -1;
    }

    while (true)
    {
        sleep(3);
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME,&ts);

        struct tm tm; 
        char buff[100] = { 0 };
        strftime(buff, sizeof(buff), "%F %T.", localtime_r(&ts.tv_sec,&tm));
        snprintf(buff + strlen(buff), sizeof(buff) + strlen(buff), "%09lu", ts.tv_nsec);

        fprintf(stdout,"[%s] accepted:%ld\n", buff,  g_accept_cnt.load());
        fprintf(stdout,"[%s] message count:%ld\n", buff,  g_message_cnt.load());
        fprintf(stdout,"[%s] message bytes:%ld\n", buff,  g_message_len.load());

        string indicator;
        tcp_engine->CollectIndicator(indicator);
        std::cerr << indicator << std::endl;
        std::cout << std::endl;
    }

    TcpEngine::Destroy(tcp_engine);
    return 0;
}