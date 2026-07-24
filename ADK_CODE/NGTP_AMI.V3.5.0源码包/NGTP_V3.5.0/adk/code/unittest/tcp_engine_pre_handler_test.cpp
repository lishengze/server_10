#define BOOST_TEST_MODULE tcp_engine
#include <boost/test/included/unit_test.hpp>

#include <mutex>
#include <unistd.h>
#include <iostream>
#include <adk/io_engine.h>
#include <boost/format.hpp>

class PreSendHandler final : public adk::io_engine::PreSendHandler
{
public:
    int32_t OnTxMessageBefore(void* ep_share_ctx, adk::io_engine::Message* message) override
    {
        {
            std::lock_guard<std::mutex> _(mutex_);
            ++counter_;
        }

        std::cout << "OnTxMessageBefore: counter <" << counter_ << ">" << std::endl;
        
        // delay time increase the probalility of recurrence
        usleep(1000);

        {
            std::lock_guard<std::mutex> _(mutex_);
            ++counter_;
        }
        return adk::ErrorCode::kSuccess;
    }

    void OnTxMessageAfter(void* ep_share_ctx, int32_t result) override
    {
        {
            std::lock_guard<std::mutex> _(mutex_);
            ++counter_;
        }

        std::cout << "OnTxMessageAfter: counter <" << counter_ << ">" << std::endl;
        
        // delay time increase the probalility of recurrence
        usleep(1000);

        {
            std::lock_guard<std::mutex> _(mutex_);
            ++counter_;
        }
    }

    uint32_t counter()
    {
        std::lock_guard<std::mutex> _(mutex_);
        return counter_;
    }

private:
    std::mutex mutex_;
    uint32_t   counter_ = 0;
};

class PreRecvHandler final : public adk::io_engine::PreRecvHandler
{
public:
    void OnRxMessageBefore(void* share_ctx) override
    {
        {
            std::lock_guard<std::mutex> _(mutex_);
            ++counter_;
        }

        std::cout << "OnRxMessageBefore: counter <" << counter_ << ">" << std::endl;

        // delay time increase the probalility of recurrence
        usleep(1000);

        {
            std::lock_guard<std::mutex> _(mutex_);
            ++counter_;
        }
    }

    void OnRxMessageAfter(void* share_ctx, int32_t result) override
    {
        {
            std::lock_guard<std::mutex> _(mutex_);
            ++counter_;
        }

        std::cout << "OnRxMessageAfter: counter <" << counter_ << ">" << std::endl;

        // delay time increase the probalility of recurrence
        usleep(1000);

        {
            std::lock_guard<std::mutex> _(mutex_);
            ++counter_;
        }
    }

    uint32_t counter()
    {
        std::lock_guard<std::mutex> _(mutex_);
        return counter_;
    }
private:
    std::mutex mutex_;
    uint32_t   counter_ = 0;
};

class EventHandler final : public adk::io_engine::EventHandler
{
public:
    EventHandler(PreSendHandler& pre_send_handler, PreRecvHandler& pre_recv_handler)
    {
        pre_send_handler_ = &pre_send_handler;
        pre_recv_handler_ = &pre_recv_handler;
    }

    void OnEvent(adk::io_engine::Endpoint* endpoint, adk::io_engine::Event* event) override
    {
        if (event->level() > adk::io_engine::EventLevel::kInfo)
        {
            endpoint->Close();
        }

        /**
         * 1. Add delay in PreXHandler callback 
         * 
         * 2. deliver closed event 
         *      -> Check has no PreXHandler callback on way
         * 
         * 3. if check error set check result error
         */
        if (adk::io_engine::EventType::kEndpointClosed == event->type())
        {
            if ((pre_send_handler_->counter() & 0x1) || (pre_recv_handler_->counter() & 0x1))  // 不能为奇数
            {
                check_result_ = false;
            }

            std::lock_guard<std::mutex> _(mutex_);
            endpoint_valid_ = false;
        }
    }

    bool check_result() const
    {
        return check_result_;
    }

    void set_endpoint_valid()
    {
        std::lock_guard<std::mutex> _(mutex_);
        if (endpoint_valid_)
        {
            std::cout << "endpoint is valid" << std::endl;
        }
        endpoint_valid_ = true;
    }

    bool endpoint_valid()
    {
        std::lock_guard<std::mutex> _(mutex_);
        return endpoint_valid_;
    }

private:
    PreSendHandler* pre_send_handler_;
    PreRecvHandler* pre_recv_handler_;
    volatile bool   check_result_ = true;

    std::mutex      mutex_;
    volatile bool   endpoint_valid_ = false;
};

class AcceptHandler final : public adk::io_engine::AcceptHandler
{
public:
    void OnAccept(adk::io_engine::Endpoint* endpoint, adk::Property& ep_props) override
    {
        std::cout << (boost::format("Endpoint<%1%:%2%><%3%> OnAccept: remote address<%4%:%5%>")
                                    % endpoint->endpoint_id() 
                                    % endpoint->sub_index() 
                                    % endpoint
                                    % endpoint->remote_ip() 
                                    % endpoint->remote_port()).str() << std::endl;
    }
};

class ConnectHandler final : public adk::io_engine::ConnectHandler
{
public:
    ConnectHandler(EventHandler& event_handler)
    {
        event_handler_ = &event_handler;
    }

    void OnConnect(adk::io_engine::Endpoint* endpoint, adk::Property& ep_props) override
    {
        std::cout << (boost::format("Endpoint<%1%:%2%><%3%> OnConnect: remote address<%4%:%5%>")
                                    % endpoint->endpoint_id() 
                                    % endpoint->sub_index()
                                    % endpoint
                                    % endpoint->remote_ip() 
                                    % endpoint->remote_port()).str() << std::endl;

        event_handler_->set_endpoint_valid();
    }

private:
    EventHandler* event_handler_;
};

class MessageHandler final : public adk::io_engine::MessageHandler
{
public:
    int32_t OnMessage(adk::io_engine::Message* message) override
    {
        std::cout << "OnMessage: data length <" << message->data_len() << ">" << std::endl;
        return adk::io_engine::MessageHandler::kSuccess;
    }
};

BOOST_AUTO_TEST_CASE(pre_x_handler)
{
    constexpr uint16_t kTestPort = 50000;

    AcceptHandler  accept_handler;
    MessageHandler server_message_handler;
    PreSendHandler server_pre_send_handler;
    PreRecvHandler server_pre_recv_handler;
    EventHandler   server_event_handler(server_pre_send_handler, server_pre_recv_handler);

    adk::Property server_engine_props;
    server_engine_props
        (adk::io_engine::config::kEventHandler, &server_event_handler)
        (adk::io_engine::config::kAcceptHandler, &accept_handler)
        (adk::io_engine::config::kPreSendHandler, &server_pre_send_handler)
        (adk::io_engine::config::kPreRecvHandler, &server_pre_recv_handler)
        ;

    auto* const server_engine = adk::io_engine::TcpEngine::Create(server_engine_props);
    BOOST_REQUIRE(server_engine);

    adk::Property accepts_props;
    accepts_props
        (adk::io_engine::config::acceptor::kReuseAddr, true)
        (adk::io_engine::config::acceptor::kListenPort, kTestPort)
        (adk::io_engine::config::acceptor::kMessageHandler, &server_message_handler)
        ;

    auto* const acceptor = server_engine->Accept(accepts_props);
    BOOST_REQUIRE(acceptor);

    MessageHandler client_message_handler;
    PreSendHandler client_pre_send_handler;
    PreRecvHandler client_pre_recv_handler;
    EventHandler   client_event_handler(client_pre_send_handler, client_pre_recv_handler);
    ConnectHandler connect_handler(client_event_handler);

    adk::Property client_engine_props;
    client_engine_props
        (adk::io_engine::config::kEventHandler, &client_event_handler)
        (adk::io_engine::config::kConnectHandler, &connect_handler)
        (adk::io_engine::config::kPreSendHandler, &client_pre_send_handler)
        (adk::io_engine::config::kPreRecvHandler, &client_pre_recv_handler)
        ;

    auto* const client_engine = adk::io_engine::TcpEngine::Create(client_engine_props);
    BOOST_REQUIRE(client_engine);

    adk::Property connect_props;
    connect_props
        (adk::io_engine::config::endpoint::kRemoteIp, "127.0.0.1")
        (adk::io_engine::config::endpoint::kRemotePort, kTestPort)
        (adk::io_engine::config::endpoint::kMessageHandler, &client_message_handler)
        ;

    /**
     * Loop
     *      ->
     *      1. client create endpoint
     * 
     *      2. wait connect success
     * 
     *      3. Loop
     *          -> SendMsg to trig PreXHandler Callback
     * 
     *      4. Close endpoint
     *          -> Wait endpoint closed event deliverd
     * 
     * 6. Destroy server engine
     *    Destroy Client engine
     *
     * 7. Check test result
     */
    for (size_t test_index = 0; test_index < 8; ++test_index)
    {
        server_event_handler.set_endpoint_valid();
        auto* const endpoint = client_engine->Connect(connect_props);
        BOOST_REQUIRE(endpoint);

        while (!client_event_handler.endpoint_valid())
        {
            usleep(0);
        }

        for (size_t msg_index = 0; msg_index < 1024; ++msg_index)
        {
            BOOST_REQUIRE(endpoint->SendMsg("Hello world!", 13) == adk::ErrorCode::kSuccess);
            usleep(0);
        }

        endpoint->Close();

        while (client_event_handler.endpoint_valid() || server_event_handler.endpoint_valid())
        {
            usleep(0);
        }
    }

    adk::io_engine::TcpEngine::Destroy(server_engine);
    adk::io_engine::TcpEngine::Destroy(client_engine);

    BOOST_REQUIRE(server_event_handler.check_result());
    BOOST_REQUIRE(client_event_handler.check_result());
}