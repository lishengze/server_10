#ifndef __ADK_IMPL_NET_UTILS_H_
#define __ADK_IMPL_NET_UTILS_H_

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <string>
#include <vector>

#include <boost/asio.hpp>
#include <boost/thread/pthread/mutex.hpp>

#include "error_code.h"

#define BUFFER_SIZE 4096

namespace adk_impl
{
// route definition
struct route
{
    std::string destination;
    int mask;
    std::string gateway;
    std::string Iface_name;
};

/**
* @brief 获取路由表信息
*/
static inline std::vector<route> GetRouteTable()
{
    static boost::mutex* s_lock = new boost::mutex();
    boost::mutex::scoped_lock lock_guard(*s_lock);

    std::vector<route> rt;
    int received_bytes      = 0;
    int msg_len             = 0;
    int route_attribute_len = 0;
    int sock                = -1;
    uint32_t msgseq         = 0;
    struct nlmsghdr* nlh;
    struct nlmsghdr* nlmsg;
    struct rtmsg* route_entry;

    // This struct contain route attributes (route type)
    struct rtattr* route_attribute;
    char msgbuf[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];
    char* ptr = buffer;
    struct timeval tv;

    // open socket
    if ((sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE)) < 0)
    {
        return rt;
    }

    memset(msgbuf, 0, sizeof(msgbuf));
    memset(buffer, 0, sizeof(buffer));

    // point the header and the msg structure pointers into the buffer
    nlmsg = (struct nlmsghdr*)msgbuf;

    // Fill in the nlmsg header
    nlmsg->nlmsg_len   = NLMSG_LENGTH(sizeof(struct rtmsg));
    nlmsg->nlmsg_type  = RTM_GETROUTE;  // Get the routes from kernel routing table .
    nlmsg->nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST;  // The message is a request for dump.
    nlmsg->nlmsg_seq   = msgseq++;  // Sequence of the message packet.
    nlmsg->nlmsg_pid   = getpid();  // PID of process sending the request.

    // 1 Sec Timeout to avoid stall
    tv.tv_sec = 1;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (struct timeval*)&tv, sizeof(struct timeval));
    // send msg
    if (send(sock, nlmsg, nlmsg->nlmsg_len, 0) < 0)
    {
        return rt;
    }

    // receive response
    do
    {
        received_bytes = recv(sock, ptr, sizeof(buffer) - msg_len, 0);
        if (received_bytes < 0)
        {
            return rt;
        }

        nlh = (struct nlmsghdr*)ptr;

        // Check if the header is valid
        if ((NLMSG_OK(nlmsg, received_bytes) == 0) || (nlmsg->nlmsg_type == NLMSG_ERROR))
        {
            return rt;
        }

        // If we received all data break
        if (nlh->nlmsg_type == NLMSG_DONE)
        {
            break;
        }
        else
        {
            ptr += received_bytes;
            msg_len += received_bytes;
        }

        //  Break if its not a multi part message
        if ((nlmsg->nlmsg_flags & NLM_F_MULTI) == 0)
        {
            break;
        }
    } while ((nlmsg->nlmsg_seq != msgseq) || (static_cast<pid_t>(nlmsg->nlmsg_pid) != getpid()));

    char gateway_address[INET_ADDRSTRLEN], interface[IF_NAMESIZE], dsts[INET_ADDRSTRLEN];
    // parse response
    // outer loop: loops thru all the NETLINK headers that also include the route entry header
    for (; NLMSG_OK(nlh, received_bytes); nlh = NLMSG_NEXT(nlh, received_bytes))
    {
        //  Get the route data
        route_entry = (struct rtmsg*)NLMSG_DATA(nlh);

        //  We are just interested in main routing table
        if (route_entry->rtm_table != RT_TABLE_MAIN)
            continue;

        route_attribute     = (struct rtattr*)RTM_RTA(route_entry);
        route_attribute_len = RTM_PAYLOAD(nlh);

        route r;
        // inner loop: loop thru all the attributes of one route entry
        for (; RTA_OK(route_attribute, route_attribute_len);
             route_attribute = RTA_NEXT(route_attribute, route_attribute_len))
        {

            memset(gateway_address, 0, sizeof(gateway_address));
            memset(interface, 0, sizeof(interface));
            memset(dsts, 0, sizeof(dsts));

            switch (route_attribute->rta_type)
            {
                // unique ID associated with the network interface
            case RTA_OIF:
                if_indextoname(*(int*)RTA_DATA(route_attribute), interface);
                r.Iface_name = std::string(interface);
                break;

                // gateway IPv4 address
            case RTA_GATEWAY:
                inet_ntop(AF_INET, RTA_DATA(route_attribute), gateway_address, sizeof(gateway_address));
                r.gateway = std::string(gateway_address);
                break;

                // destination IPv4 address
            case RTA_DST:
                inet_ntop(AF_INET, RTA_DATA(route_attribute), dsts, sizeof(dsts));
                r.destination = std::string(dsts);
                break;
            default:
                break;
            }
        }

        // mask bits
        r.mask = route_entry->rtm_dst_len;
        rt.push_back(r);
    }

    close(sock);

    return rt;
}

/**
* @brief 获取指定网卡对应的ip地址
*
* @param  eth_name 网卡名称
* @param  address 输出参数，ip地址
*
* @return 成功返回kSuccess，失败返回kFailure
*/
static inline int32_t GetIfAddress(const std::string& eth_name, std::string& address)
{
    struct ifaddrs *addrs, *iter;
    if (getifaddrs(&addrs) != 0)
    {
        return ErrorCode::kFailure;
    }

    iter       = addrs;
    bool found = false;
    char host[NI_MAXHOST];
    while (iter != nullptr)
    {
        if (eth_name == iter->ifa_name)
        {
            int s;
            if (iter->ifa_addr->sa_family == AF_INET || iter->ifa_addr->sa_family == AF_INET6)
            {
                s = getnameinfo(
                    iter->ifa_addr,
                    (iter->ifa_addr->sa_family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6),
                    host,
                    NI_MAXHOST,
                    nullptr,
                    0,
                    NI_NUMERICHOST);
                if (s != 0)
                {
                    return ErrorCode::kFailure;
                }
            }
            else
            {
                iter = iter->ifa_next;
                continue;
            }

            address = &host[0];
            found   = true;
            break;
        }
        iter = iter->ifa_next;
    }

    freeifaddrs(addrs);
    if (!found)
    {
        return ErrorCode::kFailure;
    }

    return ErrorCode::kSuccess;
}

/**
* @brief 通过对端地址查找本机能够连通的ip
*
* @param  peer 对端ip地址
* @param  local 输出参数，可连通的本机ip
*
* @return 找到返回kSuccess，未找到但有default则返回kDefaultGateway，未找到且未配置默认网关则返回kFailure
*/
static inline int32_t GetConnectableIp(const std::string& peer, std::string& local)
{
    // peer must be ip format
    try
    {
        boost::asio::ip::address::from_string(peer);
    }
    catch (const std::exception& e)
    {
        return ErrorCode::kFailure;
    }

    // 127.0.0.1, return directly
    if (peer == "127.0.0.1")
    {
        local = peer;
        return ErrorCode::kSuccess;
    }

    std::string gateway_if;

    // get route table and traversal search
    std::vector<route> rt = GetRouteTable();
    for (auto& r : rt)
    {
        // calc network address by peer_ip and mask
        uint32_t network_mask = 0xFFFFFFFF - (uint32_t)((1ul << (32u - r.mask)) - 1u);
        uint32_t network_val  = inet_network(peer.c_str());
        network_val &= network_mask;

        if (network_val == inet_network(r.destination.c_str()))
        {
            // route found, then get local ip by iface_name
            if (GetIfAddress(r.Iface_name, local) == ErrorCode::kSuccess)
            {
                return ErrorCode::kSuccess;
            }
        }

        // save gateway Iface_name
        if (!r.gateway.empty())
        {
            gateway_if = r.Iface_name;
        }
    }

    if (!gateway_if.empty())
    {
        if (GetIfAddress(gateway_if, local) == ErrorCode::kSuccess)
        {
            return ErrorCode::kDefaultGateway;
        }
    }
    return ErrorCode::kFailure;
}

/**
 * @brief      获取指定IP地址网卡的MTU值
 *
 * @param[in]  ip_address  网卡的IP地址
 * @param[out] mtu         网卡的MTU
 *
 * @return     成功时返回 ErrorCode::kSuccess
 */
static inline int32_t GetInterfaceMtu(const std::string& ip_address, uint32_t& mtu)
{
    struct ifaddrs *addrs, *iter;
    if (getifaddrs(&addrs) != 0)    // get all interfaces information
    {
        return ErrorCode::kFailure;
    }

    iter       = addrs;
    bool found = false;
    char host[NI_MAXHOST];
    struct ifreq ifr;
    while (iter != nullptr) // iterate each interface
    {
        int s;
        // ifa_addr may be NULL
        if (NULL == iter->ifa_addr)
        {
            iter = iter->ifa_next;
            continue;
        }

        if (iter->ifa_addr->sa_family == AF_INET || iter->ifa_addr->sa_family == AF_INET6)
        {
            // the interface has ipv4 or ipv6 address
            s = getnameinfo(
                iter->ifa_addr,
                (iter->ifa_addr->sa_family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6),
                host,
                NI_MAXHOST,
                nullptr,
                0,
                NI_NUMERICHOST);    // get the interface address string
            if (s != 0)
            {
                // getnameinfo failed
                iter = iter->ifa_next;
                continue;
            }

            // success
        }
        else
        {
            // invalid address type
            iter = iter->ifa_next;
            continue;
        }

        if (ip_address == &host[0])
        {
            // save the interface name
            strcpy(ifr.ifr_name, iter->ifa_name);
            found = true;
            break;
        }
        // not the specified address
        iter = iter->ifa_next;
    }

    freeifaddrs(addrs);
    if (!found)
    {
        // no interface with the user specified ip address was found
        return ErrorCode::kFailure;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0); // we need a sock to query the MTU
    if (sock < 0)
    {
        return ErrorCode::kFailure;
    }

    if (!ioctl(sock, SIOCGIFMTU, &ifr)) // get the interface MTU
    {
        mtu = ifr.ifr_mtu;
        close(sock);                    // donot forget to close the socket
        return ErrorCode::kSuccess;
    }
    close(sock);                        // donot forget to close the socket
    return ErrorCode::kFailure;
}

}
#endif