#define BOOST_TEST_MODULE monitor
#include <boost/test/included/unit_test.hpp>

#include <stdio.h>
#include <unistd.h>
#include <string>
#include <adk/monitor/system_info.h>

BOOST_AUTO_TEST_CASE(test_get_system_info)
{
    printf("cpu cores:%d\n", adk::GetSystemCPUCores());
    printf("cpu @ %lf Ghz\n", adk::GetSystemCPUGHZ());
    printf("cpu usage rate:%lf%%\n", adk::GetCpuUsageRate());
    auto ram_info = adk::GetRAMInfo();
    printf("total memory:%dG\n", ram_info.first);
    printf("free memory:%dG\n", ram_info.second);

    for (int i = 0; i < 5; ++i)
    {
        auto disk_info = adk::GetDiskInfo();
        printf("total disk:%dG\n", std::get<0>(disk_info));
        printf("free disk:%dG\n", std::get<1>(disk_info));
        printf("IOPS:%u\n", std::get<2>(disk_info));
        sleep(1);
    }
}
