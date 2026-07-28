#define BOOST_TEST_MODULE zf_verbs_server

#include "zf_verbs.h"

#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(test_TcpStackZf_Server)
{
    auto* const env_server = std::getenv(kServerEnv.c_str());
    if (nullptr == env_server)
    {
        BOOST_TEST_MESSAGE("zf_verbs_server_test ignored");
        return;
    }

    const std::string kServerIp = env_server;
    BOOST_TEST_MESSAGE((boost::format("server ip <%1%>") % kServerIp).str());

    auto* const tcp_stack = adk::verbs::ITcpStack::Create(kServerIp);
    BOOST_REQUIRE(tcp_stack);

    BOOST_REQUIRE(!tcp_stack->io_parallel_support());
    BOOST_REQUIRE_EQUAL(tcp_stack->message_ip(), kServerIp);
    BOOST_REQUIRE_EQUAL(tcp_stack->ReactorPerform(), 0);
    BOOST_REQUIRE(tcp_stack->stack_type() == adk::verbs::ITcpStack::StackType::kStackZf);

    auto* tcp_epoller = adk::verbs::ITcpEPoller::Create(tcp_stack, 
                                                        adk::verbs::ITcpEPoller::PollerType::kControl);
    BOOST_REQUIRE(tcp_epoller);

    BOOST_TEST_MESSAGE((boost::format("Server listen at address <%1%:%2%>") 
                        % kServerIp 
                        % kServerPort).str());

    auto* acceptor = adk::verbs::ITcpAcceptor::Create(tcp_stack, std::string(), kServerPort, true, true);
    BOOST_REQUIRE(acceptor);

    BOOST_REQUIRE(tcp_epoller->EPollAdd(acceptor, acceptor));
    BOOST_REQUIRE(!tcp_epoller->EPollAdd(acceptor, acceptor));

    adk::verbs::ITcpEndpoint* accept_endpoint = nullptr;
    while (true)
    {
        if (tcp_stack->ReactorPerform() > 0)
        {
            struct epoll_event event;
            BOOST_REQUIRE_EQUAL(tcp_epoller->EPollWait(&event, 1, 1000), 1);
            BOOST_REQUIRE_EQUAL(event.data.ptr, acceptor);
            BOOST_REQUIRE(adk::verbs::ITcpAcceptor::EPollResult(event.events));

            accept_endpoint = acceptor->Accept();
            if (nullptr != accept_endpoint)
            {
                BOOST_REQUIRE(tcp_epoller->EPollDel(acceptor));
                BOOST_REQUIRE(!tcp_epoller->EPollDel(acceptor));
                break;
            }
        }
    }

    BOOST_TEST_MESSAGE((boost::format("Established, accept endpoint <%1%:%2%>-<%3%:%4%>")
                        % accept_endpoint->remote_ip()
                        % accept_endpoint->remote_port()
                        % accept_endpoint->local_ip()
                        % accept_endpoint->local_port()).str());

    while (true)
    {
        if (tcp_stack->ReactorPerform() > 0)
        {
            const auto recv_result = accept_endpoint->Recv(kBuffer, kBufferSize);
            if (recv_result > 0)
            {
                BOOST_REQUIRE_EQUAL(accept_endpoint->Send(kBuffer, recv_result), recv_result);
            }

            if (-1 == recv_result)
            {
                continue;
            }

            if (0 == recv_result)
            {
                BOOST_TEST_MESSAGE("Server endpoint recv EOF");
                break;
            }
        }
    }

    adk::verbs::ITcpEndpoint::Destroy(accept_endpoint);
    adk::verbs::ITcpEPoller::Destroy(tcp_epoller);
    adk::verbs::ITcpStack::Destroy(tcp_stack);         
}