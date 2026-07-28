#ifndef ADK_IMPL_IO_ENGINE_TCP_INTERFACE_H_
#define ADK_IMPL_IO_ENGINE_TCP_INTERFACE_H_

#include <string>

#include <sys/uio.h>
#include <sys/epoll.h>

// #define _IO_ENGINE_TRACE_
#ifdef _IO_ENGINE_TRACE_
#ifndef TRACE_PRINT
#include <iostream>
#define TRACE_PRINT(info) \
    std::cout << info << std::endl
#endif
#else
#ifndef TRACE_PRINT
#define TRACE_PRINT(info)
#endif
#endif

namespace adk_impl
{

namespace verbs
{

namespace default_value
{

constexpr int32_t kInvalidFd = -1;
constexpr int32_t kPendingQueueSize = 2048;

}

class ITcpStack
{
public:
    enum class StackType : int32_t
    {
        kStackSk = 0,
        kStackZf,
        kStackMax
    };

    enum class DriveMode : int32_t
    {
        kPoller = 0,
        kReactor
    };

    static ITcpStack* Create(const std::string& message_ip);

    static void Destroy(ITcpStack* tcp_stack);

    virtual bool io_parallel_support() const
    {
        return true;
    }

    virtual int32_t ReactorPerform()
    {
        return 1;
    }

    virtual void ReactorTerminated()
    {
    }

    virtual DriveMode drive_mode() const = 0;

    inline StackType stack_type() const
    {
        return stack_type_;
    }

    inline const std::string& message_ip() const
    {
        return message_ip_;
    }

protected:
    ITcpStack() = default;
    virtual ~ITcpStack() = default;

    virtual bool Open(const std::string& nic_name)
    {
        return true;
    }

    virtual void Close()
    {
    }

    StackType   stack_type_;
    std::string message_ip_;
};

class ITcpEndpoint;
class ITcpAcceptor;

class ITcpEPoller
{
public:
    enum class PollerType
    {
        kControl,
        kGeneral,
        kSpecial,
        kTypeMax
    };

    static ITcpEPoller* Create(ITcpStack* tcp_stack, 
                               PollerType poller_type = PollerType::kGeneral);

    static void Destroy(ITcpEPoller* tcp_epoller);

    /**
     * @brief    一般化事件注册
     */
    virtual bool EPollAdd(ITcpEndpoint* endpoint, uint32_t events, void* context) = 0;

    /**
     * @brief    客户端异步连接事件注册
     */
    virtual bool EPollAdd(ITcpEndpoint* endpoint, void* context) = 0;

    virtual bool EPollDel(ITcpEndpoint* endpoint) = 0;

    /**
     * @brief    服务器端监听连接事件注册
     */
    virtual bool EPollAdd(ITcpAcceptor* acceptor, void* context) = 0;

    virtual bool EPollDel(ITcpAcceptor* acceptor) = 0;

    virtual int32_t EPollWait(struct epoll_event* events, 
                              int32_t maxevents, 
                              int32_t timeout_ms) = 0;

    virtual bool EPollAdd(int32_t fd, uint32_t events, void* context) = 0;

    virtual bool EPollDel(int32_t fd) = 0;

    bool EPollAddR(ITcpEndpoint* endpoint, void* context)
    {
        return EPollAdd(endpoint, EPOLLIN, context);
    }

    bool EPollAddR(int32_t fd, void* context)
    {
        return EPollAdd(fd, EPOLLIN, context);
    }

    bool EPollAddW(ITcpEndpoint* endpoint, void* context)
    {
        return EPollAdd(endpoint, EPOLLOUT, context);
    }

    bool EPollAddW(int32_t fd, void* context)
    {
        return EPollAdd(fd, EPOLLOUT, context);
    }

    bool EPollAddRW(ITcpEndpoint* endpoint, void* context)
    {
        return EPollAdd(endpoint, EPOLLIN | EPOLLOUT, context);
    }

    bool EPollAddRW(int32_t fd, void* context)
    {
        return EPollAdd(fd, EPOLLIN | EPOLLOUT, context);
    }

    virtual void Stop() = 0;

protected:
    ITcpEPoller() = default;
    virtual ~ITcpEPoller() = default;

    virtual bool Open(ITcpStack* tcp_stack) = 0;

    virtual void Close() = 0;
};

class ITcpAcceptor
{
public:
    static ITcpAcceptor* Create(ITcpStack* tcp_stack, 
                                const std::string& listen_ip, 
                                uint16_t listen_port, 
                                bool reuse_addr,
                                bool reuse_port);

    static void Destroy(ITcpAcceptor* tcp_acceptor);

    static bool EPollResult(uint32_t events)
    {
        return events & EPOLLIN;
    }

    virtual ITcpEndpoint* Accept() = 0;

    const std::string& listen_ip() const
    {
        return listen_ip_;
    }

    uint16_t listen_port() const
    {
        return listen_port_;
    }

protected:
    ITcpAcceptor() = default;
    virtual ~ITcpAcceptor() = default;

    virtual bool Open(ITcpStack* tcp_stack, 
                      const std::string& listen_ip, 
                      uint16_t listen_port, 
                      bool reuse_addr,
                      bool reuse_port) = 0;

    virtual void Close() = 0;

    std::string listen_ip_;
    uint16_t    listen_port_ = 0;
};

class ITcpEndpoint
{
public:
    enum class OptionType
    {
        kTcpNoDelay = 0,
        kSendBuffer,
        kRecvBuffer,
        kBlockMode,
        kOptionMax
    };

    enum class BindResult : int32_t
    {
        kSuccess = 0,
        kFailure,
        kAddrInUse
    };

    enum class ConnectResult : int32_t
    {
        kSuccess = 0,
        kFailure,
        kInProgress
    };

    static ITcpEndpoint* Create(ITcpStack* tcp_stack, bool reuse_addr, bool reuse_port);

    static void Destroy(ITcpEndpoint* tcp_endpoint);

    virtual bool SetOption(OptionType option_type, int32_t option_value) = 0;

    virtual int32_t Bind(uint16_t local_port) = 0;

    virtual int32_t Connect(const std::string& remote_ip, uint16_t remote_port) = 0;

    virtual bool EPollResult(uint32_t events) = 0;

    virtual ssize_t Send(const void* buffer, size_t len) = 0;

    virtual ssize_t Send(const struct iovec *iov, size_t iovcnt) = 0;

    virtual ssize_t Recv(char* buffer, size_t len) = 0;

    virtual ssize_t Recv(const struct iovec *iov, size_t iovcnt) = 0;

    virtual std::string LastError() const = 0;

    virtual void Close() = 0;

    const std::string& local_ip() const
    {
        return local_ip_;
    }

    uint16_t local_port() const
    {
        return local_port_;
    }

    const std::string& remote_ip() const
    {
        return remote_ip_;
    }

    uint16_t remote_port() const
    {
        return remote_port_;
    }

    int32_t endpoint_id() const
    {
        return endpoint_id_;
    }

protected:
    ITcpEndpoint() = default;
    virtual ~ITcpEndpoint() = default;

    virtual bool Open(ITcpStack* tcp_stack, bool reuse_addr, bool reuse_port) = 0;

    std::string local_ip_;
    uint16_t    local_port_ = 0;
    std::string remote_ip_;
    uint16_t    remote_port_ = 0;
    int32_t     endpoint_id_ = default_value::kInvalidFd;
};

}

}

#endif
