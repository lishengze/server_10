#ifndef ADK_TEST_VERBS_PERF_H_
#define ADK_TEST_VERBS_PERF_H_

#include <time.h>
#include <string.h>

#include <string>
#include <thread>
#include <iomanip>
#include <iostream>
#include <typeinfo>

#include <boost/format.hpp>
#include <boost/date_time.hpp>
#include <boost/program_options.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <adk/token_buckets.h>
#include <tcp_verbs/tcp_socket.h>

#if defined(__x86_64__)
#include <tcp_verbs/tcp_direct_zf.h>
#elif defined(__aarch64__)
#include <tcp_verbs/tcp_direct_zf_arm.h>
#endif

#include <tcp_verbs/tcp_interface.h>

struct MessageFormat
{
    uint32_t message_size;
    struct timespec timepoint1;
    struct timespec timepoint2;
    struct timespec timepoint3;
};

static inline bool CheckTxResult(int32_t result, int32_t expect, adk::verbs::ITcpEndpoint* endpoint)
{
    if (expect != result)
    {
        if (result > 0)
        {
            std::cout << (boost::format("Endpoint <%1%:%2%>-<%3%:%4%> send message <%5%/%6%> blocked")
                                        % endpoint->local_ip()
                                        % endpoint->local_port()
                                        % endpoint->remote_ip()
                                        % endpoint->remote_port()
                                        % result
                                        % expect).str() << std::endl;
        }
        else
        {
            std::cout << (boost::format("Endpoint <%1%:%2%>-<%3%:%4%> error <%5%>")
                                        % endpoint->local_ip()
                                        % endpoint->local_port()
                                        % endpoint->remote_ip()
                                        % endpoint->remote_port()
                                        % strerror(errno)).str() << std::endl;
        }
        return false;
    }

    return true;
}

static inline bool CheckRxResult(int32_t result, adk::verbs::ITcpEndpoint* endpoint)
{
    if (result == 0)
    {
        std::cout << (boost::format("Endpoint <%1%:%2%>-<%3%:%4%> recv EOF")
                                    % endpoint->local_ip()
                                    % endpoint->local_port()
                                    % endpoint->remote_ip()
                                    % endpoint->remote_port()).str() << std::endl;
        return false;
    }

    if ((result < 0) && (EAGAIN != errno))
    {
        std::cout << (boost::format("Endpoint <%1%:%2%>-<%3%:%4%> error <%5%>")
                                    % endpoint->local_ip()
                                    % endpoint->local_port()
                                    % endpoint->remote_ip()
                                    % endpoint->remote_port()
                                    % strerror(errno)).str() << std::endl;
        return false;
    }

    return true;
}

#endif
