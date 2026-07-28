#define BOOST_TEST_MODULE adk_util
#include <boost/test/included/unit_test.hpp>
#include <turtle/mock.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

#include <adk/util.h>
#include <adk/error_code.h>

#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <iomanip>

using namespace adk;
using std::endl;
using std::cout;

// 统计进程打开的fd数量
int32_t CountingFDs()
{
    int32_t nr = 0;
    boost::filesystem::directory_iterator end_it; 
    for(boost::filesystem::directory_iterator i("/proc/self/fdinfo"); i != end_it; ++i)
    {
        ++nr;
    }
    return nr;
}

// 保存还原coredump_filter的内容
struct Save
{
    static char buf[32];
    static int fd;

    Save()
    {
        fd = open("/proc/self/coredump_filter", O_RDWR, 0660);
        memset(buf, 0x00, sizeof(buf));
        read(fd, buf, 12);
    }

    static void Restore()
    {
        write(fd, buf, strlen(buf));
    }
};

char Save::buf[32];
int Save::fd;
Save g_save;

// 不提供环境变量时，默认保存共享内存到core文件
BOOST_AUTO_TEST_CASE(basic_usage)
{
    auto total_saved = CountingFDs();
    BOOST_REQUIRE(adk::EnableShareMemoryDump(nullptr) == adk::ErrorCode::kSuccess);
    auto total_now = CountingFDs();
    // fd数量不变
    BOOST_REQUIRE(total_now == total_saved);

    int fd = open("/proc/self/coredump_filter", O_RDWR, 0660);
    char buf[12];

    int var = (1 << 0)
              + (1 << 1)
              + (1 << 2)
              + (1 << 3)
              + (1 << 4)
              + (1 << 5)
              + (1 << 6)
              ;
    std::stringstream ss;
    ss << std::hex << var;
    std::string expect_str(ss.str());

    memset(buf, 0x00, sizeof(buf));
    size_t ret = read(fd, buf, 12);
    std::string read_str(buf);
    boost::trim_if(read_str, boost::is_any_of("0 \n"));
    BOOST_REQUIRE_EQUAL(expect_str, read_str);
}

// 提供环境变量，但未配置时，默认不保存共享内存到core文件
BOOST_AUTO_TEST_CASE(with_env_name_0)
{
    Save::Restore();
    auto total_saved = CountingFDs();
    BOOST_REQUIRE(adk::EnableShareMemoryDump("ABC") == adk::ErrorCode::kFailure);
    auto total_now = CountingFDs();
    BOOST_REQUIRE(total_now == total_saved);

    int fd = open("/proc/self/coredump_filter", O_RDWR, 0660);
    char buf[12];

    int var = (1 << 0)
              + (1 << 1)
              + (1 << 2)
              + (1 << 3)
              + (1 << 4)
              + (1 << 5)
              + (1 << 6)
              ;
    std::stringstream ss;
    ss << std::hex << var;
    std::string expect_str(ss.str());

    memset(buf, 0x00, sizeof(buf));
    size_t ret = read(fd, buf, 12);
    std::string read_str(buf);
    boost::trim_if(read_str, boost::is_any_of("0 \n"));

    BOOST_REQUIRE(expect_str != read_str);
}

// 提供环境变量，但配置时，保存共享内存到core文件
BOOST_AUTO_TEST_CASE(with_env_name_1)
{
    Save::Restore();

    ::setenv("ABC", "y", 1);
    auto total_saved = CountingFDs();
    BOOST_REQUIRE(adk::EnableShareMemoryDump("ABC") == adk::ErrorCode::kSuccess);
    auto total_now = CountingFDs();
    BOOST_REQUIRE(total_now == total_saved);

    int fd = open("/proc/self/coredump_filter", O_RDWR, 0660);
    char buf[12];

    int var = (1 << 0)
              + (1 << 1)
              + (1 << 2)
              + (1 << 3)
              + (1 << 4)
              + (1 << 5)
              + (1 << 6)
              ;
    std::stringstream ss;
    ss << std::hex << var;
    std::string expect_str(ss.str());

    memset(buf, 0x00, sizeof(buf));
    size_t ret = read(fd, buf, 12);
    std::string read_str(buf);
    boost::trim_if(read_str, boost::is_any_of("0 \n"));
    BOOST_REQUIRE_EQUAL(expect_str, read_str);
}
