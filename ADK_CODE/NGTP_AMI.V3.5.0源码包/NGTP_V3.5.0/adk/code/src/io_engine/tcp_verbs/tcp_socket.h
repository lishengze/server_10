#ifndef ADK_IMPL_IO_ENGINE_TCP_SOCKET_H_
#define ADK_IMPL_IO_ENGINE_TCP_SOCKET_H_

#include "tcp_interface.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <adk/arch/generic.h>

namespace adk_impl
{

namespace verbs
{

class TcpStackSk final : public ITcpStack
{
public:
    static constexpr DriveMode kDriveMode = DriveMode::kPoller;
    inline DriveMode drive_mode() const override
    {
        return kDriveMode;
    }

private:
    TcpStackSk(const std::string& message_ip);
    virtual ~TcpStackSk() = default;

    friend class ITcpStack;
};

class TcpEPollerSk final : public ITcpEPoller
{
public:
    bool EPollAdd(ITcpEndpoint* endpoint, uint32_t events, void* context) override;

    bool EPollAdd(ITcpEndpoint* endpoint, void* context) override
    {
        return EPollAddW(endpoint, context);
    }

    bool EPollDel(ITcpEndpoint* endpoint) override;

    bool EPollAdd(ITcpAcceptor* acceptor, void* context) override;

    bool EPollDel(ITcpAcceptor* acceptor) override;

    int32_t EPollWait(struct epoll_event* events, int32_t maxevents, int32_t timeout_ms) override
    {
        assert(epoll_fd_ > 0);
        return epoll_wait(epoll_fd_, events, maxevents, timeout_ms);
    }

    bool EPollAdd(int32_t fd, uint32_t events, void* context) override
    {
        assert(epoll_fd_ > 0);

        struct epoll_event add_event;
        add_event.events = events;
        add_event.data.ptr = context;
        return (0 == epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &add_event));
    }

    bool EPollDel(int32_t fd) override
    {
        assert(epoll_fd_ > 0);
        return (0 == epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr));
    }

    void Stop() override;

private:
    TcpEPollerSk();
    virtual ~TcpEPollerSk();

    bool Open(ITcpStack* tcp_stack) override;

    void Close() override;

    int32_t epoll_fd_;

    friend class ITcpEPoller;
};

class TcpAcceptorSk final : public ITcpAcceptor
{
public:
    ITcpEndpoint* Accept() override;

    int32_t sock_fd() const
    {
        return sock_fd_;
    }

private:
    TcpAcceptorSk();
    virtual ~TcpAcceptorSk();

    bool Open(ITcpStack* tcp_stack, 
              const std::string& listen_ip, 
              uint16_t listen_port, 
              bool reuse_addr,
              bool reuse_port) override;

    void Close() override;

    int32_t sock_fd_;

    friend class ITcpAcceptor;
};

class TcpEndpointSk final : public ITcpEndpoint
{
public:
    using StackType =   TcpStackSk;
    using EPollerType = TcpEPollerSk;

    static constexpr bool kZcRecvSupport = false;

    inline static constexpr ITcpStack::DriveMode drive_mode()
    {
        return StackType::kDriveMode;
    }

    bool SetOption(OptionType option_type, int32_t option_value) override;

    int32_t Bind(uint16_t local_port) override;

    int32_t Connect(const std::string& remote_ip, uint16_t remote_port) override;

    bool EPollResult(uint32_t events) override;

    ADK_HOT inline ssize_t Send(const void* buffer, size_t len) override
    {
        return send(sock_fd_, buffer, len, 0);
    }

    ADK_HOT inline ssize_t Send(const struct iovec *iov, size_t iovcnt) override
    {
        return writev(sock_fd_, iov, iovcnt);
    }

    ADK_HOT inline ssize_t Recv(char* buffer, size_t len) override
    {
        return recv(sock_fd_, buffer, len, 0);
    }

    ADK_HOT inline ssize_t Recv(const struct iovec *iov, size_t iovcnt) override
    {
        return readv(sock_fd_, iov, iovcnt);
    }

    template<typename Callback>
    ADK_HOT inline ssize_t ZcRecv(const Callback& callback)
    {
        errno = ESOCKTNOSUPPORT;
        return -1;
    }

    std::string LastError() const override;

    void Close() override;

    int32_t sock_fd() const
    {
        return sock_fd_;
    }

protected:
    TcpEndpointSk();
    virtual ~TcpEndpointSk();

    bool Open(ITcpStack* tcp_stack, bool reuse_addr, bool reuse_port) override;

private:
    int32_t sock_fd_;

    friend class ITcpEndpoint;
    friend class TcpAcceptorSk;
};

inline bool TcpEPollerSk::EPollAdd(ITcpEndpoint* endpoint, uint32_t events, void* context)
{
    assert(endpoint);
    assert(epoll_fd_ > 0);

    struct epoll_event add_event;
    add_event.events = events;
    add_event.data.ptr = context;
    return (0 == epoll_ctl(epoll_fd_,
                           EPOLL_CTL_ADD,
                           ((TcpEndpointSk*)endpoint)->sock_fd(),
                           &add_event));
}

inline bool TcpEPollerSk::EPollDel(ITcpEndpoint* endpoint)
{
    assert(endpoint);
    assert(epoll_fd_ > 0);

    return (0 == epoll_ctl(epoll_fd_,
                           EPOLL_CTL_DEL,
                           ((TcpEndpointSk*)endpoint)->sock_fd(),
                           nullptr));
}

inline bool TcpEPollerSk::EPollAdd(ITcpAcceptor* acceptor, void* context)
{
    assert(acceptor);
    assert(epoll_fd_ > 0);

    struct epoll_event add_event;
    add_event.events = EPOLLIN;
    add_event.data.ptr = context;
    return (0 == epoll_ctl(epoll_fd_,
                           EPOLL_CTL_ADD,
                           ((TcpAcceptorSk*)acceptor)->sock_fd(),
                           &add_event));
}

inline bool TcpEPollerSk::EPollDel(ITcpAcceptor* acceptor)
{
    assert(acceptor);
    assert(epoll_fd_ > 0);

    return (0 == epoll_ctl(epoll_fd_,
                           EPOLL_CTL_DEL,
                           ((TcpAcceptorSk*)acceptor)->sock_fd(),
                           nullptr));
}

}

}

#endif