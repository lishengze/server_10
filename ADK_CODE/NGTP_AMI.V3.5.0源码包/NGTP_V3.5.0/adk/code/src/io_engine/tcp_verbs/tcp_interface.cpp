#include "tcp_socket.h"
#if defined(__x86_64__)
#include "tcp_direct_zf.h"
#elif defined(__aarch64__)
#include "tcp_direct_zf_arm.h"
#endif

#include <net/if.h>
#include <ifaddrs.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

namespace adk_impl
{

namespace verbs
{

std::string GetInterfaceName(const std::string& ipv4)
{
    std::string if_name;

    struct ifaddrs* if_infos;
    getifaddrs(&if_infos);
    for (auto if_node = if_infos; if_node != nullptr; if_node = if_node->ifa_next)
    {
        if (if_node->ifa_addr
            && (if_node->ifa_flags & IFF_UP)
            && (AF_INET == if_node->ifa_addr->sa_family))
        {
            auto* const sa_if = (struct sockaddr_in*)(if_node->ifa_addr);
            if (ipv4 == inet_ntoa(sa_if->sin_addr))
            {
                if_name = if_node->ifa_name;
                break;
            }
        }
    }

    freeifaddrs(if_infos);
    return if_name;
}

ITcpStack* ITcpStack::Create(const std::string& message_ip)
{
    std::string nic_name;
    if (!message_ip.empty())
    {
        nic_name = GetInterfaceName(message_ip);
        if (nic_name.empty())
        {
            return nullptr;
        }

        ITcpStack* tcp_stack = new TcpStackZf(message_ip);
        if (tcp_stack->Open(nic_name))
        {
            return tcp_stack;
        }

        ITcpStack::Destroy(tcp_stack);
    }

    ITcpStack* tcp_stack = new TcpStackSk(message_ip);
    if (tcp_stack->Open(nic_name))
    {
        return tcp_stack;
    }

    ITcpStack::Destroy(tcp_stack);
    return nullptr;
}

void ITcpStack::Destroy(ITcpStack* tcp_stack)
{
    assert(tcp_stack);

    tcp_stack->Close();
    delete tcp_stack;
}

ITcpEPoller* ITcpEPoller::Create(ITcpStack* tcp_stack, PollerType poller_type)
{
    ITcpEPoller* tcp_epoller = nullptr;
    switch (tcp_stack->stack_type())
    {
    case ITcpStack::StackType::kStackSk:
        tcp_epoller = new TcpEPollerSk;
        break;
    case ITcpStack::StackType::kStackZf:
        if (PollerType::kControl == poller_type)
        {
            tcp_epoller = new TcpEPollerZfControl;
        }
        else if (PollerType::kSpecial == poller_type)
        {
            tcp_epoller = new TcpEPollerZfSpecial;
        }
        else if (PollerType::kGeneral == poller_type)
        {
            tcp_epoller = new TcpEPollerZfGeneral;
        }
        else
        {
            tcp_epoller = new TcpEPollerZf;
        }
        break;
    default:
        return nullptr;
    }

    assert(tcp_epoller);
    if (ADK_UNLIKELY(!tcp_epoller->Open(tcp_stack)))
    {
        tcp_epoller->Close();
        delete tcp_epoller;
        return nullptr;
    }

    return tcp_epoller;
}

void ITcpEPoller::Destroy(ITcpEPoller* tcp_epoller)
{
    assert(tcp_epoller);

    tcp_epoller->Close();
    delete tcp_epoller;
}

ITcpAcceptor* ITcpAcceptor::Create(ITcpStack* tcp_stack, 
                                   const std::string& listen_ip, 
                                   uint16_t listen_port, 
                                   bool reuse_addr,
                                   bool reuse_port)
{
    ITcpAcceptor* tcp_acceptor = nullptr;
    switch (tcp_stack->stack_type())
    {
    case ITcpStack::StackType::kStackSk:
        tcp_acceptor = new TcpAcceptorSk;
        break;
    case ITcpStack::StackType::kStackZf:
        tcp_acceptor = new TcpAcceptorZf;
        break;
    default:
        return nullptr;
    }

    assert(tcp_acceptor);
    if (ADK_UNLIKELY(!tcp_acceptor->Open(tcp_stack, 
                                         listen_ip, 
                                         listen_port, 
                                         reuse_addr,
                                         reuse_port)))
    {
        tcp_acceptor->Close();
        delete tcp_acceptor;
        return nullptr;
    }

    return tcp_acceptor;
}

void ITcpAcceptor::Destroy(ITcpAcceptor* tcp_acceptor)
{
    assert(tcp_acceptor);

    tcp_acceptor->Close();
    delete tcp_acceptor;
}

ITcpEndpoint* ITcpEndpoint::Create(ITcpStack* tcp_stack, bool reuse_addr, bool reuse_port)
{
    ITcpEndpoint* tcp_endpoint = nullptr;
    switch (tcp_stack->stack_type())
    {
    case ITcpStack::StackType::kStackSk:
        tcp_endpoint = new TcpEndpointSk;
        break;
    case ITcpStack::StackType::kStackZf:
        tcp_endpoint = new TcpEndpointZf;
        break;
    default:
        return nullptr;
    }

    assert(tcp_endpoint);
    if (ADK_UNLIKELY(!tcp_endpoint->Open(tcp_stack, reuse_addr, reuse_port)))
    {
        ITcpEndpoint::Destroy(tcp_endpoint);
        return nullptr;
    }

    return tcp_endpoint;
}

void ITcpEndpoint::Destroy(ITcpEndpoint* tcp_endpoint)
{
    assert(tcp_endpoint);

    tcp_endpoint->Close();
    delete tcp_endpoint;
}

}

}