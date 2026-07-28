#include <unistd.h>

#include <string>

#include <boost/format.hpp>
#include <boost/date_time.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <adk/property.h>
#include <adk/io_engine.h>
#include <adk/arch/generic.h>
#include <adk/token_buckets.h>

using adk::ErrorCode;
using TcpEndpoint = adk::io_engine::Endpoint;

struct MessageHeader
{
    uint32_t        message_len;
    struct timespec timestamp11;
    struct timespec timestamp12;
    struct timespec timestamp21;
    struct timespec timestamp22;
    char            data[];

    char* msg_data()
    {
        return data;
    }
};

constexpr uint32_t kMessageHeaderSize = sizeof(uint32_t);
constexpr auto     kMessageDataOffset = (size_t)&(((MessageHeader*)0)->data);
constexpr uint32_t kMinMessageSize = 128;

volatile bool g_is_running = false;

template<typename DerivedHandler>
class AcceptHandler : public adk::io_engine::AcceptHandler
{
public:
    void OnAccept(TcpEndpoint* endpoint, adk::Property& ep_props) override
    {
        std::cout << (boost::format("Endpoint<%1%:%2%><%3%> OnAccept: remote address<%4%:%5%>")
                                    % endpoint->endpoint_id() 
                                    % endpoint->sub_index() 
                                    % endpoint
                                    % endpoint->remote_ip() 
                                    % endpoint->remote_port()).str() << std::endl;

        adk::io_engine::MessageHandler* const message_handler = static_cast<DerivedHandler*>(this)->OnAccept(endpoint);
        if (nullptr != message_handler)
        {
            ep_props.SetValue(adk::io_engine::config::endpoint::kMessageHandler, message_handler);
        }
    }
};

template<typename DerivedHandler>
class ConnectHandler : public adk::io_engine::ConnectHandler
{
public:
    void OnConnect(TcpEndpoint* endpoint, adk::Property& ep_props) override
    {
        std::cout << (boost::format("Endpoint<%1%:%2%><%3%> OnConnect: remote address<%4%:%5%>")
                                    % endpoint->endpoint_id() 
                                    % endpoint->sub_index()
                                    % endpoint
                                    % endpoint->remote_ip() 
                                    % endpoint->remote_port()).str() << std::endl;

        static_cast<DerivedHandler*>(this)->OnConnect(endpoint);
    }
};

template<bool kIsServer>
class EventHandler : public adk::io_engine::EventHandler
{
public:
    void OnEvent(TcpEndpoint* endpoint, adk::io_engine::Event* event) override
    {
        std::cout << (boost::format("Endpoint<%1%:%2%><%3%> OnEvent: level<%4%> type<%5%> what<%6%>")
                                    % endpoint->endpoint_id()
                                    % endpoint->sub_index()
                                    % endpoint
                                    % event->level()
                                    % event->type()
                                    % event->what()).str() << std::endl;

        if (event->level() > adk::io_engine::EventLevel::kInfo)
        {
            g_is_running = false;
            if (kIsServer)
            {
                endpoint->Close();
            }
        }
    }
};

using EventHandlerClient = EventHandler<false>;
using EventHandlerServer = EventHandler<true>;

template<typename DerivedHandler>
class MessageHandler : public adk::io_engine::MessageHandler
{
public:
    int32_t OnMessage(adk::io_engine::Message* message) override
    {
        const char* msg_data = message->const_data();
        uint32_t left_len = message->data_len();

        do 
        {
            if (ADK_UNLIKELY(left_len < kMessageHeaderSize))
            {
                const auto consume_len = (uint32_t)(msg_data - message->const_data());
                message->set_follow_up(consume_len, -1);
                return adk::io_engine::MessageHandler::kFollowUp;
            }

            const auto msg_len = ((struct MessageHeader*)msg_data)->message_len;
            if (ADK_UNLIKELY(left_len < msg_len))
            {
                const auto consume_len = (uint32_t)(msg_data - message->const_data());
                message->set_follow_up(consume_len, msg_len - left_len);
                return adk::io_engine::MessageHandler::kFollowUp;
            }

            static_cast<DerivedHandler*>(this)->OnMessage((struct MessageHeader*)(msg_data));

            left_len -= msg_len;
            msg_data += msg_len;
        } while (left_len > 0);

        return adk::io_engine::MessageHandler::kSuccess;
    }
};

class DecodeTemplate : public adk::io_engine::DecodeTemplate
{
public:
    int32_t MessageLength(const void* msg_data, uint32_t len) override
    {
        if (len > kMessageHeaderSize)
        {
            return ((struct MessageHeader*)msg_data)->message_len;
        }

        return -1;
    }
};

using boost::program_options::variables_map;

bool client_options(int argc, char* argv[], variables_map& vm)
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("remote-ip", boost::program_options::value<std::string>(), "set remote ip address")
        ("remote-port", boost::program_options::value<uint16_t>()->default_value(50000), "set remote port")
        ("message-ip", boost::program_options::value<std::string>(), "set message ip")
        ("message-size", boost::program_options::value<uint32_t>()->default_value(128), "set message size")
        ("transmit-rate", boost::program_options::value<double>()->default_value(100000), "set transmit rate")
        ("tx-low-latency", boost::program_options::value<bool>()->default_value(true), "set tx low latency")
        ("rx-low-latency", boost::program_options::value<bool>()->default_value(true), "set rx low latency")
        ("tcp-no-delay", "set tcp no delay")
        ;

    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    if (vm.count("help") || !vm.count("remote-ip"))
    {
        std::cout << desc << std::endl;
        return false;
    }

    return true;
}

bool server_options_blank(int argc, char* argv[], variables_map& vm)
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("listen-port", boost::program_options::value<uint16_t>()->default_value(50000), "set server listen port")
        ;

    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    if (vm.count("help"))
    {
        std::cout << desc << std::endl;
        return false;
    }

    return true;
}

bool server_options(int argc, char* argv[], variables_map& vm)
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("message-ip", boost::program_options::value<std::string>(), "set message ip")
        ("listen-port", boost::program_options::value<uint16_t>()->default_value(50000), "set server listen port")
        ("tx-low-latency", boost::program_options::value<bool>()->default_value(true), "set tx low latency")
        ("rx-low-latency", boost::program_options::value<bool>()->default_value(true), "set rx low latency")
        ("tcp-no-delay", "set tcp no delay")
        ;

    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    if (vm.count("help"))
    {
        std::cout << desc << std::endl;
        return false;
    }

    return true;
}