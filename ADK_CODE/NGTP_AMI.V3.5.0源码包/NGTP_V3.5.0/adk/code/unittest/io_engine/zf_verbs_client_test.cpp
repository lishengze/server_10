#define BOOST_TEST_MODULE zf_verbs_client

#include "zf_verbs.h"

#include <cstdlib>
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(test_TcpStackZf_Client)
{
    auto* const env_client = std::getenv(kClientEnv.c_str());
    if (nullptr == env_client)
    {
        BOOST_TEST_MESSAGE("zf_verbs_client_test ignored");
        return;
    }

    auto* const env_server = std::getenv(kServerEnv.c_str());
    if (nullptr == env_server)
    {
        BOOST_TEST_MESSAGE("zf_verbs_client_test ignored");
        return;
    }

    const std::string kClientIp = env_client;
    const std::string kServerIp = env_server;
    BOOST_TEST_MESSAGE((boost::format("Client ip <%1%>, Server ip <%2%>") 
                                      % kClientIp
                                      % kServerIp).str());

    auto* const tcp_stack = adk::verbs::ITcpStack::Create(kClientIp);
    BOOST_REQUIRE(tcp_stack);

    BOOST_REQUIRE(!tcp_stack->io_parallel_support());
    BOOST_REQUIRE_EQUAL(tcp_stack->message_ip(), kClientIp);
    BOOST_REQUIRE_EQUAL(tcp_stack->ReactorPerform(), 0);
    BOOST_REQUIRE(tcp_stack->stack_type() == adk::verbs::ITcpStack::StackType::kStackZf);

    auto* tcp_epoller = adk::verbs::ITcpEPoller::Create(tcp_stack, 
                                                        adk::verbs::ITcpEPoller::PollerType::kControl);
    BOOST_REQUIRE(tcp_epoller);

    auto* endpoint_client = adk::verbs::ITcpEndpoint::Create(tcp_stack, true, true);
    BOOST_REQUIRE(endpoint_client);

    BOOST_REQUIRE_EQUAL(static_cast<int32_t>(adk::verbs::ITcpEndpoint::BindResult::kSuccess), 
                        endpoint_client->Bind(0));

    BOOST_TEST_MESSAGE((boost::format("Client connect to address <%1%:%2%>") 
                        % kServerIp 
                        % kServerPort).str());

    bool connect_result = false;
    while (!connect_result)
    {
        BOOST_REQUIRE_EQUAL(static_cast<int32_t>(adk::verbs::ITcpEndpoint::ConnectResult::kInProgress), 
                            endpoint_client->Connect(kServerIp, kServerPort));

        BOOST_REQUIRE(tcp_epoller->EPollAdd(endpoint_client, endpoint_client));
        BOOST_REQUIRE(!tcp_epoller->EPollAdd(endpoint_client, endpoint_client));

        if (tcp_stack->ReactorPerform() > 0)
        {
            struct epoll_event event;
            BOOST_REQUIRE_EQUAL(tcp_epoller->EPollWait(&event, 1, 1000), 1);
            BOOST_REQUIRE_EQUAL(event.data.ptr, endpoint_client);
            connect_result = endpoint_client->EPollResult(event.events);
        }

        BOOST_REQUIRE(tcp_epoller->EPollDel(endpoint_client));
        BOOST_REQUIRE(!tcp_epoller->EPollDel(endpoint_client));
    }

    BOOST_TEST_MESSAGE((boost::format("Established, client endpoint <%1%:%2%>-<%3%:%4%>")
                        % endpoint_client->remote_ip()
                        % endpoint_client->remote_port()
                        % endpoint_client->local_ip()
                        % endpoint_client->local_port()).str());

    struct timespec current_tp;
    uint64_t max_time_diff = 0;
    uint64_t min_time_diff = 0xffffffff;
    uint64_t total_time_diff = 0;
    constexpr int32_t kTestLoop1 = 10;
    for (int32_t index = 0; index < kTestLoop1; ++index)
    {
        clock_gettime(CLOCK_MONOTONIC, (struct timespec*)kBuffer);
        BOOST_REQUIRE_EQUAL(endpoint_client->Send(kBuffer, kTestDataSize), kTestDataSize);

    Reactor1:
        while (0 == tcp_stack->ReactorPerform());
        const auto recv_result = endpoint_client->Recv(kBuffer, kBufferSize);
        if (-1 == recv_result)
        {
            goto Reactor1;
        }

        BOOST_REQUIRE_GE(recv_result, kTestDataSize);
        BOOST_REQUIRE_EQUAL(recv_result % kTestDataSize, 0);

        int32_t cur = 0;
        do
        {
            clock_gettime(CLOCK_MONOTONIC, &current_tp);

            char* buffer = kBuffer + cur;
            const auto time_diff = (current_tp.tv_sec - ((struct timespec*)buffer)->tv_sec) * 1000000000
                                    + current_tp.tv_nsec - ((struct timespec*)buffer)->tv_nsec;
            max_time_diff = std::max<uint64_t>(time_diff, max_time_diff);
            min_time_diff = std::min<uint64_t>(time_diff, min_time_diff);
            total_time_diff += time_diff;
            cur += kTestDataSize;
        } while (cur < recv_result);

        usleep(1000);
    }

    BOOST_TEST_MESSAGE((boost::format("avg:%1%ns \t min:%2%ns \t max:%3%ns") 
                        % (total_time_diff / kTestLoop1)
                        % min_time_diff 
                        % max_time_diff).str());

    max_time_diff = 0;
    min_time_diff = 0xffffffff;
    total_time_diff = 0;
    constexpr int32_t kTestLoop2 = 100;
    for (int32_t index = 0; index < kTestLoop2; ++index)
    {
        clock_gettime(CLOCK_MONOTONIC, (struct timespec*)kBuffer);
        BOOST_REQUIRE_EQUAL(endpoint_client->Send(kBuffer, kTestDataSize), kTestDataSize);
        
    Reactor2:
        while (0 == tcp_stack->ReactorPerform());
        const auto recv_result = endpoint_client->Recv(kBuffer, kBufferSize);
        if (-1 == recv_result)
        {
            goto Reactor2;
        }

        BOOST_REQUIRE_GE(recv_result, kTestDataSize);
        BOOST_REQUIRE_EQUAL(recv_result % kTestDataSize, 0);

        int32_t cur = 0;
        do
        {
            clock_gettime(CLOCK_MONOTONIC, &current_tp);

            char* buffer = kBuffer + cur;
            const auto time_diff = (current_tp.tv_sec - ((struct timespec*)buffer)->tv_sec) * 1000000000
                                    + current_tp.tv_nsec - ((struct timespec*)buffer)->tv_nsec;
            max_time_diff = std::max<uint64_t>(time_diff, max_time_diff);
            min_time_diff = std::min<uint64_t>(time_diff, min_time_diff);
            total_time_diff += time_diff;
            cur += kTestDataSize;
        } while (cur < recv_result);

        usleep(1000);
    }

    BOOST_TEST_MESSAGE((boost::format("avg:%1%ns \t min:%2%ns \t max:%3%ns") 
                        % (total_time_diff / kTestLoop2)
                        % min_time_diff 
                        % max_time_diff).str());

    max_time_diff = 0;
    min_time_diff = 0xffffffff;
    total_time_diff = 0;
    constexpr int32_t kTestLoop3 = 1000000;
    for (int32_t index = 0; index < kTestLoop3; ++index)
    {
        clock_gettime(CLOCK_MONOTONIC, (struct timespec*)kBuffer);
        BOOST_REQUIRE_EQUAL(endpoint_client->Send(kBuffer, kTestDataSize), kTestDataSize);
        
    Reactor3:
        while (0 == tcp_stack->ReactorPerform());
        const auto recv_result = endpoint_client->Recv(kBuffer, kBufferSize);
        if (-1 == recv_result)
        {
            goto Reactor3;
        }

        BOOST_REQUIRE_GE(recv_result, kTestDataSize);
        BOOST_REQUIRE_EQUAL(recv_result % kTestDataSize, 0);

        int32_t cur = 0;
        do
        {
            clock_gettime(CLOCK_MONOTONIC, &current_tp);

            char* buffer = kBuffer + cur;
            const auto time_diff = (current_tp.tv_sec - ((struct timespec*)buffer)->tv_sec) * 1000000000
                                    + current_tp.tv_nsec - ((struct timespec*)buffer)->tv_nsec;
            max_time_diff = std::max<uint64_t>(time_diff, max_time_diff);
            min_time_diff = std::min<uint64_t>(time_diff, min_time_diff);
            total_time_diff += time_diff;
            cur += kTestDataSize;
        } while (cur < recv_result);
    }

    BOOST_TEST_MESSAGE((boost::format("avg:%1%ns \t min:%2%ns \t max:%3%ns") 
                        % (total_time_diff / kTestLoop3)
                        % min_time_diff 
                        % max_time_diff).str());

    adk::verbs::ITcpEPoller::Destroy(tcp_epoller);
    adk::verbs::ITcpEndpoint::Destroy(endpoint_client);
    adk::verbs::ITcpStack::Destroy(tcp_stack);
}