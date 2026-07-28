#ifndef __ADK_IMPL_LICENSE_COMMON_H_
#define __ADK_IMPL_LICENSE_COMMON_H_
#include <string>
#include <unistd.h>
#include <fstream>
#include <time.h>
#include <boost/regex.hpp>
#include <boost/filesystem.hpp>
#include <boost/range/algorithm_ext/erase.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string.hpp>
#include <iostream>

namespace adk_impl
{
namespace lic
{
    typedef int32_t ErrorCode_def;

    enum ErrorCode {
        kSuccess = 0,
        kFailure,
    };

    // 获取进程的绝对路径
    static ErrorCode_def GetProcessAbsolutePath(std::string &path)
    {
        return ErrorCode::kSuccess;
    }

    // 执行Shell命令并获取结果
    static ErrorCode_def ExecuteShellAndGetResult(const std::string &cmd, std::string &result)
    {
        return ErrorCode::kSuccess;
    }

    /**
    * @breaf   获取当前进程下某个so的真实路径
    */
    static inline std::string GetLibraryPath(const std::string& so_name)
    {
        return std::string();
    }

    /**
    * @breaf   获取所有磁盘的UUID
    */
    static inline std::vector<std::string> GetDiskUUID()
    {
        std::vector<std::string> empty_vec;
        return empty_vec;
    }

    /**
    * @breaf   获取当前机器的所有磁盘的UUID
    * 
    * @return  当前机器磁盘UUID字符串数组
    */
    static inline std::vector<std::string> GetCurrentDiskUUID()
    {
        std::vector<std::string> empty_vec;
        return empty_vec;
    }

    /**
    * @breaf   获取当前机器标识ID
    * 
    * @return  当前机器Machine Id字符串
    */
    static inline std::string GetCurrentMachineID()
    {
        return "";
    }

    /**
    * @breaf   获取当前CPU Model Name
    * 
    * @return  当前机器CPU信息Model Name字符串
    */
    static inline std::string GetCurrentCpuName()
    {
        return "";
    }

    /**
    * @breaf   获取RDMA网卡的sys_image_guid
    */
    static inline std::vector<std::string> GetIBVGuid()
    {
        std::vector<std::string> empty_vec;
        return empty_vec;
    }

    /**
    * @breaf   获取当前时间
    *
    * @return uint64 epoch-format
    */
    static inline uint64_t CurrentTime()
    {
        return (uint64_t)time(0);
    }

    // 将秒 转换 标准时间字符串
    static inline std::string GetLocalTime(uint64_t time_point)
    {
        return std::string();
    }

    //human-readable time string to epoch
    // 标准时间字符串转换秒, time_str格式：YYYYMMDDHHMMSS
    static inline uint64_t ConvertTimeStr(const std::string& time_str)
    {
        return (uint64_t)0;
    }
}
}
#endif