#include "socket_raw.h"

bool SocketRaw::SetBlocking(int32_t sock_fd)
{
    auto opts = fcntl(sock_fd, F_GETFL);
    if (opts < 0)
    {
        return false;
    }

    opts &= (~O_NONBLOCK);

    if (0 > fcntl(sock_fd, F_SETFL, opts))
    {
        return false;
    }

    return true;
}

bool SocketRaw::SetUnblocking(int32_t sock_fd)
{
    auto opts = fcntl(sock_fd, F_GETFL);
    if (opts < 0)
    {
        return false;
    }

    opts |= O_NONBLOCK;

    if (0 > fcntl(sock_fd, F_SETFL, opts))
    {
        return false;
    }

    return true;
}

SocketRaw::SocketRaw()
{
    socket_fd_ = kSocketError;
    epoll_fd_ = kEpollError;
}

bool SocketRaw::Open(uint16_t port, const std::string& local_addr, bool reuse_addr)
{
    Close();

    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (kSocketError == socket_fd_)
    {
        return false;
    }

    // set socket nonblock
    SetUnblocking();

    int32_t reuse = (int32_t)reuse_addr;
    setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(int32_t));

    bind_port_ = port;

    bzero(&sock_addr_, sizeof(struct sockaddr_in));
    sock_addr_.sin_family = AF_INET;
    sock_addr_.sin_addr.s_addr = local_addr.empty() ? htonl(INADDR_ANY) : inet_addr(local_addr.c_str());
    sock_addr_.sin_port = htons(bind_port_);

    if (0 > bind(socket_fd_, (struct sockaddr*)&sock_addr_, sizeof(struct sockaddr_in)))
    {
        return false;
    }

    epoll_fd_ = epoll_create1(0);
    if (ADK_UNLIKELY(kEpollError == epoll_fd_))
    {
        return false;
    }

    struct epoll_event epoll_ev;
    epoll_ev.events = EPOLLIN;
    epoll_ev.data.ptr = nullptr;
    if (ADK_UNLIKELY(kEpollError == epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, socket_fd_, &epoll_ev)))
    {
        return false;
    }

    return true;
}

void SocketRaw::Close()
{
    if (kSocketError != socket_fd_)
    {
        close(socket_fd_);
        socket_fd_ = kSocketError;
        bind_port_ = 0;
    }

    if (kEpollError != epoll_fd_)
    {
        close(epoll_fd_);
        epoll_fd_ = kEpollError;
    }
}
