#include "tcp_socket.h"

#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

namespace adk_impl
{

namespace verbs
{

TcpStackSk::TcpStackSk(const std::string& message_ip)
{
    stack_type_ = StackType::kStackSk;
    message_ip_ = message_ip;
}

TcpEPollerSk::TcpEPollerSk()
{
    epoll_fd_ = default_value::kInvalidFd;
}

TcpEPollerSk::~TcpEPollerSk()
{
    Close();
}

void TcpEPollerSk::Stop()
{
    if (default_value::kInvalidFd != epoll_fd_)
    {
        close(epoll_fd_);
        epoll_fd_ = default_value::kInvalidFd;
    }
}

bool TcpEPollerSk::Open(ITcpStack* tcp_stack)
{
    Close();

    epoll_fd_ = epoll_create1(0);
    if (ADK_UNLIKELY(default_value::kInvalidFd == epoll_fd_))
    {
        return false;
    }

    return true;
}

void TcpEPollerSk::Close()
{
    if (default_value::kInvalidFd != epoll_fd_)
    {
        close(epoll_fd_);
        epoll_fd_ = default_value::kInvalidFd;
    }
}

TcpAcceptorSk::TcpAcceptorSk()
{
    sock_fd_ = default_value::kInvalidFd;
}

TcpAcceptorSk::~TcpAcceptorSk()
{
    Close();
}

bool TcpAcceptorSk::Open(ITcpStack* tcp_stack, 
                         const std::string& listen_ip, 
                         uint16_t listen_port, 
                         bool reuse_addr, 
                         bool reuse_port)
{
    Close();

    sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (default_value::kInvalidFd == sock_fd_)
    {
        return false;
    }

    auto opts = fcntl(sock_fd_, F_GETFL);
    fcntl(sock_fd_, F_SETFL, opts | O_NONBLOCK);

    if (reuse_addr)
    {
        const int32_t optval_ra = 1;
        setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEADDR, &optval_ra, sizeof(int32_t));
    }

    if (reuse_port)
    {
        const int32_t optval_ra = 1;
        setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEPORT, &optval_ra, sizeof(int32_t));
    }

    if (listen_ip.empty())
    {
        assert(tcp_stack);
        listen_ip_ = tcp_stack->message_ip();
    }
    else
    {
        listen_ip_ = listen_ip;
    }

    struct sockaddr_in listen_addr;
    bzero(&listen_addr, sizeof(struct sockaddr_in));

    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = listen_ip_.empty() 
                                ? INADDR_ANY
                                : inet_addr(listen_ip_.c_str());
    listen_addr.sin_port = htons(listen_port);

    if (ADK_UNLIKELY(0 != bind(sock_fd_,
                               (struct sockaddr*)(&listen_addr),
                               (socklen_t)sizeof(struct sockaddr_in))))
    {
        return false;
    }

    {
        struct sockaddr_in local_sock;
        socklen_t addrlen = sizeof(struct sockaddr_in);
        getsockname(sock_fd_, (sockaddr*)&local_sock, &addrlen);
        listen_ip_ = inet_ntoa(local_sock.sin_addr);
        listen_port_ = ntohs(local_sock.sin_port);    
    }

    if (ADK_UNLIKELY(0 != listen(sock_fd_, default_value::kPendingQueueSize)))
    {
        return false;
    }

    return true;
}

void TcpAcceptorSk::Close()
{
    if (default_value::kInvalidFd != sock_fd_)
    {
        close(sock_fd_);
        sock_fd_ = default_value::kInvalidFd;
    }
}

ITcpEndpoint* TcpAcceptorSk::Accept()
{
    struct sockaddr_in remote_addr;
    bzero(&remote_addr, sizeof(struct sockaddr_in));

    socklen_t addrlen = sizeof(struct sockaddr_in);
    int sock_fd = accept(sock_fd_, (struct sockaddr*)(&remote_addr), &addrlen);
    if (ADK_UNLIKELY(sock_fd < 0))
    {
        return nullptr;
    }

    auto opts = fcntl(sock_fd, F_GETFL);
    fcntl(sock_fd, F_SETFL, opts | O_NONBLOCK);

    TcpEndpointSk* const tcp_endpoint = new TcpEndpointSk;
    tcp_endpoint->sock_fd_ = sock_fd;
    tcp_endpoint->remote_ip_ = inet_ntoa(remote_addr.sin_addr);
    tcp_endpoint->remote_port_ = ntohs(remote_addr.sin_port);

    struct sockaddr_in local_sock;
    addrlen = sizeof(struct sockaddr_in);
    getsockname(sock_fd_, (sockaddr*)&local_sock, &addrlen);
    tcp_endpoint->local_ip_ = inet_ntoa(local_sock.sin_addr);
    tcp_endpoint->local_port_ = ntohs(local_sock.sin_port);

    tcp_endpoint->endpoint_id_ = sock_fd;
    return tcp_endpoint;
}

TcpEndpointSk::TcpEndpointSk()
{
    sock_fd_ = default_value::kInvalidFd;
}

TcpEndpointSk::~TcpEndpointSk()
{
    Close();
}

bool TcpEndpointSk::Open(ITcpStack* tcp_stack, bool reuse_addr, bool reuse_port)
{
    Close();

    sock_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (default_value::kInvalidFd == sock_fd_)
    {
        return false;
    }

    auto opts = fcntl(sock_fd_, F_GETFL);
    fcntl(sock_fd_, F_SETFL, opts | O_NONBLOCK);

    if (reuse_addr)
    {
        const int32_t optval_ra = 1;
        setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEADDR, &optval_ra, sizeof(int32_t));
    }

    if (reuse_port)
    {
        const int32_t optval_ra = 1;
        setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEPORT, &optval_ra, sizeof(int32_t));
    }

    endpoint_id_ = sock_fd_;

    return true;
}

void TcpEndpointSk::Close()
{
    if (default_value::kInvalidFd != sock_fd_)
    {
        close(sock_fd_);
        sock_fd_ = default_value::kInvalidFd;
    }
}

bool TcpEndpointSk::SetOption(OptionType option, int32_t value)
{
    switch (option)
    {
    case OptionType::kTcpNoDelay:
        return (0 == setsockopt(sock_fd_, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(int32_t)));
    case OptionType::kSendBuffer:
        return (0 == setsockopt(sock_fd_, SOL_SOCKET, SO_SNDBUF, &value, sizeof(int32_t)));
    case OptionType::kRecvBuffer:
        return (0 == setsockopt(sock_fd_, SOL_SOCKET, SO_RCVBUF, &value, sizeof(int32_t)));
    case OptionType::kBlockMode:
    {
        const auto block_mode = value ? (fcntl(sock_fd_, F_GETFL) & ~O_NONBLOCK) 
                                      : (fcntl(sock_fd_, F_GETFL) | O_NONBLOCK);
        return (-1 != fcntl(sock_fd_, F_SETFL, block_mode));
    }
    default:
        return false;
    }

    return true;
}

int32_t TcpEndpointSk::Bind(uint16_t local_port)
{
    struct sockaddr_in local_addr;
    bzero(&local_addr, sizeof(struct sockaddr_in));

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = local_ip_.empty() 
                               ? INADDR_ANY 
                               : inet_addr(local_ip_.c_str());
    local_addr.sin_port = htons(local_port);

    if (ADK_UNLIKELY(0 != bind(sock_fd_, 
                               (struct sockaddr*)(&local_addr), 
                               (socklen_t)sizeof(struct sockaddr_in))))
    {
        if (EADDRINUSE == errno)
        {
            return static_cast<int32_t>(BindResult::kAddrInUse);
        }

        return static_cast<int32_t>(BindResult::kFailure);
    }

    ///> solarflare onload not support
    const int32_t syn_cnt = 1;
    setsockopt(sock_fd_, IPPROTO_TCP, TCP_SYNCNT, &syn_cnt, sizeof(int32_t));

    local_port_ = local_port;

    return static_cast<int32_t>(BindResult::kSuccess);
}

int32_t TcpEndpointSk::Connect(const std::string& remote_ip, uint16_t remote_port)
{
    remote_ip_ = remote_ip;
    remote_port_ = remote_port;

    struct sockaddr_in dest_addr;
    bzero(&dest_addr, sizeof(struct sockaddr_in));

    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = inet_addr(remote_ip.c_str());
    dest_addr.sin_port = htons(remote_port);

    const int result = connect(sock_fd_, 
                               (struct sockaddr*)(&dest_addr), 
                               sizeof(struct sockaddr_in));

    if (local_ip_.empty() || (0 == local_port_))
    {
        struct sockaddr_in local_sock;
        socklen_t addrlen = sizeof(struct sockaddr_in);
        getsockname(sock_fd_, (sockaddr*)&local_sock, &addrlen);
        local_ip_ = inet_ntoa(local_sock.sin_addr);
        local_port_ = ntohs(local_sock.sin_port);
    }

    if (result < 0)
    {
        if (EINPROGRESS == errno)
        {
            return static_cast<int32_t>(ConnectResult::kInProgress);
        }

        return static_cast<int32_t>(ConnectResult::kFailure);
    }

    return static_cast<int32_t>(ConnectResult::kSuccess);
}

bool TcpEndpointSk::EPollResult(uint32_t events)
{
    if (events & EPOLLOUT)
    {
#if 1
        if (ADK_UNLIKELY(events & EPOLLERR))
        {
            return false;
        }
#else
        int32_t optval = -1;
        socklen_t optlen = sizeof(optval);
        getsockopt(sock_fd_, SOL_SOCKET, SO_ERROR, &optval, &optlen);
        if (ADK_UNLIKELY(0 != optval))
        {
            return false;
        }
#endif
        /**
         * get local port after connect successfully
		 */
        struct sockaddr_in local_sock;
        socklen_t addrlen = sizeof(struct sockaddr_in);
        getsockname(sock_fd_, (sockaddr*)&local_sock, &addrlen);
        local_port_ = ntohs(local_sock.sin_port);
        return true;
    }

    return false;
}

std::string TcpEndpointSk::LastError() const
{
    if (0 != errno)
    {
        return strerror(errno);
    }

    int32_t optval = -1;
    socklen_t optlen = sizeof(optval);
    getsockopt(sock_fd_, SOL_SOCKET, SO_ERROR, &optval, &optlen);
    if (0 != optval)
    {
        return strerror(optval);
    }

    return std::string();
}

}

}