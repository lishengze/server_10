#ifndef ADK_IMPL_SYSTEM_INFO_H_
#define ADK_IMPL_SYSTEM_INFO_H_

#include <string>
#include <utility>
#include <tuple>

namespace adk_impl
{
extern unsigned int g_cpu_cores;
extern double g_system_ghz;

unsigned int GetSystemCPUCores();

double GetSystemCPUGHZ();

double GetCpuUsageRate();

std::pair<unsigned int, unsigned int> GetRAMInfo();

std::tuple<unsigned int, unsigned int, unsigned int> GetDiskInfo();

std::tuple<unsigned int, double, double> GetBandwidthInfo(const std::string &ip);

}

#endif