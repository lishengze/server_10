#include <sys/vfs.h>
#include <sys/statvfs.h>
#include <net/if.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <linux/types.h>
#include <sys/socket.h>
#include <linux/sockios.h>
#include <sys/ioctl.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <stdio.h>
#include <string>
#include <fstream>
#include <chrono>
#include <vector>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <adk/util.h>
#include <adk/monitor/system_info.h>

namespace adk_impl
{

struct ethtool_cmd 
{
    __u32	cmd;
    __u32	supported;
    __u32	advertising;
    __u16	speed;
    __u8	duplex;
    __u8	port;
    __u8	phy_address;
    __u8	transceiver;
    __u8	autoneg;
    __u8	mdio_support;
    __u32	maxtxpkt;
    __u32	maxrxpkt;
    __u16	speed_hi;
    __u8	eth_tp_mdix;
    __u8	eth_tp_mdix_ctrl;
    __u32	lp_advertising;
    __u32	reserved[2];
};

unsigned int g_cpu_cores = 0;
double g_system_ghz = 0;
std::map<std::string, uint32_t> g_ip_speed_map;
std::map<std::string, std::string> g_ip_net_dev_map;

static std::vector<std::pair<std::string, std::string>> GetNetDeviceNameByIP(const std::string &ip);
static uint32_t GetNetDeviceSpeed(const std::string &dev_name);
static void GetAllNetDeviceSpeed();

struct DoInit
{
    DoInit()
    {
        // GetSystemCPUCores();
        // GetSystemCPUGHZ();
        // GetAllNetDeviceSpeed();
    }
};
static DoInit s_g_do_init;

static bool ExecuteShellAndGetResult(const std::string &cmd, std::string &result);

unsigned int GetSystemCPUCores()
{
    g_cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);
    return g_cpu_cores;
}

double GetSystemCPUGHZ()
{
    const static std::string cmd = "lscpu | grep @";
    std::string ret;
    if (!ExecuteShellAndGetResult(cmd, ret))
    {
        return g_system_ghz;
    }

    auto pos = ret.find('@');
    if (pos == std::string::npos)
    {
        return g_system_ghz;
    }

    ret = ret.substr(pos+1, std::string::npos);
    boost::trim(ret);

    std::string frequency_str;
    std::size_t i = 0;
    for (; i < ret.size(); ++i)
    {
        if (std::isdigit(ret[i]) || ret[i] == '.')
        {
            frequency_str.push_back(ret[i]);
            continue;
        }

        break;
    }

    std::string uint_str = ret.substr(i, std::string::npos);
    boost::to_lower(uint_str);
    try
    {
        auto tmp = std::stod(frequency_str);
        if (uint_str == "ghz")
        {
            g_system_ghz = tmp;
        }
        else if (uint_str == "mhz")
        {
            g_system_ghz = tmp / 1000.0;
        }
        else if (uint_str == "hz")
        {
            g_system_ghz = tmp / 1000000.0;
        }
    }
    catch(const std::exception &err)
    {
    }
    return g_system_ghz;
}

#define idle 3
double GetCpuUsageRate()
{
    static thread_local uint64_t total_frame_old = 0;
	static thread_local uint64_t total_frame_new = 0;
	static thread_local uint64_t idle_frame_old = 0;
	static thread_local uint64_t idle_frame_new = 0;

    std::ifstream ifs("/proc/stat");
    std::string line;
    if (!std::getline(ifs, line))
    {
        return 0;
    }
    line = line.substr(3, std::string::npos);
    boost::trim(line);
    std::vector<std::string> str_vec;
    boost::split(str_vec, line, boost::is_any_of(" \t\n"));

    total_frame_new = 0;
    idle_frame_new = 0;
    int size = 0;
    for (auto str : str_vec)
    {
        if (!str.empty())
        {
            uint64_t tmp = std::stoull(str);
            total_frame_new += tmp;
            if (size++ == idle)
            {
                idle_frame_new = tmp;
            }
        }
    }

    uint64_t total_frame = total_frame_new - total_frame_old;
    uint64_t idle_frame = idle_frame_new - idle_frame_old;
    total_frame_old = total_frame_new;
    idle_frame_old = idle_frame_new;
    double cpu_use = double(total_frame - idle_frame) / double(total_frame) * 100.0;
    return cpu_use;
}

static uint64_t GetRamStringInfo(const std::string &line, const std::string &key)
{
    uint64_t number = 0;
    try
    {   boost::smatch ret;
        boost::regex reg("^" + key + ":([\\s]+)([0-9]+) kB$");
        if (boost::regex_match(line, ret, reg))
        {
            number = std::stoull(ret[2]);
        }   
    }
    catch(...)
    {
    }

    return number;
}

std::pair<unsigned int, unsigned int> GetRAMInfo()
{
    // MemTotal:       65419796 kB
    // MemFree:        22870048 kB
    // MemAvailable:   40220400 kB
    // Buffers:              68 kB
    // Cached:         17530136 kB

    static const std::vector<std::string> key_vec = {"MemTotal", "MemFree", "MemAvailable", "Buffers", "Cached"};

    std::ifstream ifs("/proc/meminfo");
    uint64_t mem_total = 0;
    uint64_t other = 0;
    for(std::size_t i = 0; i < key_vec.size(); ++i)
    {
        std::string line;
        if (!std::getline(ifs, line))
        {
            return std::make_pair(0, 0);
        }

        if (i == 2)
        {
            continue;
        }

        if (i == 0)
        {
            mem_total = GetRamStringInfo(line, key_vec[0]);
            continue;
        }

        other += GetRamStringInfo(line, key_vec[i]);
    }

    if (mem_total < other)
    {
        return std::make_pair(0, 0);
    }

    //used = total - free - buffers - cache
    return std::make_pair(mem_total/1024/1024, (mem_total - other)/1024/1024);
}

std::tuple<unsigned int, unsigned int, unsigned int> GetDiskInfo()
{
    std::ifstream ifs("/etc/fstab");
    std::string line;
    uint64_t total_disk_gb = 0;
    uint64_t total_disk_free_gb = 0;

    while (std::getline(ifs, line))
    {
        if (!boost::starts_with(line, "/dev/"))
        {
            continue;
        }

        // /dev/mapper/rhel-home   /home                   xfs     defaults        0 0
        auto pos = line.find(' ');
        if (pos != std::string::npos)
        {
            while (std::isspace((unsigned char)(line[++pos])))
            {
                continue;
            }
            auto beg = pos;
            while (!std::isspace((unsigned char)(line[++pos])))
            {
                continue;
            }
            auto disk_path = line.substr(beg, pos - beg);
            struct statvfs stat;
            memset(&stat, 0, sizeof(stat));
            if (statvfs(disk_path.c_str(), &stat) == 0 && stat.f_flag)
            {
                total_disk_gb += stat.f_blocks * stat.f_bsize;
                total_disk_free_gb += stat.f_bfree * stat.f_bsize;
            }
        }
    }

    total_disk_gb /= 1024 * 1024 * 1024;
    total_disk_free_gb /= 1024 * 1024 * 1024;

    static thread_local std::chrono::system_clock::time_point last_time = std::chrono::system_clock::now();
    static thread_local uint64_t io_old = 0;
    static thread_local uint64_t io_new = 0;

    io_new = 0;
    ifs.close();
    ifs.open("/proc/diskstats");
    while (std::getline(ifs, line))
    {
        boost::trim(line);
        std::vector<std::string> str_vec;
        boost::split(str_vec, line, boost::is_any_of(" \t\n"));

        uint64_t tmp_read_io_new = 0;
        uint64_t tmp_write_io_new = 0;
        int count = 0;
        for (auto &str : str_vec)
        {
            boost::trim(str);
            if (!str.empty())
            {
                ++count;
            }

            try
            {
                if (count == 4)
                {
                    tmp_read_io_new = std::stoull(str);
                }

                if (count == 8)
                {
                    tmp_write_io_new = std::stoull(str);
                }
            }
            catch(...)
            {
                break;
            }

            if (count > 7)
            {
                io_new += tmp_read_io_new + tmp_write_io_new;
                break;
            }
        }
    }

    unsigned int io_count = 0;
    if (io_new > io_old)
    {
        auto now = std::chrono::system_clock::now();
        if (io_old != 0)
        {
            io_count = (io_new - io_old) / std::chrono::duration<double>(now - last_time).count();
        }
        io_old = io_new;
        last_time = now;
    }

    return std::make_tuple(total_disk_gb, total_disk_gb - total_disk_free_gb, io_count);
}

std::tuple<unsigned int, double, double> GetBandwidthInfo(const std::string &ip)
{
    struct BandwidthInfo
    {
        std::chrono::system_clock::time_point time_point;
        uint64_t rx_bytes = 0;
        uint64_t tx_bytes = 0;
    };

    static std::map<std::string, BandwidthInfo> ip_bandwidth_info_map;
    static std::string proc_net_dev = "/proc/net/dev";

    if (ip.empty())
    {
        return std::make_tuple(0, 0.0, 0.0);
    }

    std::ifstream ifs(proc_net_dev);
    if (!ifs)
    {
        return std::make_tuple(0, 0.0, 0.0);
    }

    uint32_t speed = 0;
    auto it = g_ip_net_dev_map.find(ip);
    if (ADK_UNLIKELY(it == g_ip_net_dev_map.end()))
    {
        auto net_dev_vec = GetNetDeviceNameByIP(ip);
        if (ADK_UNLIKELY(net_dev_vec.empty()))
        {
            return std::make_tuple(0, 0.0, 0.0);
        }

        it = g_ip_net_dev_map.find(ip);
        assert(it != g_ip_net_dev_map.end());

        speed = GetNetDeviceSpeed(net_dev_vec[0].second);
        g_ip_speed_map[ip] = speed;
    }

    speed = g_ip_speed_map[ip];

    std::string line;
    int i = 0;
    while (std::getline(ifs, line))
    {
        if (i++ <= 1)
        {
            continue;
        }

        auto pos = line.find(':');
        if (pos == std::string::npos)
        {
            continue;
        }

        auto new_dev_name = line.substr(0, pos);
        boost::trim(new_dev_name);
        if (new_dev_name == it->second)
        {
            auto data = line.substr(pos+1, std::string::npos);
            boost::trim(data);
            std::vector<std::string> data_vec;
            std::vector<std::string> data_num_vec;
            data_num_vec.reserve(16);
            boost::split(data_vec, data, boost::is_any_of(" \t\n"));
            for (auto &item : data_vec)
            {
                boost::trim(item);
                if (item.empty())
                {
                    continue;
                }

                data_num_vec.push_back(std::move(item));
            }

            if (data_num_vec.size() != 16)
            {
                return std::make_tuple(speed, 0.0, 0.0);
            }

            uint64_t rx_bytes = 0;
            uint64_t tx_bytes = 0;
            try
            {   
                rx_bytes = std::stoull(data_num_vec[0]);
                tx_bytes = std::stoull(data_num_vec[8]);
            }
            catch(...)
            {
                return std::make_tuple(speed, 0.0, 0.0);
            }

            auto info_it = ip_bandwidth_info_map.find(ip);
            if (info_it == ip_bandwidth_info_map.end())
            {
                BandwidthInfo info;
                info.time_point = std::chrono::system_clock::now();
                info.rx_bytes = rx_bytes;
                info.tx_bytes = tx_bytes;
                ip_bandwidth_info_map[ip] = std::move(info);
                return std::make_tuple(speed, 0.0, 0.0);
            }

            auto now = std::chrono::system_clock::now();
            assert(now > info_it->second.time_point);
            auto diff = std::chrono::duration<double>(now - info_it->second.time_point).count();
            info_it->second.time_point = now;

            double rx_rate = 0.0;
            double tx_rate = 0.0;
            if (rx_bytes >= info_it->second.rx_bytes)
            {
                rx_rate = (rx_bytes - info_it->second.rx_bytes) / diff;
            }
            info_it->second.rx_bytes = rx_bytes;

            if (tx_bytes >= info_it->second.tx_bytes)
            {
                tx_rate = (tx_bytes - info_it->second.tx_bytes) / diff;
            }
            info_it->second.tx_bytes = tx_bytes;

            return std::make_tuple(speed, rx_rate, tx_rate);
        }
    }

    return std::make_tuple(speed, 0.0, 0.0);
}

static uint32_t GetNetDeviceSpeed(const std::string &dev_name)
{
    struct ethtool_cmd ecmd;
	memset(&ecmd, 0, sizeof(ecmd));
	ecmd.cmd = 0x00000001;
	struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_data = (char*)(&ecmd);
	strcpy(ifr.ifr_name, dev_name.c_str());
	auto fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return 0;
    }

	if (ioctl(fd, SIOCETHTOOL, &ifr) < 0)
    {
        close(fd);
        return 0;
    }
    close(fd);
	return (ecmd.speed_hi << 16) | ecmd.speed;
}

static std::vector<std::pair<std::string, std::string>> GetNetDeviceNameByIP(const std::string &ip)
{
    std::vector<std::pair<std::string, std::string>> net_dev_vec;
    struct ifaddrs* if_infos;
    getifaddrs(&if_infos);
    for (auto if_node = if_infos; if_node != nullptr; if_node = if_node->ifa_next)
    {
        if (if_node->ifa_addr && (if_node->ifa_flags & IFF_UP) && (AF_INET == if_node->ifa_addr->sa_family))
        {
            auto* const sa_if = (struct sockaddr_in*)(if_node->ifa_addr);
            std::string addr(inet_ntoa(sa_if->sin_addr));

            std::string net_dev_name(if_node->ifa_name);
            g_ip_net_dev_map[addr] = net_dev_name;
            if (ip.empty() || addr == ip)
            {
                net_dev_vec.push_back(std::make_pair(std::move(addr), std::move(net_dev_name)));
                if (!ip.empty())
                {
                    break;
                }
            }
        }
    }

    freeifaddrs(if_infos);
    return net_dev_vec;
}

static void GetAllNetDeviceSpeed()
{
    auto net_dev_ip_vec = GetNetDeviceNameByIP(std::string());
    for (auto &item : net_dev_ip_vec)
    {
        auto speed = GetNetDeviceSpeed(item.second);
        g_ip_speed_map[item.first] = speed;
    }
}

static bool ExecuteShellAndGetResult(const std::string &cmd, std::string &result)
{
    static const size_t buf_size = 1024*4;
    ::unsetenv("LD_PRELOAD");
    FILE *shell_stream = popen(cmd.c_str(), "r");
    if (shell_stream == nullptr)
    {
        return false;
    }
    
    size_t read_len = 0;
    size_t pos = 0;
    result.resize(buf_size);
    while (true)
    {
        read_len = fread(&result[pos], sizeof(char), buf_size, shell_stream);
        pos += read_len;
        if (feof(shell_stream) || ferror(shell_stream))
        {
            result.resize(pos);
            break;
        }
        read_len = 0;
        result.resize(buf_size + pos);
    } 
    pclose(shell_stream);

    return true;
}

}
