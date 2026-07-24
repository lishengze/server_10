#include "last_error.h"
#include "tcp_engine_impl.h"

#include <signal.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <adk/io_engine/property.h>
#include <adk/io_engine/tcp_engine.h>

namespace adk_impl
{

namespace io_engine
{

static std::mutex s_tcp_engines_lck_;
static std::set<TcpEngineImpl*> s_tcp_engines_set_;
static std::map<std::string, TcpEngineImpl*> s_tcp_engines_map_;

TcpEngineImpl* CreateTcpEngine(const Property& engine_props)
{
    TcpEngineImpl* tcp_engine_impl = new TcpEngineImpl;
    assert(tcp_engine_impl);

    Property engine_new_props = engine_props;
    if (adk_impl::IsEnvSetLowUtilization())
    {
        engine_new_props.SetValue(config::kIsTxLowLatency, false);
        engine_new_props.SetValue(config::kIsRxLowLatency, false);
        engine_new_props.SetValue(config::endpoint::kTxMinResidentMicro, 0);
        engine_new_props.SetValue(config::endpoint::kRxMinResidentMicro, 0);
    }

    if (ADK_UNLIKELY(ErrorCode::kSuccess != tcp_engine_impl->Init(engine_props)))
    {
        tcp_engine_impl->Exit();
        delete tcp_engine_impl;
        tcp_engine_impl = nullptr;
    }

    return tcp_engine_impl;
}

TcpEngine* TcpEngine::Create(const Property& engine_props)
{
    signal(SIGPIPE, SIG_IGN);

    const auto engine_name = engine_props.GetValue(config::kName, std::string());
    if (!engine_name.empty())
    {
        std::lock_guard<std::mutex> _(s_tcp_engines_lck_);
        const auto iter = s_tcp_engines_map_.find(engine_name);
        if (s_tcp_engines_map_.end() != iter)
        {
            TcpEngineImpl* const tcp_engine_impl = iter->second;
            assert(tcp_engine_impl);

            tcp_engine_impl->add_reference();
            return tcp_engine_impl;
        }

        auto* const tcp_engine_impl = CreateTcpEngine(engine_props);
        if (nullptr != tcp_engine_impl)
        {
            s_tcp_engines_map_[engine_name] = tcp_engine_impl;
        }

        return tcp_engine_impl;
    }

    auto* const tcp_engine = CreateTcpEngine(engine_props);
    if (nullptr != tcp_engine)
    {
        std::lock_guard<std::mutex> _(s_tcp_engines_lck_);
        ADK_ASSERT_BOOL(s_tcp_engines_set_.insert(tcp_engine).second);
    }
    return tcp_engine;
}

void TcpEngine::Destroy(TcpEngine* const tcp_engine)
{
    assert(tcp_engine);

    TcpEngineImpl* tcp_engine_impl = (TcpEngineImpl*)tcp_engine;
    const auto& engine_name = tcp_engine_impl->engine_name();
    if (!engine_name.empty())
    {
        std::lock_guard<std::mutex> _(s_tcp_engines_lck_);
        const auto iter = s_tcp_engines_map_.find(engine_name);
        assert(s_tcp_engines_map_.end() != iter);
        assert(iter->second == tcp_engine_impl);

        if (!tcp_engine_impl->sub_reference())
        {
            return;
        }

        s_tcp_engines_map_.erase(iter);
    }
    else
    {
        std::lock_guard<std::mutex> _(s_tcp_engines_lck_);
        ADK_ASSERT_BOOL(s_tcp_engines_set_.erase(tcp_engine_impl) > 0);
    }

    tcp_engine_impl->Exit();
    delete tcp_engine_impl;
}

TcpEngine::StackType TcpEngine::GetStackType(const std::string& message_ip)
{
    ITcpStack* tcp_stack = nullptr;
    if (message_ip.empty())
    {
        struct ifaddrs* if_infos;
        getifaddrs(&if_infos);
        for (auto if_node = if_infos; if_node != nullptr; if_node = if_node->ifa_next)
        {
            if (if_node->ifa_addr
                && (if_node->ifa_flags & IFF_UP)
                && (AF_INET == if_node->ifa_addr->sa_family))
            {
                auto* const sa_if = (struct sockaddr_in*)(if_node->ifa_addr);

                // 127.0.0.1
                if (0x100007f == sa_if->sin_addr.s_addr)
                {
                    continue;
                }

                tcp_stack = verbs::ITcpStack::Create(inet_ntoa(sa_if->sin_addr));
                break;
            }
        }

        freeifaddrs(if_infos);
    }
    else
    {
        tcp_stack = verbs::ITcpStack::Create(message_ip);
    }

    auto stack_type = StackType::kUnknown;
    if (nullptr != tcp_stack)
    {
        switch (tcp_stack->stack_type())
        {
        case verbs::ITcpStack::StackType::kStackSk:
            stack_type = StackType::kTcpIp;
            break;
        case verbs::ITcpStack::StackType::kStackZf:
            stack_type = StackType::kTcpDirect;
            break;
        default:
            break;
        }

        verbs::ITcpStack::Destroy(tcp_stack);
    }

    return stack_type;
}

const char* TcpEngine::GetLastError()
{
    return GetErrorInfo();
}

int32_t TcpEngine::Pause()
{
    bool result = true;
    std::lock_guard<std::mutex> _(s_tcp_engines_lck_);
    for (auto& tcp_engine_impl : s_tcp_engines_set_)
    {
        if (ErrorCode::kSuccess != tcp_engine_impl->Pause())
        {
            result = false;
        }
    }

    for (auto& node : s_tcp_engines_map_)
    {
        if (ErrorCode::kSuccess != node.second->Pause())
        {
            result = false;
        }
    }

    return result ? ErrorCode::kSuccess : ErrorCode::kFailure;
}

void TcpEngine::Resume()
{
    std::lock_guard<std::mutex> _(s_tcp_engines_lck_);
    for (auto& tcp_engine_impl : s_tcp_engines_set_)
    {
        tcp_engine_impl->Resume();
    }

    for (auto& node : s_tcp_engines_map_)
    {
        node.second->Resume();
    }
}

Acceptor* TcpEngine::Accept(const Property& accept_props)
{
    return static_cast<TcpEngineImpl*>(this)->ToAccept(accept_props);
}

Endpoint* TcpEngine::Connect(const Property& connect_props)
{
    return static_cast<TcpEngineImpl*>(this)->ToConnect(connect_props);
}

Message* TcpEngine::NewMessage(uint32_t len)
{
    return (Message*)static_cast<TcpEngineImpl*>(this)->NewTxMessage(len);
}

void TcpEngine::DeleteMessage(Message* message)
{
    if (((MessageImpl*)message)->is_direction_tx())
    {
        if (((MessageImpl*)message)->is_last_reference())
        {
            IoEngineBase::DeleteTxMessage(((MessageImpl*)message));
        }
    }
    else
    {
        IoEngineBase::DeleteRxMessage(((MessageImpl*)message));
    }
}

int32_t TcpEngine::CollectIndicator(std::string& indicator)
{
    return static_cast<TcpEngineImpl*>(this)->CollectIndicator(indicator);
}

}

}

