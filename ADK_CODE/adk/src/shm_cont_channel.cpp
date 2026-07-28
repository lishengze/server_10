#include <adk/shm.h>
#include <adk/shm_cont_channel.h>

#include <boost/locale.hpp>

#include <pwd.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/un.h>
#include <sys/prctl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <sys/syscall.h>
#define gettid() syscall(__NR_gettid)

#ifdef NDEBUG
#define LOG_DEBUG(...)
#else
#include <iostream>
#define LOG_DEBUG(...)                              \
{                                                   \
    std::cout << DoFormat(__VA_ARGS__) << std::endl;\
}
#endif

#define NameThread(...)                             \
    prctl(PR_SET_NAME, DoFormat(__VA_ARGS__).c_str(), 0, 0)

namespace adk_impl
{

namespace sccl
{

inline bool CheckError()
{
    return (EAGAIN == errno) || (EWOULDBLOCK == errno) || (ERESTART == errno);
}

inline std::string DoFormat(boost::locale::format& formater)
{
    return formater.str();
}

template<typename Arg>
inline std::string DoFormat(boost::locale::format& formater, Arg&& arg)
{
    return (formater % std::forward<Arg>(arg)).str();
}

template<typename Arg, typename... Args>
inline std::string DoFormat(boost::locale::format& formater, Arg&& arg, Args&&... args)
{
    return DoFormat(formater % std::forward<Arg>(arg), std::forward<Args>(args)...);
}

template<typename... Args>
inline std::string DoFormat(const std::string& message, Args&&... args)
{
    boost::locale::format formater(message);
    return DoFormat(formater, std::forward<Args>(args)...);
}

constexpr uint32_t Index2Offset(uint32_t index)
{
    return sizeof(struct ShmContHeader) + index * ADK_CACHE_LINE_SIZE;
}

constexpr uint32_t kMemoryOffset = Index2Offset(kMaxProxySize);

class EventHandlerBlank final : public AgentEventHandler
{
public:
    bool OnNewProxy(const std::string& process, int32_t pid, int32_t tid) override
    {
        LOG_DEBUG("OnNewProxy({1}, {2}, {3})", process, pid, tid);
        return true;
    }

    void OnProxyBroken(const std::string& process, int32_t pid, int32_t tid) override
    {
        LOG_DEBUG("OnProxyBroken({1}, {2}, {3})", process, pid, tid);
    }
};

static std::mutex s_wal_lock;
static EventHandlerBlank s_event_handler;

namespace msg
{

enum Type : int32_t
{
    kNotifyProxyInfo = 0,
    kRequestAllocWAL = 10,
    kRequestFreeWAL,
    kResultAllocWAL = 20
};

constexpr int32_t kUnknown = -1987;
constexpr size_t kMaxMessageSize = 256;

struct Header
{
    using LenType = int32_t;

    LenType  length;
    Type     type;

    static constexpr size_t MinSize()
    {
        return sizeof(LenType);
    }
};

struct NotifyProxyInfo : Header
{
    int32_t pid;

    NotifyProxyInfo(int32_t proxy_pid, const std::string& proxy_name)
    {
        length = NotifyProxyInfo::Size() + proxy_name.length();
        type = Type::kNotifyProxyInfo;
        pid = proxy_pid;
    }

    const char* Name() const
    {
        return reinterpret_cast<const char*>(this) + NotifyProxyInfo::Size();
    }

    int32_t NameLength() const
    {
        assert(static_cast<size_t>(length) >= NotifyProxyInfo::Size());
        return length - NotifyProxyInfo::Size();
    }

    static constexpr size_t Size()
    {
        return sizeof(struct NotifyProxyInfo);
    }
};

struct RequestAllocWAL : Header
{
    int32_t tid;
    void*   context;
    RequestAllocWAL()
    {
        length = RequestAllocWAL::Size();
        type = Type::kRequestAllocWAL;
        tid = gettid();
    }

    static constexpr size_t Size()
    {
        return sizeof(struct RequestAllocWAL);
    }
};

struct RequestFreeWAL : Header
{
    uint16_t index;

    RequestFreeWAL(uint16_t idx)
    {
        length = RequestFreeWAL::Size();
        type = Type::kRequestFreeWAL;
        index = idx;
    }

    static constexpr size_t Size()
    {
        return sizeof(struct RequestFreeWAL);
    }
};

struct ResultAllocWAL : Header
{
    uint32_t offset;
    void*    context;

    ResultAllocWAL()
    {
        length = ResultAllocWAL::Size();
        type = Type::kResultAllocWAL;
    }

    static constexpr size_t Size()
    {
        return sizeof(struct ResultAllocWAL);
    }
};

}

constexpr uint32_t kLazyMicro = 1000;
thread_local struct WAL* Proxy::wal_ = nullptr;

inline std::string MakeChannelName(const std::string& name)
{
    struct passwd* pw = getpwuid(geteuid());
    if (nullptr == pw)
    {
        return DoFormat("_{1}_shm_c_chnl", name);
    }

    return DoFormat("{1}_{2}_shm_c_chnl", pw->pw_name, name);
}

inline std::string MakeUnixPath(const std::string& channel_name)
{
    struct passwd* pw = getpwuid(geteuid());
    if (nullptr == pw)
    {
        return std::string(".") + channel_name;
    }

    if (std::string(pw->pw_name) == "root")
    {
        return std::string("/root/.") + channel_name;
    }

    return DoFormat("/home/{1}/.{2}", pw->pw_name, channel_name);
}

class Registry
{
public:
    Registry()
    {
        proxy_ = nullptr;
    }

    ~Registry()
    {
        if (nullptr != Proxy::wal_)
        {
            assert(proxy_);
            proxy_->DestroyWAL();
        }
    }

    void Register(Proxy* proxy)
    {
        proxy_ = proxy;
    }

private:
    Proxy* proxy_;
};

thread_local Registry tls_registry;

struct IpcResultAllocWAL
{
    int32_t offset;
};

Agent* Agent::Create(const std::string& name, 
                     AgentEventHandler* event_handler, 
                     bool do_recovery,
                     uint32_t memory_size, 
                     uint32_t max_message_size)
{
    auto* const agent = aligned_malloc(ADK_CACHE_LINE_SIZE, sizeof(Agent));
    if (nullptr != agent)
    {
        new (agent) Agent(name, event_handler);
        if (reinterpret_cast<Agent*>(agent)->Init(do_recovery, memory_size, max_message_size))
        {
            signal(SIGPIPE, SIG_IGN);
            return reinterpret_cast<Agent*>(agent);
        }

        reinterpret_cast<Agent*>(agent)->~Agent();
        aligned_free(agent);
    }

    return nullptr;
}

void Agent::Destroy(Agent* agent, bool do_shm_destroy)
{
    if (!do_shm_destroy)
    {
        agent->shm_header_ = nullptr;
    }

    agent->~Agent();
    aligned_free(agent);
}

Agent::Agent(const std::string& name, AgentEventHandler* event_handler)
{
    shm_header_ = nullptr;
    shm_entries_ = nullptr;

    is_running_ = false;
    dmsk_fd_ = -1;
    epoll_fd_ = -1;
    name_ = MakeChannelName(name);
    singleton_process_ = nullptr;
    event_handler_ = event_handler;
    if (nullptr == event_handler_)
    {
        event_handler_ = &s_event_handler;
    }
}

Agent::~Agent()
{
    if (nullptr != shm_header_)
    {
        ShmFactory::Destroy(name_);
        shm_header_ = nullptr;
    }

    if (actor_thrd_.joinable())
    {
        is_running_ = false;
        actor_thrd_.join();
    }

    if (-1 != dmsk_fd_)
    {
        close(dmsk_fd_);
        dmsk_fd_ = -1;
    }

    if (-1 != epoll_fd_)
    {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }

    if (nullptr != singleton_process_)
    {
        delete singleton_process_;
        singleton_process_ = nullptr;
    }
}

bool Agent::Init(bool do_recovery, uint32_t memory_size, uint32_t max_message_size)
{
    const auto kMaxLength = Entry::kMaxLength;
    if (max_message_size >= kMaxLength)
    {
        LOG_DEBUG("Agent({1}) max message size {2} is larger than support {3}", 
                  name_,
                  max_message_size,
                  kMaxLength);
        return false;
    }

    const auto unix_path = MakeUnixPath(name_);
    singleton_process_ = new SingletonProcess(unix_path);
    if (ErrorCode::kSuccess != singleton_process_->Lock())
    {
        LOG_DEBUG("Agent({1}) share memory cont-channel {2} already in use", name_, unix_path);
        return false;
    }

    unlink(unix_path.c_str());

    memory_size = ADK_ROUND_TO_POWER_OF_2(memory_size);
    
    assert(nullptr == shm_header_);
    if (do_recovery)
    {
        shm_header_ = reinterpret_cast<ShmContHeader*>(ShmFactory::Attach(name_));
    }

    if ((nullptr != shm_header_) 
        && (memory_size - 1 == shm_header_->memory_mask_c) 
        && (max_message_size == shm_header_->reserve_size_c))
    {
        LOG_DEBUG("Agent({1}) recovery begin", name_);

        const auto rollback_head = shm_header_->head & (memory_size - 1);
        shm_entries_ = reinterpret_cast<char*>(shm_header_) + kMemoryOffset;
        if (IsMixed(shm_header_->tail))
        {
            shm_header_->tail = Restore(shm_header_->tail);
            LOG_DEBUG("Agent({1}) recovery Tail({2})", name_, shm_header_->tail);
        }

        for (uint16_t index = 0; index < kMaxProxySize; ++index)
        {
            auto* const wal = reinterpret_cast<WAL*>(reinterpret_cast<char*>(shm_header_) + Index2Offset(index));
            assert(wal->index == index + 1);
            assert(wal->size_limit == max_message_size);
            assert(wal->memory_size == memory_size);
            assert(wal->memory_mask == memory_size - 1);

            assert(wal->cursor <= shm_header_->tail);
            if (ADK_UNLIKELY(wal->cursor >= shm_header_->head))
            {
                auto* const entry = GetEntry(wal->cursor);
                if (ADK_UNLIKELY(entry->IsAcquired(index)))
                {
                    entry->Release();
                    LOG_DEBUG("Agent({1}) recovery Entry({2}) with Index({3})", name_, wal->cursor, index);
                }
            }

            wal->cursor = 0;
            wal->threshold = rollback_head + memory_size;
        }

        assert(shm_header_->tail >= shm_header_->head);
        const auto rollback_distance = shm_header_->head - rollback_head;
        const auto rollback_tail = shm_header_->tail - rollback_distance;

        shm_header_->head = rollback_head;
        shm_header_->head_threshold = rollback_tail;
        shm_header_->tail = rollback_tail;

        LOG_DEBUG("Agent({1}) recovery success, rollback distance {2}", name_, rollback_distance);
    }
    else
    {
        const auto total_memory_size = kMemoryOffset + memory_size + max_message_size;
        shm_header_ = reinterpret_cast<ShmContHeader*>(ShmFactory::Create(name_, total_memory_size));
        if (ADK_UNLIKELY(nullptr == shm_header_))
        {
            ShmFactory::Destroy(name_);

            LOG_DEBUG("Agent({1}) create share memory failed, destroy and try again", name_);
            shm_header_ = reinterpret_cast<ShmContHeader*>(ShmFactory::Create(name_, total_memory_size));
            if ADK_UNLIKELY((nullptr == shm_header_))
            {
                LOG_DEBUG("Agent({1}) create share memory failed", name_);
                return false;
            }
        }

        shm_entries_ = reinterpret_cast<char*>(shm_header_) + kMemoryOffset;

        shm_header_->head = 0;
        shm_header_->head_threshold = 0;

        shm_header_->memory_mask_c = memory_size - 1;
        shm_header_->reserve_size_c = max_message_size;

        shm_header_->tail = 0;

        for (uint16_t index = 0; index < kMaxProxySize; ++index)
        {
            auto* const wal = reinterpret_cast<WAL*>(reinterpret_cast<char*>(shm_header_) + Index2Offset(index));
            wal->index = index + 1;
            wal->size_limit = max_message_size;
            wal->memory_size = memory_size;
            wal->memory_mask = memory_size - 1;

            wal->cursor = 0;
            wal->threshold = shm_header_->head + memory_size;
        }
    }

    dmsk_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (-1 == dmsk_fd_)
    {
        return false;
    }

    struct sockaddr_un sa_unix;
    sa_unix.sun_family = AF_UNIX;
    strncpy(sa_unix.sun_path, unix_path.c_str(), sizeof(sa_unix.sun_path));
    if (0 != bind(dmsk_fd_, (struct sockaddr*)(&sa_unix), sizeof(struct sockaddr_un)))
    {
        LOG_DEBUG("Agent({1}) bind domain socket {2} failed, {3}", name_, unix_path, strerror(errno));
        return false;
    }

    if (0 != listen(dmsk_fd_, 128))
    {
        LOG_DEBUG("Agent({1}) listen domain socket {2} failed, {3}", name_, unix_path, strerror(errno));
        return false;
    }

    fcntl(dmsk_fd_, F_SETFL, fcntl(dmsk_fd_, F_GETFL) | O_NONBLOCK);

    epoll_fd_ = epoll_create1(0);
    if (-1 == epoll_fd_)
    {
        return false;
    }

    struct epoll_event add_event;
    add_event.events = EPOLLIN;
    add_event.data.fd = dmsk_fd_;
    if (0 != epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, dmsk_fd_, &add_event))
    {
        return false;
    }

    is_running_ = true;
    actor_thrd_ = std::thread([&]() {
        NameThread("Agent_{1}", name_);

        constexpr auto kMaxEvents = 64;
        constexpr auto kTimeoutMs = 100;
        struct epoll_event events[kMaxEvents];

        int32_t data_len = 0;
        char buffer[msg::kMaxMessageSize] = { 0 };

        do 
        {
            const auto event_nr = epoll_wait(epoll_fd_, events, kMaxEvents, kTimeoutMs);
            for (auto index = 0; index < event_nr; ++index)
            {
                const auto& event = events[index];
                if (dmsk_fd_ == event.data.fd)
                {
                    const auto fd = accept(dmsk_fd_, nullptr, nullptr);
                    if (-1 != fd)
                    {
                        AllocProxy(fd);
                        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
                    }
                    else
                    {
                        assert(CheckError());
                    }
                }
                else if (event.events & EPOLLERR)
                {
                    FreeProxy(event.data.fd);
                }
                else
                {
                retry_read:
                    const auto result = recv(event.data.fd, buffer + data_len, msg::kMaxMessageSize - data_len, 0);
                    if (result > 0)
                    {
                        data_len += result;
                        auto* const temp_buffer = &buffer[0];
                        LOG_DEBUG("Agent({1}) data length = {2}", name_, data_len);
                        while ((static_cast<size_t>(data_len) >= msg::Header::MinSize())
                            && (data_len >= reinterpret_cast<msg::Header*>(temp_buffer)->length))
                        {
                            LOG_DEBUG("Agent({1}) length = {2}, type = {3}",
                                      name_,
                                      reinterpret_cast<msg::Header*>(temp_buffer)->length,
                                      reinterpret_cast<msg::Header*>(temp_buffer)->type);
                            OnMessage(event.data.fd, temp_buffer);

                            data_len -= reinterpret_cast<msg::Header*>(temp_buffer)->length;
                            if (ADK_UNLIKELY(data_len > 0))
                            {
                                memmove(temp_buffer,
                                        temp_buffer + reinterpret_cast<msg::Header*>(temp_buffer)->length,
                                        data_len);
                            }
                        }
                        goto retry_read;
                    }
                    else if (result < 0)
                    {
                        if (ADK_UNLIKELY(!CheckError()))
                        {
                            LOG_DEBUG("Agent({1}) domain socket error {2}", name_, strerror(errno));

                            data_len = 0;
                            FreeProxy(event.data.fd);
                        }
                        else if (ADK_UNLIKELY(data_len > 0))
                        {
                            goto retry_read;
                        }
                    }
                    else
                    {
                        LOG_DEBUG("Agent({1}) end of stream", name_);

                        data_len = 0;
                        FreeProxy(event.data.fd);
                    }
                }
            }
        } while (ACCESS_ONCE(is_running_));
    });
    return true;
}

uint16_t Agent::AllocWAL()
{
    return static_cast<uint16_t>(index_allocator_.Allocate() + 1);
}

void Agent::FreeWAL(uint16_t index)
{
    index_allocator_.Free(index - 1);
    auto* const wal = reinterpret_cast<WAL*>(reinterpret_cast<char*>(shm_header_) + Index2Offset(index - 1));
    assert(index == wal->index);

    const auto current_tail = ACCESS_ONCE(shm_header_->tail);
    if (index == Unmixed(current_tail))
    {
        ADK_BARRIER();
        shm_header_->tail = Restore(current_tail);
        LOG_DEBUG("Agent({1}) FreeIndex({2}) Release and Restore", name_, index);
    }
    else
    {
        assert(wal->cursor <= current_tail);
        if (wal->cursor >= ACCESS_ONCE(shm_header_->head))
        {
            auto* const entry = GetEntry(wal->cursor);
            if (ADK_UNLIKELY(entry->IsAcquired(index)))
            {
                entry->Release();
                LOG_DEBUG("Agent({1}) FreeIndex({2}) Release", name_, index);
            }
        }
        else
        {
            LOG_DEBUG("Agent({1}) FreeIndex({2})", name_, index);
        }
    }
}

void Agent::AllocProxy(int32_t fd)
{
    LOG_DEBUG("Agent({1}) AllocProxy({2})", name_, fd);

    assert(proxy_map_.end() == proxy_map_.find(fd));
    auto& proxy_info = proxy_map_[fd];
    proxy_info.pid = 0;

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = fd;
    ADK_UNUSED const auto ec = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event);
    assert(-1 != ec);
}

void Agent::FreeProxy(int32_t fd)
{
    LOG_DEBUG("Agent({1}) FreeProxy({2})", name_, fd);

    const auto iter = proxy_map_.find(fd);
    if (proxy_map_.end() != iter)
    {
        assert(event_handler_);
        for (const auto& proxy : iter->second.index2tid_map)
        {
            FreeWAL(proxy.first);
            event_handler_->OnProxyBroken(iter->second.name, iter->second.pid, proxy.second);
        }

        proxy_map_.erase(iter);
    }
    else
    {
        assert(false);
    }

    ADK_UNUSED const auto ec = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    assert(-1 != ec);
}

void Agent::OnMessage(int32_t fd, void* buffer)
{
    ADK_UNUSED
    const auto length = static_cast<size_t>(reinterpret_cast<msg::Header*>(buffer)->length);
    switch (reinterpret_cast<msg::Header*>(buffer)->type)
    {
    case msg::Type::kNotifyProxyInfo:
        assert(length >= msg::NotifyProxyInfo::Size());
        assert(proxy_map_.end() != proxy_map_.find(fd));
        {
            auto* const message = reinterpret_cast<msg::NotifyProxyInfo*>(buffer);
            auto& proxy_info = proxy_map_[fd];
            assert(0 == proxy_info.pid);
            assert(0 == proxy_info.index2tid_map.size());
            proxy_info.pid = message->pid;
            if (message->NameLength() > 0)
            {
                proxy_info.name = std::string(message->Name(), message->NameLength());
            }

            LOG_DEBUG("Agent({1}) new proxy fd = {2}, pid = {3} name = {4}", 
                      name_, 
                      fd, 
                      proxy_info.pid, 
                      proxy_info.name);
        }
        break;
    case msg::Type::kRequestAllocWAL:
        assert(msg::RequestAllocWAL::Size() == length);
        assert(proxy_map_.end() != proxy_map_.find(fd));
        {
            auto* const message = reinterpret_cast<msg::RequestAllocWAL*>(buffer);

            auto& proxy_info = proxy_map_[fd];
            assert(0 != proxy_info.pid);

            msg::ResultAllocWAL result;
            result.context = message->context;

            assert(event_handler_);
            if (event_handler_->OnNewProxy(proxy_info.name, proxy_info.pid, message->tid))
            {
                const auto wal_index = AllocWAL();
                if (wal_index > 0)
                {
                    result.offset = Index2Offset(wal_index - 1);
                    if (msg::ResultAllocWAL::Size() == send(fd, &result, msg::ResultAllocWAL::Size(), 0))
                    {
                        assert(proxy_info.index2tid_map.end() == proxy_info.index2tid_map.find(wal_index));
                        proxy_info.index2tid_map[wal_index] = message->tid;
                        return;
                    }
                    else
                    {
                        FreeWAL(wal_index);
                        event_handler_->OnProxyBroken(proxy_info.name, proxy_info.pid, message->tid);
                    }
                }
            }

            result.offset = 0;
            send(fd, &result, msg::ResultAllocWAL::Size(), 0);
        }
        break;
    case msg::Type::kRequestFreeWAL:
        assert(msg::RequestFreeWAL::Size() == length);
        assert(proxy_map_.end() != proxy_map_.find(fd));
        {
            auto& proxy_info = proxy_map_[fd];
            auto* const message = reinterpret_cast<msg::RequestFreeWAL*>(buffer);
            const auto iter = proxy_info.index2tid_map.find(message->index);
            if (proxy_info.index2tid_map.end() != iter)
            {
                FreeWAL(message->index);

                assert(event_handler_);
                event_handler_->OnProxyBroken(proxy_info.name, proxy_info.pid, iter->second);
                proxy_info.index2tid_map.erase(iter);
            }
            else
            {
                assert(false);
            }
        }
        break;
    default:
        assert(false);
    }
}

void OnMessageProxy(void* buffer)
{
    const auto length = reinterpret_cast<msg::Header*>(buffer)->length;
    switch (reinterpret_cast<msg::Header*>(buffer)->type)
    {
    case msg::Type::kResultAllocWAL:
        if (msg::ResultAllocWAL::Size() == length)
        {
            auto* const result = reinterpret_cast<msg::ResultAllocWAL*>(buffer);
            assert(result->context);

            ADK_BARRIER();
            reinterpret_cast<IpcResultAllocWAL*>(result->context)->offset = result->offset;
        }
        else
        {
            assert(false);
        }
        break;
    default:
        assert(false);
    }
}

Proxy::Proxy(const std::string& agent_name, 
             const std::string& proxy_name, 
             const std::function<bool()>& broken_cb)
{
    shm_header_ = nullptr;
    shm_entries_ = nullptr;
    status_valid_ = false;

    is_running_ = false;
    dmsk_fd_ = -1;

    agent_name_ = MakeChannelName(agent_name);
    if (proxy_name.empty())
    {
        proxy_name_ = program_invocation_name;
    }
    else
    {
        proxy_name_ = proxy_name;
    }

    broken_cb_ = broken_cb;
}

Proxy::~Proxy()
{
    is_running_ = false;
    if (ipc_actor_.joinable())
    {
        ipc_actor_.join();
    }

    if (-1 != dmsk_fd_)
    {
        close(dmsk_fd_);
        dmsk_fd_ = -1;
    }

    if (nullptr != shm_header_)
    {
        ShmFactory::Detach(agent_name_);
        shm_header_ = nullptr;
    }
}

bool Proxy::Init()
{
    if (ADK_UNLIKELY(!DoConnect()))
    {
        return false;
    }

    shm_header_ = reinterpret_cast<struct ShmContHeader*>(ShmFactory::Attach(agent_name_));
    if (nullptr == shm_header_)
    {
        return false;
    }

    shm_entries_ = reinterpret_cast<char*>(shm_header_) + kMemoryOffset;

    is_running_ = true;
    ipc_actor_ = std::thread([&]()
    {
        char buffer[msg::kMaxMessageSize] = { 0 };
        NameThread("Proxy_{1}_{2}", proxy_name_, agent_name_);

    next_connection:
        do
        {
            if (ACCESS_ONCE(status_valid_))
            {
                ADK_BARRIER();
                break;
            }

            usleep(0);
        } while (ACCESS_ONCE(is_running_));

        int32_t data_len = 0;
        const auto dmsk_fd = ACCESS_ONCE(dmsk_fd_);
        while (ACCESS_ONCE(is_running_))
        {
            const auto result = recv(dmsk_fd, buffer + data_len, msg::kMaxMessageSize - data_len, 0);
            if (ADK_UNLIKELY(0 == result))
            {
                LOG_DEBUG("Proxy({1}) end of stream", proxy_name_);
                break;
            }

            if (result < 0)
            {
                if (ADK_UNLIKELY(!CheckError()))
                {
                    LOG_DEBUG("Proxy({1}) domain socket error, {2}", proxy_name_, strerror(errno));
                    break;
                }

                usleep(kLazyMicro);
                continue;
            }

            data_len += result;
            auto* const temp_buffer = &buffer[0];
            LOG_DEBUG("Proxy({1}) data length = {2}", proxy_name_, data_len);
            while ((static_cast<size_t>(data_len) >= msg::Header::MinSize()) 
                   && (data_len >= reinterpret_cast<msg::Header*>(temp_buffer)->length))
            {
                LOG_DEBUG("Proxy({1}) length = {2}, type = {3}",
                          proxy_name_,
                          reinterpret_cast<msg::Header*>(temp_buffer)->length,
                          reinterpret_cast<msg::Header*>(temp_buffer)->type);

                OnMessageProxy(temp_buffer);

                data_len -= reinterpret_cast<msg::Header*>(temp_buffer)->length;
                if (ADK_UNLIKELY(data_len > 0))
                {
                    memmove(temp_buffer, 
                            temp_buffer + reinterpret_cast<msg::Header*>(temp_buffer)->length, 
                            data_len);
                }
            }
        }

        status_valid_ = false;
        {
            std::lock_guard<std::mutex> _(s_wal_lock);
            for (auto* wal_pptr : wal_registry_vec_)
            {
                *reinterpret_cast<void**>(wal_pptr) = nullptr;
            }

            wal_registry_vec_.clear();
        }

        if (ACCESS_ONCE(is_running_))
        {
            if (broken_cb_())
            {
                while (!DoConnect() && ACCESS_ONCE(is_running_))
                {
                    usleep(1000);
                }
            }
            goto next_connection;
        }
    });

    return true;
}

bool Proxy::DoConnect()
{
    if (-1 != dmsk_fd_)
    {
        close(dmsk_fd_);
    }

    assert(!status_valid_);
    dmsk_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ADK_UNLIKELY(-1 == dmsk_fd_))
    {
        return false;
    }

    struct sockaddr_un sa_unix;
    sa_unix.sun_family = AF_UNIX;
    const auto unix_path = MakeUnixPath(agent_name_);
    strncpy(sa_unix.sun_path, unix_path.c_str(), sizeof(sa_unix.sun_path));
    if ((0 != connect(dmsk_fd_, (struct sockaddr*)(&sa_unix), sizeof(struct sockaddr_un))))
    {
        LOG_DEBUG("Proxy({1}) connect to {2} failed, {3}", 
                  proxy_name_, 
                  unix_path, 
                  strerror(errno));
        return false;
    }

    fcntl(dmsk_fd_, F_SETFL, fcntl(dmsk_fd_, F_GETFL) | O_NONBLOCK);

    struct iovec iov_msg[2];
    msg::NotifyProxyInfo notify_info(getpid(), proxy_name_);

    auto& iov_node1 = iov_msg[0];
    iov_node1.iov_base = &notify_info;
    iov_node1.iov_len = msg::NotifyProxyInfo::Size();

    auto& iov_node2 = iov_msg[1];
    iov_node2.iov_base = const_cast<char*>(proxy_name_.c_str());
    iov_node2.iov_len = proxy_name_.size();

    if (ADK_UNLIKELY(notify_info.length != writev(dmsk_fd_, iov_msg, 2)))
    {
        return false;
    }

    ADK_BARRIER();
    status_valid_ = true;
    return true;
}

Proxy* Proxy::Create(const std::string& agent_name, 
                     const std::string& proxy_name, 
                     const std::function<bool()>& broken_cb)
{
    auto* const proxy = aligned_malloc(ADK_CACHE_LINE_SIZE, sizeof(Proxy));
    if (nullptr != proxy)
    {
        new (proxy) Proxy(agent_name, proxy_name, broken_cb);
        if (reinterpret_cast<Proxy*>(proxy)->Init())
        {
            signal(SIGPIPE, SIG_IGN);
            return reinterpret_cast<Proxy*>(proxy);
        }

        reinterpret_cast<Proxy*>(proxy)->~Proxy();
        aligned_free(proxy);
    }

    return nullptr;
}

void Proxy::Destroy(Proxy* proxy)
{
    assert(proxy);
    proxy->~Proxy();
    aligned_free(proxy);
}

WAL* Proxy::CreateWAL()
{
    assert(nullptr == wal_);
    if (ADK_UNLIKELY(!ACCESS_ONCE(status_valid_)))
    {
        return nullptr;
    }

    struct IpcResultAllocWAL ipc_result;
    ipc_result.offset = msg::kUnknown;

    msg::RequestAllocWAL request;
    request.context = &ipc_result;

    {
        std::lock_guard<std::mutex> _(ipc_lock_);
        if (ADK_UNLIKELY(msg::RequestAllocWAL::Size() != send(
            dmsk_fd_, 
            &request, 
            msg::RequestAllocWAL::Size(), 
            0)))
        {
            return nullptr;
        }
    }

    do 
    {
        if (ACCESS_ONCE(ipc_result.offset) > msg::kUnknown)
        {
            break;
        }

        usleep(0);
    } while (ACCESS_ONCE(status_valid_));

    if (ADK_UNLIKELY(ipc_result.offset <= 0))
    {
        return nullptr;
    }

    tls_registry.Register(this);
    wal_ = reinterpret_cast<WAL*>(reinterpret_cast<char*>(shm_header_) + ipc_result.offset);

    {
        std::lock_guard<std::mutex> _(s_wal_lock);
        wal_registry_vec_.push_back(&wal_);
    }
    return wal_;
}

void Proxy::DestroyWAL()
{
    std::lock_guard<std::mutex> _(s_wal_lock);
    if (ADK_UNLIKELY(nullptr == wal_))
    {
        return;
    }

    //> proxy object is valid when (wal_ != nullptr) protect by s_wal_lock
    const auto iter = std::find(wal_registry_vec_.begin(), wal_registry_vec_.end(), &wal_);
    if (wal_registry_vec_.end() != iter)
    {
        wal_registry_vec_.erase(iter);
    }
    else
    {
        assert(false);
    }

    ssize_t result;
    msg::RequestFreeWAL request(wal_->index);
    {
        std::lock_guard<std::mutex> _(ipc_lock_);
        result = send(dmsk_fd_, &request, msg::RequestFreeWAL::Size(), 0);
    }

    if (ADK_UNLIKELY(msg::RequestFreeWAL::Size() != result))
    {
        LOG_DEBUG("Proxy({1}) send request failed, data length = {2}, result = {3}", 
                  proxy_name_, 
                  msg::RequestFreeWAL::Size(), 
                  result);
    }

    wal_ = nullptr;
}

}

}