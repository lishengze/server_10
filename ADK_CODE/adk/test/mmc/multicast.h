#ifndef AMI_TC_MULTICAST_H_
#define AMI_TC_MULTICAST_H_

#include "socket_raw.h"

#include <set>
#include <mutex>

class MulticastRaw : public SocketRaw
{
public:
    static MulticastRaw* Create(const std::string& nic_addr, uint16_t port);

    static void Delete(MulticastRaw* const multicast_impl);

    void SetOptTTL(uint8_t ttl);

    void SetOptMcLoop(bool is_loop);

    bool JoinMcGroup(const std::string& addr);

    bool LeaveMcGroup(const std::string& addr);

private:
    void Exit();

    MulticastRaw() = default;
    ~MulticastRaw() = default;

    std::string nic_addr_;

    std::mutex  mc_grp_lock_;
    std::set<uint32_t> multicast_grp_;
};

#endif
