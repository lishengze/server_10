#include "multicast.h"

#include <assert.h>

struct ip_mrep
{
    struct in_addr imr_multiaddr;  // IP multicast address of group.
    struct in_addr imr_interface;  // Local IP address of interface.
};

MulticastRaw* MulticastRaw::Create(const std::string& nic_addr, uint16_t port)
{
    MulticastRaw* multicast_impl = new MulticastRaw;
    assert(multicast_impl);

    if (!multicast_impl->Open(port, std::string(), true))
    {
        multicast_impl->Exit();
        delete multicast_impl;
        return nullptr;
    }

    int32_t multicast_all = 0;
    setsockopt(multicast_impl->socket_fd_, IPPROTO_IP, 
               IP_MULTICAST_ALL, &multicast_all, sizeof(int32_t));

    struct in_addr addr_nic;
    addr_nic.s_addr = nic_addr.empty() ? htonl(INADDR_ANY)
                                       : inet_addr(nic_addr.c_str());

    setsockopt(multicast_impl->socket_fd_, IPPROTO_IP, IP_MULTICAST_IF, 
               reinterpret_cast<const char*>(&addr_nic), sizeof(struct in_addr));

    multicast_impl->nic_addr_ = nic_addr;
    return multicast_impl;
}

void MulticastRaw::Delete(MulticastRaw* const multicast_impl)
{
    assert(multicast_impl);
    multicast_impl->Exit();
    delete multicast_impl;
}

void MulticastRaw::SetOptTTL(uint8_t ttl)
{
    setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL, 
               reinterpret_cast<const char*>(&ttl), sizeof(uint8_t));
}

void MulticastRaw::SetOptMcLoop(bool is_loop)
{
    const uint8_t loop_value = (uint8_t)is_loop;
    setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_LOOP, 
               reinterpret_cast<const char*>(&loop_value), sizeof(uint8_t));
}

bool MulticastRaw::JoinMcGroup(const std::string& addr)
{
    ip_mrep multicast_addr;
    multicast_addr.imr_interface.s_addr = inet_addr(nic_addr_.c_str());
    multicast_addr.imr_multiaddr.s_addr = inet_addr(addr.c_str());

    const auto result = setsockopt(socket_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, 
                                   reinterpret_cast<const char*>(&multicast_addr), 
                                   sizeof(struct ip_mrep));
    if (result < 0)
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> _(mc_grp_lock_);
        multicast_grp_.insert(multicast_addr.imr_multiaddr.s_addr);
    }

    return true;
}

bool MulticastRaw::LeaveMcGroup(const std::string& addr)
{
    ip_mrep multicast_addr;
    multicast_addr.imr_interface.s_addr = inet_addr(nic_addr_.c_str());
    multicast_addr.imr_multiaddr.s_addr = inet_addr(addr.c_str());

    {
        std::lock_guard<std::mutex> _(mc_grp_lock_);
        if (ADK_UNLIKELY(multicast_grp_.end()
            == multicast_grp_.find(multicast_addr.imr_multiaddr.s_addr)))
        {
            return false;
        }
    }

    const auto result = setsockopt(socket_fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                                   reinterpret_cast<const char*>(&multicast_addr),
                                   sizeof(struct ip_mrep));
    if (result < 0)
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> _(mc_grp_lock_);
        multicast_grp_.erase(multicast_addr.imr_multiaddr.s_addr);
    }

    return true;
}

void MulticastRaw::Exit()
{
    std::set<uint32_t> multicast_grp;

    {
        std::lock_guard<std::mutex> _(mc_grp_lock_);
        multicast_grp.swap(multicast_grp_);
    }

    ip_mrep multicast_addr;
    multicast_addr.imr_interface.s_addr = inet_addr(nic_addr_.c_str());
    for (auto iter = multicast_grp.begin(); iter != multicast_grp.end(); ++iter)
    {
        multicast_addr.imr_multiaddr.s_addr = *iter;
        (void)setsockopt(socket_fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, 
                         reinterpret_cast<const char*>(&multicast_addr), 
                         sizeof(struct ip_mrep));
    }

    Close();
}