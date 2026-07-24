#define BOOST_TEST_MODULE sk_verbs

#include <string>
#include <string.h>

#include <boost/format.hpp>
#include <tcp_verbs/tcp_interface.h>
#include <boost/test/included/unit_test.hpp>

struct EPollTask
{
    int32_t task_type;
    void*   task_context;
};

BOOST_AUTO_TEST_CASE(test_TcpStackSk)
{
    constexpr size_t kRecvBufferSize = 1024;
    char  buffer[kRecvBufferSize];

    const std::string kRemoteIp("127.0.0.1");
    const std::string kServerIp("127.0.0.1");
    constexpr uint16_t kTestPort = 50000;

    auto* const tcp_stack = adk::verbs::ITcpStack::Create(std::string());
    BOOST_REQUIRE(tcp_stack);

    BOOST_REQUIRE(tcp_stack->io_parallel_support());
    BOOST_REQUIRE_EQUAL(tcp_stack->ReactorPerform(), 1);
    BOOST_REQUIRE_EQUAL(tcp_stack->message_ip(), std::string());
    BOOST_REQUIRE(tcp_stack->stack_type() == adk::verbs::ITcpStack::StackType::kStackSk);

    auto* const tcp_epoller = adk::verbs::ITcpEPoller::Create(tcp_stack);
    BOOST_REQUIRE(tcp_epoller);

    auto* acceptor = adk::verbs::ITcpAcceptor::Create(tcp_stack, std::string(), kTestPort, true, true);
    BOOST_REQUIRE(acceptor);

    BOOST_CHECK_EQUAL(acceptor->listen_ip(), std::string());
    BOOST_CHECK_EQUAL(acceptor->listen_port(), kTestPort);

    BOOST_TEST_MESSAGE((boost::format("create accpetor <%1%> <%2%:%3%> success")
                        % acceptor
                        % acceptor->listen_ip()
                        % acceptor->listen_port()).str());

    EPollTask accept_task = { 1, acceptor };
    BOOST_REQUIRE(tcp_epoller->EPollAdd(acceptor, &accept_task));
    BOOST_TEST_MESSAGE((boost::format("EPollAdd acceptor <%1%> success") % acceptor).str());

    auto endpoint_client = adk::verbs::ITcpEndpoint::Create(tcp_stack, true, true);
    BOOST_REQUIRE(endpoint_client);

    BOOST_REQUIRE_EQUAL(endpoint_client->Bind(0), 
                        static_cast<int32_t>(adk::verbs::ITcpEndpoint::BindResult::kSuccess));

    BOOST_REQUIRE_EQUAL(endpoint_client->Connect(kServerIp, kTestPort), 
                        static_cast<int32_t>(adk::verbs::ITcpEndpoint::ConnectResult::kInProgress));

    BOOST_TEST_MESSAGE((boost::format("create endpoint <%1%> <%2%:%3%>-<%4%:%5%> success")
                        % endpoint_client
                        % endpoint_client->local_ip()
                        % endpoint_client->local_port()
                        % endpoint_client->remote_ip()
                        % endpoint_client->remote_port()).str());

    EPollTask connect_task = { 2, endpoint_client };
    BOOST_REQUIRE(tcp_epoller->EPollAdd(endpoint_client, &connect_task));
    BOOST_TEST_MESSAGE((boost::format("EPollAdd client endpoint <%1%> success") 
                        % endpoint_client).str());

    bool connect_result = false;
    adk::verbs::ITcpEndpoint* accept_endpoint = nullptr;
    for (int32_t try_index = 0; try_index < 2; ++try_index)
    {
        struct epoll_event events[2];
        const auto epoll_nr = tcp_epoller->EPollWait(events, 2, 1000);
        for (int32_t index = 0; index < epoll_nr; ++index)
        {
            auto& event_node = events[index];
            EPollTask* epoll_task = (EPollTask*)event_node.data.ptr;
            if (1 == epoll_task->task_type)
            {
                BOOST_REQUIRE(nullptr == accept_endpoint);
                BOOST_REQUIRE_EQUAL(epoll_task->task_context, acceptor);
                BOOST_CHECK(adk::verbs::ITcpAcceptor::EPollResult(event_node.events));

                accept_endpoint = acceptor->Accept();
                BOOST_REQUIRE(accept_endpoint);
				BOOST_TEST_MESSAGE((boost::format("Server accept a new connection <%1%>") 
                                    % accept_endpoint).str());
            }
            else if (2 == epoll_task->task_type)
            {
                BOOST_REQUIRE(!connect_result);
                BOOST_REQUIRE_EQUAL(epoll_task->task_context, endpoint_client);
                connect_result = endpoint_client->EPollResult(event_node.events);
                BOOST_CHECK(connect_result);

				BOOST_TEST_MESSAGE((boost::format("Client endpoint <%1%> connect success") 
                                    % endpoint_client).str());

                BOOST_REQUIRE(tcp_epoller->EPollDel(endpoint_client));
                BOOST_REQUIRE(!tcp_epoller->EPollDel(endpoint_client));
            }
            else
            {
                BOOST_CHECK(false);
            }
        }

        if (connect_result && (nullptr != accept_endpoint))
        {
            break;
        }
    }

    BOOST_REQUIRE(connect_result);
    BOOST_REQUIRE(nullptr != accept_endpoint);

    BOOST_REQUIRE_GT(endpoint_client->endpoint_id(), 0);
    BOOST_REQUIRE_GT(accept_endpoint->endpoint_id(), 0);

    BOOST_CHECK_EQUAL(accept_endpoint->remote_ip(), kRemoteIp);
    BOOST_CHECK_EQUAL(endpoint_client->remote_ip(), kServerIp);

    BOOST_CHECK_EQUAL(endpoint_client->remote_port(), kTestPort);
	BOOST_CHECK_EQUAL(accept_endpoint->local_port(), kTestPort);

    BOOST_TEST_MESSAGE((boost::format("Established, client endpoint <%1%:%2%>-<%3%:%4%>, accept endpoint <%5%:%6%>-<%7%:%8%>")
                        % endpoint_client->local_ip()
                        % endpoint_client->local_port()
                        % endpoint_client->remote_ip()
                        % endpoint_client->remote_port()
                        % accept_endpoint->remote_ip()
                        % accept_endpoint->remote_port()
                        % accept_endpoint->local_ip()
                        % accept_endpoint->local_port()).str());

	BOOST_TEST_MESSAGE((boost::format("Server close connection <%1%>") 
                        % accept_endpoint).str());

    BOOST_REQUIRE(!tcp_epoller->EPollDel(accept_endpoint));
    adk::verbs::ITcpEndpoint::Destroy(accept_endpoint);
    accept_endpoint = nullptr;

    ssize_t recv_result;
    do
    {
        recv_result = endpoint_client->Recv(buffer, kRecvBufferSize);
    } while (-1 == recv_result);

    BOOST_REQUIRE_EQUAL(recv_result, 0);
    BOOST_TEST_MESSAGE((boost::format("Client endpoint <%1%> recv EOF")
                        % endpoint_client).str());

    BOOST_REQUIRE(tcp_epoller->EPollAddR(endpoint_client, nullptr));
    BOOST_REQUIRE(!tcp_epoller->EPollAddR(endpoint_client, nullptr));

    {
        struct epoll_event event;
        BOOST_REQUIRE_EQUAL(tcp_epoller->EPollWait(&event, 1, 1000), 1);
        BOOST_REQUIRE(nullptr == event.data.ptr);

        BOOST_REQUIRE(tcp_epoller->EPollDel(endpoint_client));
        BOOST_REQUIRE(!tcp_epoller->EPollDel(endpoint_client));

        BOOST_TEST_MESSAGE((boost::format("Wait readable event of endpoint <%1%> after recv EOF")
                            % endpoint_client).str());
    }

    BOOST_REQUIRE(tcp_epoller->EPollAddW(endpoint_client, nullptr));
    BOOST_REQUIRE(!tcp_epoller->EPollAddW(endpoint_client, nullptr));

    {
        struct epoll_event event;
        BOOST_REQUIRE_EQUAL(tcp_epoller->EPollWait(&event, 1, 1000), 1);
        BOOST_REQUIRE(nullptr == event.data.ptr);

        BOOST_REQUIRE(tcp_epoller->EPollDel(endpoint_client));
        BOOST_REQUIRE(!tcp_epoller->EPollDel(endpoint_client));

        BOOST_TEST_MESSAGE((boost::format("Wait writeable event of endpoint <%1%> after recv EOF")
                            % endpoint_client).str());
    }

    {
        const auto asyn_connect_result = endpoint_client->Connect(kServerIp, kTestPort);
        BOOST_TEST_MESSAGE((boost::format("Endpoint <%1%> reconnect <1th> after recv EOF, result <%2%>") 
                            % endpoint_client 
                            % asyn_connect_result).str());

        if (static_cast<int32_t>(adk::verbs::ITcpEndpoint::ConnectResult::kSuccess) != asyn_connect_result)
        {
            BOOST_TEST_MESSAGE(endpoint_client->LastError());
        }
    }

    {
        const auto asyn_connect_result = endpoint_client->Connect(kServerIp, kTestPort);
        BOOST_TEST_MESSAGE((boost::format("Endpoint <%1%> reconnect <2th> after recv EOF, result <%2%>") 
                            % endpoint_client 
                            % asyn_connect_result).str());

        if (static_cast<int32_t>(adk::verbs::ITcpEndpoint::ConnectResult::kSuccess) != asyn_connect_result)
        {
            BOOST_TEST_MESSAGE(endpoint_client->LastError());
        }
    }

    {
        const auto asyn_connect_result = endpoint_client->Connect(kServerIp, kTestPort);
        BOOST_TEST_MESSAGE((boost::format("Endpoint <%1%> reconnect <3th> after recv EOF, result <%2%>") 
                            % endpoint_client 
                            % asyn_connect_result).str());

        if (static_cast<int32_t>(adk::verbs::ITcpEndpoint::ConnectResult::kSuccess) != asyn_connect_result)
        {
            BOOST_TEST_MESSAGE(endpoint_client->LastError());
        }
    }

    BOOST_TEST_MESSAGE((boost::format("Client close endpoint <%1%>") 
                        % endpoint_client).str());

	adk::verbs::ITcpEndpoint::Destroy(endpoint_client);
    endpoint_client = nullptr;

    endpoint_client = adk::verbs::ITcpEndpoint::Create(tcp_stack, true, true);
    BOOST_REQUIRE(endpoint_client);

    BOOST_REQUIRE_EQUAL(endpoint_client->Bind(0), 
                        static_cast<int32_t>(adk::verbs::ITcpEndpoint::BindResult::kSuccess));

    BOOST_REQUIRE_EQUAL(endpoint_client->Connect(kServerIp, kTestPort), 
                        static_cast<int32_t>(adk::verbs::ITcpEndpoint::ConnectResult::kInProgress));

    BOOST_TEST_MESSAGE((boost::format("create endpoint <%1%> <%2%:%3%>-<%4%:%5%> success")
                        % endpoint_client
                        % endpoint_client->local_ip()
                        % endpoint_client->local_port()
                        % endpoint_client->remote_ip()
                        % endpoint_client->remote_port()).str());

    connect_task = { 2, endpoint_client };
    BOOST_REQUIRE(tcp_epoller->EPollAdd(endpoint_client, &connect_task));
    BOOST_TEST_MESSAGE((boost::format("EPollAdd client endpoint <%1%> success") 
                        % endpoint_client).str());

    connect_result = false;
    while (nullptr == accept_endpoint || !connect_result)
    {
        struct epoll_event events[2];
        const auto epoll_nr = tcp_epoller->EPollWait(events, 2, 1000);
        for (int32_t index = 0; index < epoll_nr; ++index)
        {
            auto& event_node = events[index];
            EPollTask* epoll_task = (EPollTask*)event_node.data.ptr;
            if (1 == epoll_task->task_type)
            {
                BOOST_CHECK(nullptr == accept_endpoint);
                BOOST_REQUIRE_EQUAL(epoll_task->task_context, acceptor);
                BOOST_CHECK(adk::verbs::ITcpAcceptor::EPollResult(event_node.events));

                accept_endpoint = acceptor->Accept();
                BOOST_CHECK(accept_endpoint);
				BOOST_TEST_MESSAGE((boost::format("Server accept a new connection <%1%>") 
                                    % accept_endpoint).str());
            }
            else if (2 == epoll_task->task_type)
            {
                BOOST_CHECK(!connect_result);
                BOOST_REQUIRE_EQUAL(epoll_task->task_context, endpoint_client);
                connect_result = endpoint_client->EPollResult(event_node.events);
                BOOST_CHECK(connect_result);

				BOOST_TEST_MESSAGE((boost::format("Client endpoint <%1%> connect success") 
                                    % endpoint_client).str());
            }
        }
    }

    BOOST_REQUIRE_GT(endpoint_client->endpoint_id(), 0);
    BOOST_REQUIRE_GT(accept_endpoint->endpoint_id(), 0);

    BOOST_CHECK_EQUAL(accept_endpoint->remote_ip(), kRemoteIp);
    BOOST_CHECK_EQUAL(endpoint_client->remote_ip(), kServerIp);

    BOOST_CHECK_EQUAL(endpoint_client->remote_port(), kTestPort);
	BOOST_CHECK_EQUAL(accept_endpoint->local_port(), kTestPort);

    BOOST_TEST_MESSAGE((boost::format("Established, client endpoint <%1%:%2%>-<%3%:%4%>, accept endpoint <%5%:%6%>-<%7%:%8%>")
                        % endpoint_client->local_ip()
                        % endpoint_client->local_port()
                        % endpoint_client->remote_ip()
                        % endpoint_client->remote_port()
                        % accept_endpoint->remote_ip()
                        % accept_endpoint->remote_port()
                        % accept_endpoint->local_ip()
                        % accept_endpoint->local_port()).str());

    BOOST_TEST_MESSAGE((boost::format("Server close accpetor <%1%>") % acceptor).str());
    BOOST_REQUIRE(tcp_epoller->EPollDel(acceptor));
    BOOST_REQUIRE(!tcp_epoller->EPollDel(acceptor));
    adk::verbs::ITcpAcceptor::Destroy(acceptor);

    BOOST_REQUIRE(tcp_epoller->EPollDel(endpoint_client));
    BOOST_REQUIRE(!tcp_epoller->EPollDel(endpoint_client));

    EPollTask client_readable = { 3, endpoint_client };
    BOOST_REQUIRE(tcp_epoller->EPollAddR(endpoint_client, &client_readable));
    BOOST_REQUIRE(!tcp_epoller->EPollAddR(endpoint_client, &client_readable));

    EPollTask server_readable = { 4, accept_endpoint };
    BOOST_REQUIRE(tcp_epoller->EPollAddR(accept_endpoint, &server_readable));
    BOOST_REQUIRE(!tcp_epoller->EPollAddR(accept_endpoint, &server_readable));

    {
        struct epoll_event events[2];
        BOOST_REQUIRE_EQUAL(0, tcp_epoller->EPollWait(events, 2, 1000));
    }

    std::string client_to_server("Client to Server");
    std::string server_to_client("Server to Client");

    BOOST_REQUIRE_EQUAL(client_to_server.length() + 1, 
                        endpoint_client->Send(client_to_server.c_str(), 
                                              client_to_server.length() + 1));

    BOOST_REQUIRE_EQUAL(server_to_client.length() + 1, 
                        accept_endpoint->Send(server_to_client.c_str(), 
                                              server_to_client.length() + 1));

    {
        struct epoll_event events[2];
        BOOST_REQUIRE_EQUAL(2, tcp_epoller->EPollWait(events, 2, 1000));

        for (int32_t index = 0; index < 2; ++index)
        {
            auto& event_node = events[index];
            EPollTask* epoll_task = (EPollTask*)event_node.data.ptr;

            if (3 == epoll_task->task_type)
            {
                BOOST_REQUIRE_EQUAL(epoll_task->task_context, endpoint_client);
                BOOST_REQUIRE_EQUAL(server_to_client.length() + 1, 
                                    endpoint_client->Recv(buffer, kRecvBufferSize));
                BOOST_REQUIRE_EQUAL(server_to_client, std::string(buffer));
            }
            else if (4 == epoll_task->task_type)
            {
                BOOST_REQUIRE_EQUAL(epoll_task->task_context, accept_endpoint);
                BOOST_REQUIRE_EQUAL(client_to_server.length() + 1, 
                                    accept_endpoint->Recv(buffer, kRecvBufferSize));
                BOOST_REQUIRE_EQUAL(client_to_server, std::string(buffer));
            }
            else
            {
                BOOST_REQUIRE(false);
            }
        }
    }

    BOOST_TEST_MESSAGE((boost::format("Client close endpoint <%1%>") % endpoint_client).str());
    BOOST_REQUIRE(tcp_epoller->EPollDel(endpoint_client));
    BOOST_REQUIRE(!tcp_epoller->EPollDel(endpoint_client));
    adk::verbs::ITcpEndpoint::Destroy(endpoint_client);

    do
    {
        recv_result = accept_endpoint->Recv(buffer, kRecvBufferSize);
    } while (-1 == recv_result);

    BOOST_CHECK_EQUAL(recv_result, 0);
    BOOST_TEST_MESSAGE((boost::format("Server endpoint <%1%> recv EOF")
                        % accept_endpoint).str());

    BOOST_TEST_MESSAGE((boost::format("Server close endpoint <%1%>") % accept_endpoint).str());

    BOOST_REQUIRE(tcp_epoller->EPollDel(accept_endpoint));
    BOOST_REQUIRE(!tcp_epoller->EPollDel(accept_endpoint));
	adk::verbs::ITcpEndpoint::Destroy(accept_endpoint);

    adk::verbs::ITcpEPoller::Destroy(tcp_epoller);
    adk::verbs::ITcpStack::Destroy(tcp_stack);
}