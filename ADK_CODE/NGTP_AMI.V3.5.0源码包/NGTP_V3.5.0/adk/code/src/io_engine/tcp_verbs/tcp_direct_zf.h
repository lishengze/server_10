#ifndef ADK_IMPL_IO_ENGINE_TCP_DIRECT_ZF_H_
#define ADK_IMPL_IO_ENGINE_TCP_DIRECT_ZF_H_

#include "tcp_interface.h"

#include <map>
#include <deque>
#include <mutex>
#include <iostream>
#include <functional>
#include <stddef.h>

#include <zf/zf.h>
#include <adk/arch/generic.h>
#include <adk/arch/synchronize.h>
#include <adk/lock_free_queue_variant.h>

#ifndef ZF_ERROR
#define ZF_ERROR(func_name, res) \
    errno = -res;                \
    TRACE_PRINT(func_name << " failed, deltail: " << strerror(errno))
#endif

#define _REACTOR_PERFORM_ATTR_ 8

namespace adk_impl
{

namespace verbs
{

struct zf_verbs
{
    int (*zf_init)(void);
    int (*zf_attr_alloc)(struct zf_attr**);
    void (*zf_attr_free)(struct zf_attr*);
    void (*zf_attr_reset)(struct zf_attr*);
    int (*zf_attr_set_str)(struct zf_attr*, const char*, const char*);
    int (*zf_attr_set_int)(struct zf_attr*, const char*, int64_t);
    int (*zf_stack_alloc)(struct zf_attr*, struct zf_stack**);
    int (*zf_stack_free)(struct zf_stack*);
#ifndef _REACTOR_PERFORM_ATTR_
    int (*zf_reactor_perform)(struct zf_stack*);
#else
    int (*zf_reactor_perform_attr)(struct zf_stack*, const struct zf_attr*);
#endif
    int (*zf_stack_has_pending_work)(struct zf_stack*);
    int (*zf_muxer_alloc)(struct zf_stack*, struct zf_muxer_set**);
    void (*zf_muxer_free)(struct zf_muxer_set*);
    int (*zf_muxer_add)(struct zf_muxer_set*, struct zf_waitable*, const struct epoll_event*);
    int (*zf_muxer_del)(struct zf_waitable*);
    int (*zf_muxer_wait)(struct zf_muxer_set*, struct epoll_event*, int, int64_t);
    struct zf_waitable* (*zftl_to_waitable)(struct zftl*);
    struct zf_waitable* (*zft_to_waitable)(struct zft*);
    int (*zftl_listen)(struct zf_stack*, const struct sockaddr*, socklen_t, const struct zf_attr*, struct zftl**);
    int (*zftl_free)(struct zftl*);
    int (*zftl_accept)(struct zftl*, struct zft**);
    int (*zft_alloc)(struct zf_stack*, const struct zf_attr*, struct zft_handle**);
    int (*zft_handle_free)(struct zft_handle*);
    int (*zft_addr_bind)(struct zft_handle*, const struct sockaddr*, socklen_t, int);
    int (*zft_connect)(struct zft_handle*, const struct sockaddr*, socklen_t, struct zft**);
    int (*zft_free)(struct zft*);
    int (*zft_state)(struct zft*);
    int (*zft_error)(struct zft*);
    ssize_t (*zft_send)(struct zft*, const struct iovec*, int, int);
    ssize_t (*zft_send_single)(struct zft*, const void*, size_t, int);
    int (*zft_recv)(struct zft*, const struct iovec*, int, int);
    void (*zft_zc_recv)(struct zft*, struct zft_msg*, int);
    int (*zft_zc_recv_done)(struct zft*, struct zft_msg*);
    void (*zft_getname)(struct zft*, struct sockaddr*, socklen_t*, struct sockaddr*, socklen_t*);
};

class Spinlock
{
public:
    Spinlock() : is_lock_(ATOMIC_FLAG_INIT)
    {
    }

    bool try_lock()
    {
        if ((*(volatile bool*)(&is_lock_)))
        {
            return false;
        }

        return !is_lock_.test_and_set(std::memory_order_acquire);;
    }

    void lock()
    {
        uint32_t relax_counter = 1;
        do 
        {
            if (try_lock())
            {
                break;
            }

            for (uint32_t index = 0; index < relax_counter; ++index)
            {
                ADK_PAUSE();
            }

            relax_counter = std::min<uint32_t>(relax_counter + 1, kCpuRelaxBackoff);
        } while (true);
    }

    void send_lock()
    {
        do 
        {
            if (try_lock())
            {
                break;
            }

            ADK_PAUSE();
        } while (true);
    }

    void unlock()
    {
        is_lock_.clear(std::memory_order_release);
    }

private:
    static constexpr uint32_t kCpuRelaxBackoff = 128;

    std::atomic_flag is_lock_;
};

using ReactorLock = Spinlock;

struct AsyncTaskZf
{
    volatile bool done;
    int result;
    std::function<int(void)> task_executor;
};

class TcpStackZf final : public ITcpStack
{
public:
    static constexpr DriveMode kDriveMode = DriveMode::kReactor;

    static struct zf_verbs* GetZfVerbs();

    bool io_parallel_support() const override
    {
        return false;
    }

    inline ADK_HOT int32_t ReactorPerform() override
    {
        if (ADK_UNLIKELY(ACCESS_ONCE(has_peeding_task_)))
        {
            DealPeedingTask();
            return 1;
        }

#ifdef _REACTOR_PERFORM_ATTR_
        return /*has_pending_work() && */reactor_perform_attr();
#else
        return /*has_pending_work() && */reactor_perform();
#endif
    }

    void ReactorTerminated() override
    {
        reactor_terminated_ = true;
    }

    DriveMode drive_mode() const override
    {
        return kDriveMode;
    }

    struct zf_stack* stack() const
    {
        return stack_;
    }

    const std::string& nic_name() const
    {
        return nic_name_;
    }

    inline int32_t reactor_perform_with_lock()
    {
        std::lock_guard<ReactorLock> _(reactor_lock_);
#ifndef _REACTOR_PERFORM_ATTR_
        return reactor_perform();
#else
        return reactor_perform_attr();
#endif
    }

#ifndef _REACTOR_PERFORM_ATTR_
    inline int32_t reactor_perform()
    {
        assert(zf_verbs_);
        return zf_verbs_->zf_reactor_perform(stack_);
    }
#else
    inline int32_t reactor_perform_attr()
    {
        assert(zf_verbs_);
        return zf_verbs_->zf_reactor_perform_attr(stack_, reactor_attr_);
    }
#endif

    inline int32_t has_pending_work()
    {
        assert(zf_verbs_);
        return zf_verbs_->zf_stack_has_pending_work(stack_);
    }

    ReactorLock& reactor_lock()
    {
        return reactor_lock_;
    }

    int32_t DoSyncTask(AsyncTaskZf* task);

private:
    TcpStackZf(const std::string& message_ip);
    virtual ~TcpStackZf();

    bool Open(const std::string& nic_name) override;

    void Close() override;

    void DealPeedingTask();

    struct zf_stack* stack_;

#ifdef _REACTOR_PERFORM_ATTR_
    struct zf_attr*  reactor_attr_;
#endif

    bool       has_peeding_task_;
    bool       reactor_terminated_;
    std::mutex peeding_task_lock_;
    std::deque<AsyncTaskZf*> peeding_task_queue_;

    struct zf_verbs* zf_verbs_;

    std::string      nic_name_;
    ReactorLock      reactor_lock_;

    friend class ITcpStack;
};

class TcpEPollerZfControl final : public ITcpEPoller
{
public:
    bool EPollAdd(ITcpEndpoint* endpoint, uint32_t events, void* context) override
    {
        assert(false);
        return false;
    }

    bool EPollAdd(ITcpEndpoint* endpoint, void* context) override;

    bool EPollDel(ITcpEndpoint* endpoint) override;

    bool EPollAdd(ITcpAcceptor* acceptor, void* context) override;

    bool EPollDel(ITcpAcceptor* acceptor) override;

    int32_t EPollWait(struct epoll_event* events, int32_t maxevents, int32_t timeout) override;

    bool EPollAdd(int32_t fd, uint32_t events, void* context) override
    {
        assert(false);
        return false;
    }

    bool EPollDel(int32_t fd) override
    {
        assert(false);
        return false;
    }

    void Stop() override;

private:
    struct PollerNode
    {
        uint32_t events;
        void*    context;
    };

    TcpEPollerZfControl() = default;
    virtual ~TcpEPollerZfControl();

    bool Open(ITcpStack* tcp_stack) override;

    void Close() override;

    std::map<void*, PollerNode> pollers_map_;

    friend class ITcpEPoller;
};

class TcpEPollerZfGeneral final : public ITcpEPoller
{
public:
    bool EPollAdd(ITcpEndpoint* endpoint, uint32_t events, void* context) override;

    bool EPollAdd(ITcpEndpoint* endpoint, void* context) override
    {
        assert(false);
        return false;
    }

    bool EPollDel(ITcpEndpoint* endpoint) override;

    bool EPollAdd(ITcpAcceptor* acceptor, void* context) override
    {
        assert(false);
        return false;
    }

    bool EPollDel(ITcpAcceptor* acceptor) override
    {
        assert(false);
        return false;
    }

    int32_t EPollWait(struct epoll_event* events, int32_t maxevents, int32_t timeout) override;

    bool EPollAdd(int32_t fd, uint32_t events, void* context) override;

    bool EPollDel(int32_t fd) override;

    void Stop() override;

private:
    struct PollerNode
    {
        int64_t  timer;
        uint32_t events;
        void*    context;
    };

    struct FakeEPoller
    {
        int64_t  timer;
        uint32_t events;
        void*    context;
    };

    TcpEPollerZfGeneral();
    virtual ~TcpEPollerZfGeneral();

    bool Open(ITcpStack* tcp_stack) override;

    void Close() override;

    int32_t    epoll_fd_;
    int64_t    epolling_fd_nr_;

    std::mutex pollers_lock_;
    std::map<void*, FakeEPoller> pollers_map_;

    friend class ITcpEPoller;
};

class TcpEPollerZfSpecial final : public ITcpEPoller
{
public:
    bool EPollAdd(ITcpEndpoint* endpoint, uint32_t events, void* context) override;

    bool EPollAdd(ITcpEndpoint* endpoint, void* context) override;

    bool EPollDel(ITcpEndpoint* endpoint) override;

    bool EPollAdd(ITcpAcceptor* acceptor, void* context) override;

    bool EPollDel(ITcpAcceptor* acceptor) override;

    int32_t EPollWait(struct epoll_event* events, int32_t maxevents, int32_t timeout) override;

    bool EPollAdd(int32_t fd, uint32_t events, void* context) override
    {
        assert(false);
        return false;
    }

    bool EPollDel(int32_t fd) override
    {
        assert(false);
        return false;
    }

    void Stop() override;

private:
    TcpEPollerZfSpecial();
    virtual ~TcpEPollerZfSpecial();

    bool Open(ITcpStack* tcp_stack) override;

    void Close() override;

    TcpStackZf*          tcp_stack_;
    struct zf_muxer_set* muxer_set_;

    friend class ITcpEPoller;
};

class TcpEPollerZf final : public ITcpEPoller
{
public:
    bool EPollAdd(ITcpEndpoint* endpoint, uint32_t events, void* context) override;

    bool EPollAdd(ITcpEndpoint* endpoint, void* context) override;

    bool EPollDel(ITcpEndpoint* endpoint) override;

    bool EPollAdd(ITcpAcceptor* acceptor, void* context) override;

    bool EPollDel(ITcpAcceptor* acceptor) override;

    int32_t EPollWait(struct epoll_event* events, int32_t maxevents, int32_t timeout) override;

    bool EPollAdd(int32_t fd, uint32_t events, void* context) override;

    bool EPollDel(int32_t fd) override;

    void Stop() override;

private:
    enum class PollerTypeZf
    {
        kCtrl,
        kFake,
        kMuxer,
        kTypeMax,
    };

    struct CtrlEPoller
    {
        uint32_t events;
        void*    context;
    };

    struct FakeEPoller
    {
        int64_t  timer;
        uint32_t events;
        void*    context;
    };

    TcpEPollerZf();
    virtual ~TcpEPollerZf();

    bool Open(ITcpStack* tcp_stack) override;

    void Close() override;

    bool MuxerAdd(struct zf_waitable* w, uint32_t events, void* context);

    bool MuxerAddRaw(struct zf_waitable* w, uint32_t events, void* context);

    bool MuxerDelete(struct zf_waitable* w);

    bool MuxerAddCtrl(void* zf_tcp, uint32_t events, void* context);

    bool MuxerDeleteCtrl(void* zf_tcp);

    struct zf_verbs*     zf_verbs_;
    TcpStackZf*          tcp_stack_;
    struct zf_muxer_set* muxer_set_;

    int32_t              epoll_fd_;
    int64_t              epolling_fd_nr_;

    PollerTypeZf         poller_type_;
    std::mutex           fake_epoller_lock_;
    std::map<void*, FakeEPoller> fake_epoller_map_;
    std::map<void*, CtrlEPoller> ctrl_epoller_map_;

    friend class ITcpEPoller;
};

class TcpAcceptorZf final : public ITcpAcceptor
{
public:
    ITcpEndpoint* Accept() override;

    struct zf_waitable* waitable() const
    {
        return waitable_;
    }

    struct zftl* listen_endpoint() const
    {
        return zftl_;
    }

private:
    TcpAcceptorZf();
    virtual ~TcpAcceptorZf();

    bool Open(ITcpStack* tcp_stack, 
              const std::string& listen_ip, 
              uint16_t listen_port, 
              bool reuse_addr,
              bool reuse_port) override;

    void Close() override;

    struct zftl* zftl_;
    TcpStackZf*  tcp_stack_;
    struct zf_waitable* waitable_;
    friend class ITcpAcceptor;
};

class TcpEndpointZf final : public ITcpEndpoint
{
public:
    using StackType =   TcpStackZf;
    using EPollerType = TcpEPollerZfGeneral;

    static constexpr bool kZcRecvSupport = true;

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
        assert(zf_verbs_);

        const auto zft_res = zf_verbs_->zft_send_single(zft_, buffer, len, 0);
        if (ADK_UNLIKELY(zft_res < 0))
        {
            errno = -zft_res;
            return -1;
        }

        return zft_res;
    }

    ADK_HOT inline ssize_t Send(const struct iovec *iov, size_t iovcnt) override
    {
        assert(zf_verbs_);

        const auto zft_res = zf_verbs_->zft_send(zft_, iov, iovcnt, 0);
        if (ADK_UNLIKELY(zft_res < 0))
        {
            errno = -zft_res;
            return -1;
        }

        return zft_res;
    }

    ADK_HOT inline ssize_t Recv(char* buffer, size_t len) override
    {
        struct iovec iov { buffer, len };
        return Recv(&iov, 1);
    }

    ADK_HOT inline ssize_t Recv(const struct iovec *iov, size_t iovcnt) override
    {
        assert(zf_verbs_);

        const auto zft_res = zf_verbs_->zft_recv(zft_, iov, iovcnt, 0);
        if (ADK_UNLIKELY(zft_res < 0))
        {
            errno = -zft_res;
            return -1;
        }

        return zft_res;
    }

    template<typename Callback>
    ADK_HOT inline ssize_t ZcRecv(const Callback& callback)
    {
        assert(zf_verbs_);

        ZftZeroCopyMsg zft_zc_msg;
        zft_zc_msg.msg.iovcnt = kZcRecvBatchSize;

        zf_verbs_->zft_zc_recv(zft_, &zft_zc_msg.msg, 0);
        if (zft_zc_msg.msg.iovcnt > 0)
        {
            for (int32_t iov_index = 0; iov_index < zft_zc_msg.msg.iovcnt; ++iov_index)
            {
                auto& iov_node = zft_zc_msg.msg.iov[iov_index];
                if (iov_node.iov_len > 0)
                {
                    callback((char*)iov_node.iov_base, (ssize_t)iov_node.iov_len);
                }
                else
                {
                    break;
                }
            }

            const auto zft_res = zf_verbs_->zft_zc_recv_done(zft_, &zft_zc_msg.msg);
            errno = -zft_res;
            return zft_res;
        }

        errno = EAGAIN;
        return -1;
    }

    std::string LastError() const override;

    void Close() override;

    struct zft* endpoint() const
    {
        return zft_;
    }

    struct zf_waitable* waitable() const
    {
        return waitable_;
    }

private:
    static constexpr uint32_t kZcRecvBatchSize = 8;
    union ZftZeroCopyMsg
    {
        struct zft_msg msg;
        char fix_size[sizeof(iovec[kZcRecvBatchSize]) + sizeof(zft_msg)];
        iovec* getiov()
        {
            return (iovec*)((char*)this + sizeof(zft_msg));
        }
    };

#if __GNUC__ <= 4
    struct ZftZeroCopyMsg_bk
    {
        struct zft_msg msg;
        struct iovec iov[kZcRecvBatchSize];
    };
        static_assert(sizeof(ZftZeroCopyMsg) == sizeof(ZftZeroCopyMsg_bk), "struct size misMatch");
        static_assert(sizeof (zft_msg) == offsetof(ZftZeroCopyMsg_bk, iov), "struct loc mismatch.");
#endif

    TcpEndpointZf();
    virtual ~TcpEndpointZf();

    bool Open(ITcpStack* tcp_stack, bool reuse_addr, bool reuse_port) override;

    TcpStackZf*         tcp_stack_;

    struct zf_verbs*    zf_verbs_;
    struct zf_attr*     zft_attr_;
    struct sockaddr_in  local_addr_;

    struct zft*         zft_;
    struct zf_waitable* waitable_;
    friend class ITcpEndpoint;
    friend class TcpAcceptorZf;
};

}

}

#endif
