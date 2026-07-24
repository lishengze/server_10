#include <vector>
#include <string>
#include <thread>
#include <iostream>
#include <adk_pack/error_code.h>
#include <adk_pack/io_engine.h>
#include <boost/format.hpp>
#include <boost/date_time.hpp>

#define ADK_UNLIKELY(X) X

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

constexpr uint32_t kPayloadSize = 64;
constexpr uint32_t kMaxConnections = 8192;

class HeartbeatHandlerBase : public adk::io_engine::HeartbeatHandler
{
public:
    HeartbeatHandlerBase()
    {
        char* const buffer = hrbt_message_;
        *((uint64_t*)buffer) = 0;
    }

    void SendHBMsg(Endpoint* endpoint)
    {
        endpoint->SendMsg(hrbt_message_, kPayloadSize);
        std::cout << boost::posix_time::ptime(boost::posix_time::microsec_clock::local_time()) 
                  << " SendHBMsg" << std::endl;
    }

    virtual uint32_t GetPeriodMilli()
    {
        return 3000;
    }
private:
    char hrbt_message_[kPayloadSize];
};

class EventHandlerBase : public adk::io_engine::EventHandler
{
public:
    void OnEvent(Endpoint* endpoint, Event* event) override
    {
        string detail = (format("Endpoint<%1%:%2%><%6%> OnEvent: level<%3%> type<%4%> what<%5%>")
            % endpoint->endpoint_id() % endpoint->sub_index()
            % event->level() % event->type() % event->what() % endpoint).str();
        std::cout << detail << std::endl;
    }
};

class AcceptHandlerBase : public adk::io_engine::AcceptHandler
{
public:
    void OnAccept(Endpoint* endpoint, Property& ep_props) override
    {
        string detail = (format("Endpiont<%1%:%2%><%3%> OnAccept: remote address<%4%:%5%>")
            % endpoint->endpoint_id() % endpoint->sub_index() % endpoint
            % endpoint->remote_ip() % endpoint->remote_port()).str();
        std::cout << detail << std::endl;
    }
};

class ConnectHandlerBase : public adk::io_engine::ConnectHandler
{
public:
    void OnConnect(Endpoint* endpoint, Property& ep_props) override
    {
        string detail = (format("Endpoint<%1%:%2%><%5%> OnConnect: remote address<%3%:%4%>")
            % endpoint->endpoint_id() % endpoint->sub_index()
            % endpoint->remote_ip() % endpoint->remote_port() % endpoint).str();
        std::cout << detail << std::endl;
    }
};

class MessageHandlerBase : public adk::io_engine::MessageHandler
{
public:
    MessageHandlerBase()
    {
        is_running_ = true;
        recv_counter_ = 0;
        expect_recv_value_ = 0;
        ob_thread_hdl_ = std::thread(&MessageHandlerBase::Observer, this);
    }

    ~MessageHandlerBase()
    {
        is_running_ = false;
        ob_thread_hdl_.join();
    }

    int32_t OnMessage(Message* message) override
    {
        const char* msg_data = message->const_data();
        uint32_t left_len = message->data_len();

        const char* consume_data = msg_data;
        while (left_len >= kPayloadSize)
        {
            const uint64_t recv_data = *((uint64_t*)consume_data);
            if (recv_data != expect_recv_value_)
            {
                Endpoint* const endpoint = message->endpoint();
                string detail = (format("Endpoint<%1%> OnMessage: recv<%2%> expect<%3%>")
                    % endpoint->endpoint_id() % recv_data % expect_recv_value_).str();
                std::cout << detail << std::endl;
            }

            ++recv_counter_;
            expect_recv_value_ = recv_data + 1;
            left_len -= kPayloadSize;
            consume_data += kPayloadSize;
        }        

        if (0 != left_len)
        {
            message->set_follow_up(consume_data - msg_data, kPayloadSize - left_len);
            return adk::io_engine::MessageHandler::Result::kFollowUp;
        }

        return ErrorCode::kSuccess;
    }

protected:
    void Observer()
    {
        uint64_t last_record = 0;
        while (is_running_)
        {
            sleep(1);
            const uint64_t temp_data = recv_counter_;
            std::cout << "OnMessage: data diff = " << temp_data - last_record << std::endl;
            last_record = temp_data;            
        }
    }

    volatile bool is_running_;
    std::thread ob_thread_hdl_;

    uint64_t recv_counter_;
    uint64_t expect_recv_value_;
};
