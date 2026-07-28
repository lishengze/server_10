#if defined (__x86_64__)
#include "tcp_direct_zf.h"

#include <adk/util.h>

#include <deque>

#include <time.h>
#include <dlfcn.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

namespace adk_impl
{

namespace verbs
{

constexpr uint32_t Spinlock::kCpuRelaxBackoff;

static zf_verbs s_zf_verbs;

static int32_t s_ids_register = 0;
static std::mutex s_reuse_ids_lock;
static std::deque<uint32_t> s_reuse_ids_deq;

static bool InitZfVerbs()
{
    void* zf_verbs_handle = dlopen("libonload_zf.so", RTLD_LAZY);
    if (ADK_UNLIKELY(nullptr == zf_verbs_handle))
    {
        TRACE_PRINT("@> dlopen libonload_zf.so failed");
        return false;
    }

    *(void**)(&s_zf_verbs.zf_init) = dlsym(zf_verbs_handle, "zf_init");
    *(void**)(&s_zf_verbs.zf_attr_alloc) = dlsym(zf_verbs_handle, "zf_attr_alloc");
    *(void**)(&s_zf_verbs.zf_attr_free) = dlsym(zf_verbs_handle, "zf_attr_free");
    *(void**)(&s_zf_verbs.zf_attr_reset) = dlsym(zf_verbs_handle, "zf_attr_reset");
    *(void**)(&s_zf_verbs.zf_attr_set_str) = dlsym(zf_verbs_handle, "zf_attr_set_str");
    *(void**)(&s_zf_verbs.zf_attr_set_int) = dlsym(zf_verbs_handle, "zf_attr_set_int");
    *(void**)(&s_zf_verbs.zf_stack_alloc) = dlsym(zf_verbs_handle, "zf_stack_alloc");
    *(void**)(&s_zf_verbs.zf_stack_free) = dlsym(zf_verbs_handle, "zf_stack_free");
#ifndef _REACTOR_PERFORM_ATTR_
    *(void**)(&s_zf_verbs.zf_reactor_perform) = dlsym(zf_verbs_handle, "zf_reactor_perform");
#else
    *(void**)(&s_zf_verbs.zf_reactor_perform_attr) = dlsym(zf_verbs_handle, "zf_reactor_perform_attr");
#endif
    *(void**)(&s_zf_verbs.zf_stack_has_pending_work) = dlsym(zf_verbs_handle, "zf_stack_has_pending_work");
    *(void**)(&s_zf_verbs.zf_muxer_alloc) = dlsym(zf_verbs_handle, "zf_muxer_alloc");
    *(void**)(&s_zf_verbs.zf_muxer_free) = dlsym(zf_verbs_handle, "zf_muxer_free");
    *(void**)(&s_zf_verbs.zf_muxer_add) = dlsym(zf_verbs_handle, "zf_muxer_add");
    *(void**)(&s_zf_verbs.zf_muxer_del) = dlsym(zf_verbs_handle, "zf_muxer_del");
    *(void**)(&s_zf_verbs.zf_muxer_wait) = dlsym(zf_verbs_handle, "zf_muxer_wait");
    *(void**)(&s_zf_verbs.zftl_to_waitable) = dlsym(zf_verbs_handle, "zftl_to_waitable");
    *(void**)(&s_zf_verbs.zft_to_waitable) = dlsym(zf_verbs_handle, "zft_to_waitable");
    *(void**)(&s_zf_verbs.zftl_listen) = dlsym(zf_verbs_handle, "zftl_listen");
    *(void**)(&s_zf_verbs.zftl_free) = dlsym(zf_verbs_handle, "zftl_free");
    *(void**)(&s_zf_verbs.zftl_accept) = dlsym(zf_verbs_handle, "zftl_accept");
    *(void**)(&s_zf_verbs.zft_alloc) = dlsym(zf_verbs_handle, "zft_alloc");
    *(void**)(&s_zf_verbs.zft_handle_free) = dlsym(zf_verbs_handle, "zft_handle_free");
    *(void**)(&s_zf_verbs.zft_addr_bind) = dlsym(zf_verbs_handle, "zft_addr_bind");
    *(void**)(&s_zf_verbs.zft_connect) = dlsym(zf_verbs_handle, "zft_connect");
    *(void**)(&s_zf_verbs.zft_free) = dlsym(zf_verbs_handle, "zft_free");
    *(void**)(&s_zf_verbs.zft_state) = dlsym(zf_verbs_handle, "zft_state");
    *(void**)(&s_zf_verbs.zft_error) = dlsym(zf_verbs_handle, "zft_error");
    *(void**)(&s_zf_verbs.zft_send) = dlsym(zf_verbs_handle, "zft_send");
    *(void**)(&s_zf_verbs.zft_send_single) = dlsym(zf_verbs_handle, "zft_send_single");
    *(void**)(&s_zf_verbs.zft_recv) = dlsym(zf_verbs_handle, "zft_recv");
    *(void**)(&s_zf_verbs.zft_zc_recv) = dlsym(zf_verbs_handle, "zft_zc_recv");
    *(void**)(&s_zf_verbs.zft_zc_recv_done) = dlsym(zf_verbs_handle, "zft_zc_recv_done");
    *(void**)(&s_zf_verbs.zft_getname) = dlsym(zf_verbs_handle, "zft_getname");

    if (ADK_UNLIKELY(s_zf_verbs.zf_init() < 0))
    {
        TRACE_PRINT("@> zf_init failed");
        return false;
    }

    TRACE_PRINT("@> zf_init");

    return true;
}

TcpStackZf::TcpStackZf(const std::string& message_ip)
{
    stack_type_ = StackType::kStackZf;
    message_ip_ = message_ip;

    stack_ = nullptr;
#ifdef _REACTOR_PERFORM_ATTR_
    reactor_attr_ = nullptr;
#endif
    has_peeding_task_ = false;
    reactor_terminated_ = false;
    zf_verbs_ = TcpStackZf::GetZfVerbs();
}

TcpStackZf::~TcpStackZf()
{
}

bool TcpStackZf::Open(const std::string& nic_name)
{
    if (ADK_UNLIKELY(nic_name.empty()))
    {
        return false;
    }

    nic_name_ = nic_name;

    static bool s_verbs = InitZfVerbs();
    if (ADK_UNLIKELY(!s_verbs))
    {
        return false;
    }

    Close();

    struct zf_attr* attr_out;
    if (ADK_UNLIKELY(s_zf_verbs.zf_attr_alloc(&attr_out) < 0))
    {
        return false;
    }

    TRACE_PRINT("@> zf_attr_alloc([out]" << (void*)attr_out << ")");

    OnExit<> on_exit([attr_out]() {
        s_zf_verbs.zf_attr_free(attr_out);
        TRACE_PRINT("@> zf_attr_free(" << (void*)attr_out << ")");
    });

    if (ADK_UNLIKELY(s_zf_verbs.zf_attr_set_str(attr_out, "interface", nic_name_.c_str()) < 0))
    {
        return false;
    }

    TRACE_PRINT("@> zf_attr_set_str(" << (void*)attr_out << ", \"interface\", " << nic_name_ << ")");

    s_zf_verbs.zf_attr_set_str(attr_out, "ctpio_mode", "ct");
    TRACE_PRINT("@> zf_attr_set_str(" << (void*)attr_out << ", \"ctpio_mode\", \"ct\")");

    if (ADK_UNLIKELY(s_zf_verbs.zf_stack_alloc(attr_out, &stack_) < 0))
    {
        s_zf_verbs.zf_attr_reset(attr_out);
        s_zf_verbs.zf_attr_set_int(attr_out, "pio", 1);
        s_zf_verbs.zf_attr_set_str(attr_out, "interface", nic_name_.c_str());
        TRACE_PRINT("@> zf_attr_set_str(" << (void*)attr_out << ", \"pio\", 1)");
        if (ADK_UNLIKELY(s_zf_verbs.zf_stack_alloc(attr_out, &stack_) < 0))
        {
            s_zf_verbs.zf_attr_set_int(attr_out, "pio", 0);
            if (ADK_UNLIKELY(s_zf_verbs.zf_stack_alloc(attr_out, &stack_) < 0))
            {
                return false;
            }
        }
    }

    TRACE_PRINT("@> zf_stack_alloc(" << (void*)attr_out << ", [out]" << (void*)stack_ << ")");

#ifdef _REACTOR_PERFORM_ATTR_
    if (ADK_UNLIKELY(s_zf_verbs.zf_attr_alloc(&reactor_attr_) < 0))
    {
        return false;
    }

    assert(reactor_attr_);
    s_zf_verbs.zf_attr_set_int(reactor_attr_, "reactor_spin_count", _REACTOR_PERFORM_ATTR_);
    TRACE_PRINT("@> zf_attr_set_int(" << (void*)reactor_attr_ << ", " << "[\"reactor_spin_count\"] = " 
                                      << _REACTOR_PERFORM_ATTR_ << ")");
#endif
    return true;
}

void TcpStackZf::Close()
{
    if (nullptr != stack_)
    {
        s_zf_verbs.zf_stack_free(stack_);
        TRACE_PRINT("@> zf_stack_free(" << (void*)stack_ << ")");
        stack_ = nullptr;
    }
}

void TcpStackZf::DealPeedingTask()
{
    std::lock_guard<std::mutex> _(peeding_task_lock_);
    while (!peeding_task_queue_.empty())
    {
        auto* const aync_task = peeding_task_queue_.front();
        assert(aync_task);
        assert(!aync_task->done);
        aync_task->result = aync_task->task_executor();

#ifndef _REACTOR_PERFORM_ATTR_
        reactor_perform();
#else
        reactor_perform_attr();
#endif
        ADK_BARRIER();
        aync_task->done = true;
        peeding_task_queue_.pop_front();
    }

    has_peeding_task_ = false;
}

int32_t TcpStackZf::DoSyncTask(AsyncTaskZf* task)
{
    {
        std::lock_guard<std::mutex> _(peeding_task_lock_);
        peeding_task_queue_.push_back(task);
        has_peeding_task_ = true;
    }

    do
    {
        if (ADK_UNLIKELY(ACCESS_ONCE(reactor_terminated_)))
        {
            DealPeedingTask();
        }
        else
        {
            usleep(0);
        }
    } while (!task->done);

    return ACCESS_ONCE(task->result);
}

struct zf_verbs* TcpStackZf::GetZfVerbs()
{
    return &s_zf_verbs;
}

bool TcpEPollerZfControl::EPollAdd(ITcpEndpoint* endpoint, void* context)
{
    auto* const zf_tcp = static_cast<TcpEndpointZf*>(endpoint)->endpoint();
    assert(zf_tcp);

    if (ADK_UNLIKELY(pollers_map_.end() != pollers_map_.find(zf_tcp)))
    {
        return false;
    }

    auto& poller_node = pollers_map_[zf_tcp];
    poller_node.events = EPOLLOUT;
    poller_node.context = context;
    return true;
}

bool TcpEPollerZfControl::EPollDel(ITcpEndpoint* endpoint)
{
    auto* const zf_tcp = static_cast<TcpEndpointZf*>(endpoint)->endpoint();
    assert(zf_tcp);

    return (pollers_map_.erase(zf_tcp) > 0);
}

bool TcpEPollerZfControl::EPollAdd(ITcpAcceptor* acceptor, void* context)
{
    auto* const zfl_tcp = static_cast<TcpAcceptorZf*>(acceptor)->listen_endpoint();
    assert(zfl_tcp);

    if (ADK_UNLIKELY(pollers_map_.end() != pollers_map_.find(zfl_tcp)))
    {
        return false;
    }

    auto& poller_node = pollers_map_[zfl_tcp];
    poller_node.events = EPOLLIN;
    poller_node.context = context;
    return true;
}

bool TcpEPollerZfControl::EPollDel(ITcpAcceptor* acceptor)
{
    auto* const zfl_tcp = static_cast<TcpAcceptorZf*>(acceptor)->listen_endpoint();
    assert(zfl_tcp);

    return (pollers_map_.erase(zfl_tcp) > 0);
}

int32_t TcpEPollerZfControl::EPollWait(struct epoll_event* events, int32_t maxevents, int32_t timeout)
{
    int32_t epoll_nr = 0;
    for (auto& poller_node : pollers_map_)
    {
        if (poller_node.second.events & EPOLLOUT)
        {
            const auto zft_status = s_zf_verbs.zft_state((struct zft*)(poller_node.first));
            if (TCP_ESTABLISHED == zft_status)
            {
                auto& ev_node = events[epoll_nr];
                ev_node.events = EPOLLOUT;
                ev_node.data.ptr = poller_node.second.context;
                ++epoll_nr;
            }
            else if (TCP_SYN_SENT != zft_status)
            {
                auto& ev_node = events[epoll_nr];
                ev_node.events = EPOLLOUT | EPOLLERR;
                ev_node.data.ptr = poller_node.second.context;
                ++epoll_nr;
            }
        }
        else
        {
            auto& ev_node = events[epoll_nr];
            ev_node.events = EPOLLIN;
            ev_node.data.ptr = poller_node.second.context;
            ++epoll_nr;
        }

        if (maxevents == epoll_nr)
        {
            break;
        }
    }

    usleep(1000);
    return epoll_nr;
}

TcpEPollerZfControl::~TcpEPollerZfControl()
{
    Close();
}

bool TcpEPollerZfControl::Open(ITcpStack* tcp_stack)
{
    return true;
}

void TcpEPollerZfControl::Stop()
{
    Close();
}

void TcpEPollerZfControl::Close()
{

}

TcpEPollerZfGeneral::TcpEPollerZfGeneral()
{
    epoll_fd_ = default_value::kInvalidFd;
    epolling_fd_nr_ = 0;
}

bool TcpEPollerZfGeneral::EPollAdd(ITcpEndpoint* endpoint, uint32_t events, void* context)
{
    auto* const zf_tcp = static_cast<TcpEndpointZf*>(endpoint)->waitable();
    assert(zf_tcp);

    std::lock_guard<std::mutex> _(pollers_lock_);
    if (ADK_UNLIKELY(pollers_map_.end() != pollers_map_.find(zf_tcp)))
    {
        return false;
    }

    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
    auto& epoller = pollers_map_[zf_tcp];
    epoller.context = context;
    epoller.events = events;
    epoller.timer = current_time.tv_nsec + current_time.tv_sec * 1000000000;

    return true;
}

bool TcpEPollerZfGeneral::EPollAdd(int32_t fd, uint32_t events, void* context)
{
    __sync_fetch_and_add(&epolling_fd_nr_, 1);

    struct epoll_event add_event;
    add_event.events = events;
    add_event.data.ptr = context;
    return (0 == epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &add_event));

    return true;
}

bool TcpEPollerZfGeneral::EPollDel(ITcpEndpoint* endpoint)
{
    auto* const zf_tcp = static_cast<TcpEndpointZf*>(endpoint)->endpoint();
    assert(zf_tcp);

    return (pollers_map_.erase(zf_tcp) > 0);
}

bool TcpEPollerZfGeneral::EPollDel(int32_t fd)
{
    assert(epoll_fd_ > 0);

    __attribute__((unused)) const auto nr = __sync_fetch_and_sub(&epolling_fd_nr_, 1);
    assert(nr > 0);

    return (0 == epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr));
}

int32_t TcpEPollerZfGeneral::EPollWait(struct epoll_event* events, int32_t maxevents, int32_t timeout)
{
    constexpr int64_t kRxEPollTimeoutNs = 1000000;
    constexpr int64_t kTxEPollTimeoutNs = 1000000;

    int32_t epoll_nr = 0;
    if (ACCESS_ONCE(epolling_fd_nr_) > 0)
    {
        const auto epoll_nr1 = epoll_wait(epoll_fd_, events, maxevents, 0);
        if (epoll_nr1 > 0)
        {
            epoll_nr += epoll_nr1;
            if (maxevents == epoll_nr)
            {
                return maxevents;
            }
        }
    }

    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
    const auto current_ns = current_time.tv_nsec + current_time.tv_sec * 1000000000;

    {
        std::lock_guard<std::mutex> _(pollers_lock_);
        for (auto& poller_node : pollers_map_)
        {
            if (((poller_node.second.events & EPOLLOUT)
                && (poller_node.second.timer + kTxEPollTimeoutNs < current_ns))
                || ((poller_node.second.events & EPOLLIN)
                    && (poller_node.second.timer + kRxEPollTimeoutNs < current_ns)))
            {
                auto& ev_node = events[epoll_nr];
                ev_node.events = poller_node.second.events;
                ev_node.data.ptr = poller_node.second.context;

                if (maxevents == ++epoll_nr)
                {
                    return maxevents;
                }
            }
        }
    }

    if (0 == epoll_nr)
    {
        usleep(0);
    }

    return epoll_nr;
}

TcpEPollerZfGeneral::~TcpEPollerZfGeneral()
{
    Close();
}

bool TcpEPollerZfGeneral::Open(ITcpStack* tcp_stack)
{
    Close();

    epoll_fd_ = epoll_create1(0);
    if (ADK_UNLIKELY(default_value::kInvalidFd == epoll_fd_))
    {
        return false;
    }

    return true;
}

void TcpEPollerZfGeneral::Stop()
{
    Close();
}

void TcpEPollerZfGeneral::Close()
{
    if (default_value::kInvalidFd != epoll_fd_)
    {
        close(epoll_fd_);
        epoll_fd_ = default_value::kInvalidFd;
    }
}

TcpEPollerZfSpecial::TcpEPollerZfSpecial()
{
    tcp_stack_ = nullptr;
    muxer_set_ = nullptr;
}

bool TcpEPollerZfSpecial::EPollAdd(ITcpEndpoint* endpoint, uint32_t events, void* context)
{
    assert(endpoint);
    auto* const zf_w = static_cast<TcpEndpointZf*>(endpoint)->waitable();
    assert(zf_w);

    struct epoll_event add_event;
    add_event.events = events | EPOLLHUP | EPOLLRDHUP | EPOLLERR;
    add_event.data.ptr = context;

    assert(muxer_set_);
    const auto add_res = s_zf_verbs.zf_muxer_add(muxer_set_, zf_w, &add_event);
    if (ADK_UNLIKELY(add_res < 0))
    {
        ZF_ERROR("zf_muxer_add", add_res);
        return false;
    }

    TRACE_PRINT("@> zf_muxer_add(" << (void*)muxer_set_ << ", "
        << (void*)zf_w << ", <" << add_event.events << ">)");
    return true;
}

bool TcpEPollerZfSpecial::EPollAdd(ITcpEndpoint* endpoint, void* context)
{
    return EPollAdd(endpoint, EPOLLOUT, context);
}

bool TcpEPollerZfSpecial::EPollAdd(ITcpAcceptor* acceptor, void* context)
{
    assert(acceptor);
    auto* const zf_w = static_cast<TcpAcceptorZf*>(acceptor)->waitable();
    assert(zf_w);

    struct epoll_event add_event;
    add_event.events = EPOLLIN | EPOLLHUP | EPOLLRDHUP | EPOLLERR;
    add_event.data.ptr = context;

    assert(muxer_set_);
    const auto add_res = s_zf_verbs.zf_muxer_add(muxer_set_, zf_w, &add_event);
    if (ADK_UNLIKELY(add_res < 0))
    {
        ZF_ERROR("zf_muxer_add", add_res);
        return false;
    }

    TRACE_PRINT("@> zf_muxer_add(" << (void*)muxer_set_ << ", "
        << (void*)zf_w << ", <" << add_event.events << ">)");
    return true;
}

bool TcpEPollerZfSpecial::EPollDel(ITcpEndpoint* endpoint)
{
    assert(endpoint);
    auto* const zf_w = static_cast<TcpEndpointZf*>(endpoint)->waitable();
    assert(zf_w);

    TRACE_PRINT("@> zf_muxer_del(" << (void*)zf_w << ")");
    return (0 == s_zf_verbs.zf_muxer_del(zf_w));
}

bool TcpEPollerZfSpecial::EPollDel(ITcpAcceptor* acceptor)
{
    assert(acceptor);
    auto* const zf_w = static_cast<TcpAcceptorZf*>(acceptor)->waitable();
    assert(zf_w);

    TRACE_PRINT("@> zf_muxer_del(" << (void*)zf_w << ")");
    return (0 == s_zf_verbs.zf_muxer_del(zf_w));
}

int32_t TcpEPollerZfSpecial::EPollWait(struct epoll_event* events, int32_t maxevents, int32_t timeout)
{
    const auto wait_nr = s_zf_verbs.zf_muxer_wait(muxer_set_,
        events,
        maxevents,
        ((int64_t)timeout) * 1000000);
    TRACE_PRINT("@> zf_muxer_wait(" << (void*)muxer_set_ << ", "
        << (void*)events << ", " << maxevents << ", "
        << ((int64_t)timeout) * 1000000 << ") return " << wait_nr);
    return wait_nr;
}

TcpEPollerZfSpecial::~TcpEPollerZfSpecial()
{
    Close();
}

bool TcpEPollerZfSpecial::Open(ITcpStack* tcp_stack)
{
    Close();

    if (ADK_UNLIKELY(s_zf_verbs.zf_muxer_alloc(((TcpStackZf*)tcp_stack)->stack(), &muxer_set_) < 0))
    {
        return false;
    }

    TRACE_PRINT("@> zf_muxer_alloc(" << (void*)((TcpStackZf*)tcp_stack)->stack()
        << ", [out]" << (void*)muxer_set_ << ")");

    tcp_stack_ = (TcpStackZf*)tcp_stack;
    return true;
}

void TcpEPollerZfSpecial::Stop()
{
    Close();
}

void TcpEPollerZfSpecial::Close()
{
    if (nullptr != muxer_set_)
    {
        s_zf_verbs.zf_muxer_free(muxer_set_);
        TRACE_PRINT("@> zf_muxer_free(" << (void*)muxer_set_ << ")");
        muxer_set_ = nullptr;
    }
}

TcpEPollerZf::TcpEPollerZf()
{
    zf_verbs_ = TcpStackZf::GetZfVerbs();

    tcp_stack_ = nullptr;
    muxer_set_ = nullptr;

    epoll_fd_ = default_value::kInvalidFd;
    epolling_fd_nr_ = 0;

    poller_type_ = PollerTypeZf::kTypeMax;
}

TcpEPollerZf::~TcpEPollerZf()
{
    Close();
}

void TcpEPollerZf::Stop()
{
    Close();
}

bool TcpEPollerZf::Open(ITcpStack* tcp_stack)
{
    Close();

    epoll_fd_ = epoll_create1(0);
    if (ADK_UNLIKELY(default_value::kInvalidFd == epoll_fd_))
    {
        return false;
    }

    if (ADK_UNLIKELY(s_zf_verbs.zf_muxer_alloc(((TcpStackZf*)tcp_stack)->stack(), &muxer_set_) < 0))
    {
        return false;
    }

    TRACE_PRINT("@> zf_muxer_alloc(" << (void*)((TcpStackZf*)tcp_stack)->stack() 
                << ", [out]" << (void*)muxer_set_ << ")");

    tcp_stack_ = (TcpStackZf*)tcp_stack;
    return true;
}

void TcpEPollerZf::Close()
{
    if (nullptr != muxer_set_)
    {
        s_zf_verbs.zf_muxer_free(muxer_set_);
        TRACE_PRINT("@> zf_muxer_free(" << (void*)muxer_set_ << ")");
        muxer_set_ = nullptr;
    }

    if (default_value::kInvalidFd != epoll_fd_)
    {
        close(epoll_fd_);
        epoll_fd_ = default_value::kInvalidFd;
    }
}

bool TcpEPollerZf::MuxerAdd(struct zf_waitable* w, uint32_t events, void* context)
{
#if 0
    if (ADK_UNLIKELY(events & EPOLLIN))
    {
        return MuxerAddRaw(w, events, context);
    }
    else
    {
#endif
        std::lock_guard<std::mutex> _(fake_epoller_lock_);
        if (ADK_UNLIKELY(fake_epoller_map_.end() != fake_epoller_map_.find(w)))
        {
            return false;
        }

        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
        auto& epoller = fake_epoller_map_[w];
        epoller.context = context;
        epoller.events = events;
        epoller.timer = current_time.tv_nsec + current_time.tv_sec * 1000000000;
#if 0
    }
#endif
    return true;
}

bool TcpEPollerZf::MuxerAddCtrl(void* zf_tcp, uint32_t events, void* context)
{
    if (PollerTypeZf::kTypeMax == ACCESS_ONCE(poller_type_))
    {
        poller_type_ = PollerTypeZf::kCtrl;
    }

    if (ADK_UNLIKELY(ctrl_epoller_map_.end() != ctrl_epoller_map_.find(zf_tcp)))
    {
        return false;
    }

    auto& ctrl_epoller = ctrl_epoller_map_[zf_tcp];
    ctrl_epoller.events = events;
    ctrl_epoller.context = context;
    return true;
}

bool TcpEPollerZf::MuxerDeleteCtrl(void* zf_tcp)
{
    return (ctrl_epoller_map_.erase(zf_tcp) > 0);
}

bool TcpEPollerZf::MuxerAddRaw(struct zf_waitable* w, uint32_t events, void* context)
{
    if (PollerTypeZf::kTypeMax == ACCESS_ONCE(poller_type_))
    {
        poller_type_ = PollerTypeZf::kMuxer;
    }

    struct epoll_event add_event;
    add_event.events = events | EPOLLHUP | EPOLLRDHUP | EPOLLERR;
    add_event.data.ptr = context;

    assert(muxer_set_);
    const auto add_res = zf_verbs_->zf_muxer_add(muxer_set_, w, &add_event);

    if (ADK_UNLIKELY(add_res < 0))
    {
        ZF_ERROR("zf_muxer_add", add_res);
        return false;
    }

    TRACE_PRINT("@> zf_muxer_add(" << (void*)muxer_set_ << ", "
                << (void*)w << ", <" << add_event.events << ">)");
    return true;
}

bool TcpEPollerZf::MuxerDelete(struct zf_waitable* w)
{
    if (PollerTypeZf::kFake == ACCESS_ONCE(poller_type_))
    {
        std::lock_guard<std::mutex> _(fake_epoller_lock_);
        if (fake_epoller_map_.erase(w) > 0)
        {
            return true;
        }
    }
    else if (PollerTypeZf::kMuxer == ACCESS_ONCE(poller_type_))
    {
        TRACE_PRINT("@> zf_muxer_del(" << (void*)w << ")");
        return (0 == s_zf_verbs.zf_muxer_del(w));
    }

    assert(false);
    return false;
}

int32_t TcpEPollerZf::EPollWait(struct epoll_event* events, int32_t maxevents, int32_t timeout)
{
    constexpr int64_t kRxEPollTimeoutNs = 0;
    constexpr int64_t kTxEPollTimeoutNs = 0;

    const auto poller_type = ACCESS_ONCE(poller_type_);
    if (PollerTypeZf::kFake == poller_type)
    {
        int32_t epoll_nr = 0;
        if (ACCESS_ONCE(epolling_fd_nr_) > 0)
        {
            const auto epoll_nr1 = epoll_wait(epoll_fd_, events, maxevents, 0);
            if (epoll_nr1 > 0)
            {
                epoll_nr += epoll_nr1;
                if (maxevents == epoll_nr)
                {
                    return maxevents;
                }
            }
        }

        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
        const auto current_ns = current_time.tv_nsec + current_time.tv_sec * 1000000000;

        {
            std::lock_guard<std::mutex> _(fake_epoller_lock_);
            for (auto& fake_epoller : fake_epoller_map_)
            {
                if (fake_epoller.second.events & EPOLLOUT)
                {
                    if (fake_epoller.second.timer + kTxEPollTimeoutNs < current_ns)
                    {
                        auto& ev_node = events[epoll_nr];
                        ev_node.events = fake_epoller.second.events;
                        ev_node.data.ptr = fake_epoller.second.context;
                        ++epoll_nr;

                        if (maxevents == epoll_nr)
                        {
                            return maxevents;
                        }
                    }
                }
                else if (fake_epoller.second.events & EPOLLIN)
                {
                    if (fake_epoller.second.timer + kRxEPollTimeoutNs < current_ns)
                    {
                        auto& ev_node = events[epoll_nr];
                        ev_node.events = fake_epoller.second.events;
                        ev_node.data.ptr = fake_epoller.second.context;
                        ++epoll_nr;

                        if (maxevents == epoll_nr)
                        {
                            return maxevents;
                        }
                    }
                }
            }
        }

        if (0 == epoll_nr)
        {
            usleep(0);
        }

        return epoll_nr;
    }
    else if (PollerTypeZf::kCtrl == poller_type)
    {
        tcp_stack_->reactor_perform_with_lock();

        int32_t epoll_nr = 0;
        for (auto& ctrl_epoller : ctrl_epoller_map_)
        {
            if (ctrl_epoller.second.events & EPOLLOUT)
            {
                const auto zft_status = s_zf_verbs.zft_state((struct zft*)(ctrl_epoller.first));
                if (TCP_ESTABLISHED == zft_status)
                {
                    auto& ev_node = events[epoll_nr];
                    ev_node.events = EPOLLOUT;
                    ev_node.data.ptr = ctrl_epoller.second.context;
                    ++epoll_nr;
                }
                else if (TCP_SYN_SENT != zft_status)
                {
                    auto& ev_node = events[epoll_nr];
                    ev_node.events = EPOLLOUT | EPOLLERR;
                    ev_node.data.ptr = ctrl_epoller.second.context;
                    ++epoll_nr;
                }
            }
            else
            {
                auto& ev_node = events[epoll_nr];
                ev_node.events = EPOLLIN;
                ev_node.data.ptr = ctrl_epoller.second.context;
                ++epoll_nr;
            }

            if (maxevents == epoll_nr)
            {
                break;
            }
        }

        usleep(1000);
        return epoll_nr;
    }
    else if (PollerTypeZf::kMuxer == poller_type)
    {
        return zf_verbs_->zf_muxer_wait(muxer_set_,
                                        events,
                                        maxevents,
                                        ((int64_t)timeout) * 1000000);
    }

    assert(false);
    return 0;
}

bool TcpEPollerZf::EPollAdd(ITcpEndpoint* endpoint, uint32_t events, void* context)
{
    assert(endpoint);
    return MuxerAdd(((TcpEndpointZf*)endpoint)->waitable(), events, context);
}

bool TcpEPollerZf::EPollAdd(int32_t fd, uint32_t events, void* context)
{
    assert(epoll_fd_ > 0);

    if (PollerTypeZf::kTypeMax == ACCESS_ONCE(poller_type_))
    {
        poller_type_ = PollerTypeZf::kFake;
    }

    __sync_fetch_and_add(&epolling_fd_nr_, 1);

    struct epoll_event add_event;
    add_event.events = events;
    add_event.data.ptr = context;
    return (0 == epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &add_event));
}

bool TcpEPollerZf::EPollAdd(ITcpEndpoint* endpoint, void* context)
{
    assert(endpoint);
    return MuxerAddCtrl(((TcpEndpointZf*)endpoint)->endpoint(), EPOLLOUT, context);

    //return MuxerAddRaw(((TcpEndpointZf*)endpoint)->waitable(), EPOLLOUT, context);
}

bool TcpEPollerZf::EPollAdd(ITcpAcceptor* acceptor, void* context)
{
    assert(acceptor);
    return MuxerAddCtrl(((TcpAcceptorZf*)acceptor)->listen_endpoint(), EPOLLIN, context);

    //return MuxerAddRaw(((TcpAcceptorZf*)acceptor)->waitable(), EPOLLIN, context);
}

bool TcpEPollerZf::EPollDel(int32_t fd)
{
    assert(epoll_fd_ > 0);

    __attribute__((unused)) const auto nr = __sync_fetch_and_sub(&epolling_fd_nr_, 1);
    assert(nr > 0);

    return (0 == epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr));
}

bool TcpEPollerZf::EPollDel(ITcpEndpoint* endpoint)
{
    assert(endpoint);
    if (PollerTypeZf::kCtrl == ACCESS_ONCE(poller_type_))
    {
        return MuxerDeleteCtrl(((TcpEndpointZf*)endpoint)->endpoint());
    }
    else
    {
        return MuxerDelete(((TcpEndpointZf*)endpoint)->waitable());
    }
}

bool TcpEPollerZf::EPollDel(ITcpAcceptor* acceptor)
{
    assert(acceptor);
    return MuxerDeleteCtrl(((TcpAcceptorZf*)acceptor)->listen_endpoint());

    //return MuxerDelete(((TcpAcceptorZf*)acceptor)->waitable());
}

TcpAcceptorZf::TcpAcceptorZf()
{
    zftl_ = nullptr;
    tcp_stack_ = nullptr;
    waitable_ = nullptr;
}

TcpAcceptorZf::~TcpAcceptorZf()
{
    Close();
}

bool TcpAcceptorZf::Open(ITcpStack* tcp_stack, 
                         const std::string& listen_ip, 
                         uint16_t listen_port, 
                         bool reuse_addr,
                         bool reuse_port)
{
    Close();

    assert(tcp_stack);
    if (listen_ip.empty() || std::string("0.0.0.0") == listen_ip)
    {
        listen_ip_ = tcp_stack->message_ip();
    }
    else
    {
        listen_ip_ = listen_ip;
    }

    listen_port_ = listen_port;

    struct sockaddr_in listen_addr;
    bzero(&listen_addr, sizeof(struct sockaddr_in));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(listen_port);
    listen_addr.sin_addr.s_addr = listen_ip_.empty() 
                                ? INADDR_ANY 
                                : inet_addr(listen_ip_.c_str());

    struct zf_attr* zftl_attr;
    if (ADK_UNLIKELY(s_zf_verbs.zf_attr_alloc(&zftl_attr) < 0))
    {
        return false;
    }

    TRACE_PRINT("@> zf_attr_alloc([out]" << (void*)zftl_attr << ")");

    const auto& nic_name = ((TcpStackZf*)tcp_stack)->nic_name();
    if (!nic_name.empty())
    {
        s_zf_verbs.zf_attr_set_str(zftl_attr, "interface", nic_name.c_str());
        TRACE_PRINT("@> zf_attr_set_str(" << (void*)zftl_attr 
                    << ", \"interface\", " << nic_name << ")");
    }

    OnExit<> on_exit([zftl_attr]() {
        s_zf_verbs.zf_attr_free(zftl_attr);
        TRACE_PRINT("@> zf_attr_free(" << (void*)zftl_attr << ")");
    });

    AsyncTaskZf async_task;
    async_task.done = false;
    async_task.task_executor = [&]() -> int {
        return s_zf_verbs.zftl_listen(((TcpStackZf*)tcp_stack)->stack(),
                                      (struct sockaddr*)&listen_addr,
                                      sizeof(struct sockaddr_in),
                                      zftl_attr,
                                      &zftl_);
    };

    assert(tcp_stack);
    const auto listen_res = ((TcpStackZf*)tcp_stack)->DoSyncTask(&async_task);
    if (ADK_UNLIKELY(listen_res < 0))
    {
        ZF_ERROR("zftl_listen(" << listen_ip_<< ", " << listen_port_ << ")", listen_res);
        return false;
    }

    TRACE_PRINT("@> zftl_listen(" << (void*)((TcpStackZf*)tcp_stack)->stack()
                << ", <AF_INET|" << listen_ip_ << ":" << listen_port_ << ">, "
                << sizeof(struct sockaddr_in) << ", " << (void*)zftl_attr 
                <<  ", [out]" << (void*)zftl_ << ")");

    tcp_stack_ = (TcpStackZf*)tcp_stack;

    waitable_ = s_zf_verbs.zftl_to_waitable(zftl_);
    TRACE_PRINT("@> zftl_to_waitable(" << (void*)zftl_ << ") return " << (void*)waitable_);

    return true;
}

void TcpAcceptorZf::Close()
{
    if (nullptr != zftl_)
    {
        s_zf_verbs.zftl_free(zftl_);
        zftl_ = nullptr;
    }
}

ITcpEndpoint* TcpAcceptorZf::Accept()
{
    struct zft* ts_out;
    AsyncTaskZf async_task;
    async_task.done = false;
    async_task.task_executor = [&]() -> int {
        return s_zf_verbs.zftl_accept(zftl_, &ts_out);
    };

    assert(tcp_stack_);
    const auto accept_res = tcp_stack_->DoSyncTask(&async_task);
    if (ADK_UNLIKELY(accept_res < 0))
    {
        ZF_ERROR("zftl_accept", accept_res);
        return nullptr;
    }

    TRACE_PRINT("@> zftl_accept(" << (void*)zftl_ 
                << ", [out]" << (void*)ts_out << ")");

    TcpEndpointZf* endpoint = new TcpEndpointZf;
    endpoint->zft_ = ts_out;
    endpoint->tcp_stack_ = tcp_stack_;
    endpoint->waitable_ = s_zf_verbs.zft_to_waitable(ts_out);
    TRACE_PRINT("@> zft_to_waitable(" << (void*)ts_out 
                << ") return " << (void*)endpoint->waitable_);

    struct sockaddr_in local_addr;
    socklen_t local_addr_len = sizeof(struct sockaddr_in);
    struct sockaddr_in remote_addr;
    socklen_t remote_addr_len = sizeof(struct sockaddr_in);

    s_zf_verbs.zft_getname(ts_out,
                           (struct sockaddr*)&local_addr, 
                           &local_addr_len, 
                           (struct sockaddr*)&remote_addr, 
                           &remote_addr_len);

    if (sizeof(struct sockaddr_in) == local_addr_len)
    {
        endpoint->local_ip_ = inet_ntoa(local_addr.sin_addr);
        endpoint->local_port_ = ntohs(local_addr.sin_port);
    }

    if (sizeof(struct sockaddr_in) == remote_addr_len)
    {
        endpoint->remote_ip_ = inet_ntoa(remote_addr.sin_addr);;
        endpoint->remote_port_ = ntohs(remote_addr.sin_port);;
    }

    std::lock_guard<std::mutex> _(s_reuse_ids_lock);
    if (s_reuse_ids_deq.size() > 0)
    {
        endpoint->endpoint_id_ = s_reuse_ids_deq.front();
        s_reuse_ids_deq.pop_front();
    }
    else
    {
        endpoint->endpoint_id_ = ++s_ids_register;
    }

    return endpoint;
}

TcpEndpointZf::TcpEndpointZf()
{
    zf_verbs_ = TcpStackZf::GetZfVerbs();
    zft_attr_ = nullptr;
    tcp_stack_ = nullptr;
    bzero(&local_addr_, sizeof(struct sockaddr_in));

    zft_ = nullptr;
    waitable_ = nullptr;
}

TcpEndpointZf::~TcpEndpointZf()
{
    if (default_value::kInvalidFd != endpoint_id_)
    {
        std::lock_guard<std::mutex> _(s_reuse_ids_lock);
        s_reuse_ids_deq.push_back(endpoint_id_);
        endpoint_id_ = default_value::kInvalidFd;
    }
}

bool TcpEndpointZf::SetOption(OptionType option_type, int32_t option_value)
{
    return true;
}

bool TcpEndpointZf::Open(ITcpStack* tcp_stack, bool reuse_addr, bool reuse_port)
{
    assert(tcp_stack);
    tcp_stack_ = (TcpStackZf*)tcp_stack;

    if (ADK_UNLIKELY(s_zf_verbs.zf_attr_alloc(&zft_attr_) < 0))
    {
        return false;
    }

    TRACE_PRINT("@> zf_attr_alloc([out]" << (void*)zft_attr_ << ")");

    s_zf_verbs.zf_attr_set_int(zft_attr_, "tcp_syn_retries", 1);
    TRACE_PRINT("@> zf_attr_set_int(" << (void*)zft_attr_ << ", \"tcp_syn_retries\", 1)");

    const auto& nic_name = ((TcpStackZf*)tcp_stack)->nic_name();
    if (!nic_name.empty())
    {
        s_zf_verbs.zf_attr_set_str(zft_attr_, "interface", nic_name.c_str());
        TRACE_PRINT("@> zf_attr_set_str(" << (void*)zft_attr_ 
                    << ", \"interface\", " << nic_name << ")");
    }

    std::lock_guard<std::mutex> _(s_reuse_ids_lock);
    if (s_reuse_ids_deq.size() > 0)
    {
        endpoint_id_ = s_reuse_ids_deq.front();
        s_reuse_ids_deq.pop_front();
    }
    else
    {
        endpoint_id_ = ++s_ids_register;
    }

    return true;
}

void TcpEndpointZf::Close()
{
    if (nullptr != zft_attr_)
    {
        s_zf_verbs.zf_attr_free(zft_attr_);
        TRACE_PRINT("@> zf_attr_free(" << (void*)zft_attr_ << ")");
        zft_attr_ = nullptr;
    }

    AsyncTaskZf async_task;
    async_task.done = false;
    async_task.task_executor = [&]() -> int {
        if (nullptr != zft_)
        {
            s_zf_verbs.zft_free(zft_);
            TRACE_PRINT("@> zft_free(" << (void*)zft_ << ")");
            zft_ = nullptr;
        }

        return 0;
    };

    assert(tcp_stack_);
    tcp_stack_->DoSyncTask(&async_task);
}

int32_t TcpEndpointZf::Bind(uint16_t local_port)
{
    local_port_ = local_port;

    local_addr_.sin_family = AF_INET;
    local_addr_.sin_port = htons(local_port);
    local_addr_.sin_addr.s_addr = local_ip_.empty() 
                                ? INADDR_ANY 
                                : inet_addr(local_ip_.c_str());

    return static_cast<int32_t>(BindResult::kSuccess);
}

int32_t TcpEndpointZf::Connect(const std::string& remote_ip, uint16_t remote_port)
{
    remote_ip_ = remote_ip;
    remote_port_ = remote_port;

    AsyncTaskZf async_task;
    async_task.done = false;
    async_task.task_executor = [&]() -> int {
        if (nullptr != zft_)
        {
            s_zf_verbs.zft_free(zft_);
            TRACE_PRINT("@> zft_free(" << (void*)zft_ << ")");
            zft_ = nullptr;
        }

        struct zft_handle* tcp_handle;

        assert(zft_attr_);
        assert(tcp_stack_);
        if (ADK_UNLIKELY(s_zf_verbs.zft_alloc(tcp_stack_->stack(), zft_attr_, &tcp_handle) < 0))
        {
            return static_cast<int32_t>(ConnectResult::kFailure);
        }

        assert(tcp_handle);
        TRACE_PRINT("@> zft_alloc(" << (void*)tcp_stack_->stack() 
                    << ", " << (void*)zft_attr_ << ", [out]" 
                    << (void*)tcp_handle << ")");

        if ((AF_INET == local_addr_.sin_family) && (0 != local_addr_.sin_port))
        {
            const auto bind_res = s_zf_verbs.zft_addr_bind(tcp_handle,
                                                        (struct sockaddr*)&local_addr_,
                                                        sizeof(struct sockaddr_in),
                                                        0);

            if (ADK_UNLIKELY(bind_res < 0))
            {
                ZF_ERROR("zft_addr_bind", bind_res);

                s_zf_verbs.zft_handle_free(tcp_handle);
                TRACE_PRINT("@> zft_handle_free(" << (void*)tcp_handle << ")");
                return static_cast<int32_t>(ConnectResult::kFailure);
            }

            TRACE_PRINT("@> zft_addr_bind(" << (void*)tcp_handle
                        << ", <AF_INET|" << inet_ntoa(local_addr_.sin_addr)
                        << ":" << ntohs(local_addr_.sin_port) << ">, "
                        << sizeof(struct sockaddr_in) << ", 0)");
        }

        struct sockaddr_in remote_addr;
        remote_addr.sin_family = AF_INET;
        remote_addr.sin_addr.s_addr = inet_addr(remote_ip.c_str());
        remote_addr.sin_port = htons(remote_port);
        const auto connect_res = s_zf_verbs.zft_connect(tcp_handle,
                                                        (struct sockaddr*)&remote_addr, 
                                                        sizeof(struct sockaddr_in), 
                                                        &zft_);

        if (ADK_UNLIKELY(connect_res < 0 || zft_ == nullptr))
        {
            ZF_ERROR("zft_connect <" << remote_ip << ":" << remote_port << ">", connect_res);
            s_zf_verbs.zft_handle_free(tcp_handle);
            return static_cast<int32_t>(ConnectResult::kFailure);
        }

        TRACE_PRINT("@> zft_connect(" << (void*)tcp_handle 
                    << ", <AF_INET|" << remote_ip
                    << ":" << remote_port << ">, "
                    << sizeof(struct sockaddr_in) << ", [out]" 
                    << (void*)zft_ << ")");

        return static_cast<int32_t>(ConnectResult::kInProgress);
    };

    assert(tcp_stack_);
    const auto task_result = tcp_stack_->DoSyncTask(&async_task);
    if (ADK_UNLIKELY(static_cast<int32_t>(ConnectResult::kFailure) == task_result))
    {
        return task_result;
    }

    assert(zft_);
    waitable_ = s_zf_verbs.zft_to_waitable(zft_);
    TRACE_PRINT("@> zft_to_waitable(" << (void*)zft_ 
                << ") return " << (void*)waitable_);

    struct sockaddr_in sa_local_addr;
    struct sockaddr_in sa_remote_addr;
    socklen_t local_addr_len = sizeof(struct sockaddr_in);
    socklen_t remote_addr_len = sizeof(struct sockaddr_in);

    s_zf_verbs.zft_getname(zft_,
                           (struct sockaddr*)&sa_local_addr,
                           &local_addr_len,
                           (struct sockaddr*)&sa_remote_addr,
                           &remote_addr_len);

    if (sizeof(struct sockaddr_in) == local_addr_len)
    {
        local_ip_ = inet_ntoa(sa_local_addr.sin_addr);
        local_port_ = ntohs(sa_local_addr.sin_port);
    }

    if (sizeof(struct sockaddr_in) == remote_addr_len)
    {
        assert(remote_ip_ == inet_ntoa(sa_remote_addr.sin_addr));
        assert(remote_port_ == ntohs(sa_remote_addr.sin_port));
    }

    return static_cast<int32_t>(ConnectResult::kInProgress);

    struct zf_muxer_set* temp_muxer;
    if (ADK_UNLIKELY(s_zf_verbs.zf_muxer_alloc(tcp_stack_->stack(), &temp_muxer) < 0))
    {
        return static_cast<int32_t>(ConnectResult::kFailure);
    }

    OnExit<> muxer_exit([temp_muxer]() {
        s_zf_verbs.zf_muxer_free(temp_muxer);
    });

    struct epoll_event events;
    events.events = EPOLLOUT | EPOLLHUP | EPOLLRDHUP | EPOLLERR;
    if (ADK_UNLIKELY(s_zf_verbs.zf_muxer_add(temp_muxer, waitable_, &events) < 0))
    {
        return static_cast<int32_t>(ConnectResult::kFailure);
    }

    const auto muxer_wait_res = s_zf_verbs.zf_muxer_wait(temp_muxer, &events, 1, 1000000);

    s_zf_verbs.zf_muxer_del(waitable_);

    if ((1 == muxer_wait_res) 
        && !(events.events & EPOLLERR) 
        && (events.events & EPOLLOUT))
    {
        const auto tcp_state = s_zf_verbs.zft_state(zft_);
        TRACE_PRINT("@> zft_state(" << (void*)zft_ << ") return " << tcp_state);

        if (TCP_ESTABLISHED == tcp_state)
        {
            return static_cast<int32_t>(ConnectResult::kSuccess);
        }
    }

    return static_cast<int32_t>(ConnectResult::kInProgress);
}

bool TcpEndpointZf::EPollResult(uint32_t events)
{
    assert(zft_);
    const auto tcp_state = s_zf_verbs.zft_state(zft_);
    TRACE_PRINT("@> zft_state(" << (void*)zft_ << ") return " << tcp_state);

    if (TCP_ESTABLISHED == tcp_state)
    {
        return true;
    }

    if (!(events & EPOLLERR) && (events & EPOLLOUT))
    {
        return true;
    }

    return false;
}

std::string TcpEndpointZf::LastError() const
{
    const auto zft_errno = s_zf_verbs.zft_error(zft_);
    if (0 != zft_errno)
    {
        return strerror(zft_errno);
    }

    return std::string();
}

}

}

#endif
