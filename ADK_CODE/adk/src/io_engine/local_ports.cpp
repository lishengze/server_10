#include "local_ports.h"

#include <fstream>

#include <adk/error_code.h>
#include <adk/arch/generic.h>

namespace adk_impl
{

namespace io_engine
{

int32_t GetSysLocalPortRange(uint16_t& range_low, uint16_t& range_high)
{
    std::ifstream sysctl_file("/proc/sys/net/ipv4/ip_local_port_range");
    if (ADK_UNLIKELY(!sysctl_file.is_open()))
    {
        return ErrorCode::kFailure;
    }

    sysctl_file >> range_low >> range_high;
    return ErrorCode::kSuccess;
}

LocalPorts::LocalPorts(uint16_t range_low, uint16_t range_high)
{
    uint16_t sys_port_low;
    uint16_t sys_port_high;
    if (ErrorCode::kSuccess == GetSysLocalPortRange(sys_port_low, sys_port_high))
    {
        range_low = std::max<uint16_t>(range_low, std::min<uint16_t>(sys_port_low, sys_port_high));
        range_high = std::min<uint16_t>(range_high, std::max<uint16_t>(sys_port_low, sys_port_high));
    }

    for (int32_t port = (int32_t)range_low; port <= (int32_t)range_high; ++port)
    {
        local_ports_.push_back((uint16_t)port);
    }

    fail_nr_ = 0;
}

}

}