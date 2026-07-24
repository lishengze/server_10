/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/
#ifndef ADK_BOOST_LOGGER_H_
#define ADK_BOOST_LOGGER_H_

#include "logger.h"
#include <boost/log/sinks.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/common.hpp>

#if defined(ADK_LOG_USE_BOOST_LOG)
    #define ADK_LOG_INIT(...) \
        do { \
            ADK_LOG_USE_LOGGER(new adk::log::BoostLogger); \
            adk::log::BoostLogger::Init(__VA_ARGS__); \
        } while (false)
    #define ADK_LOG_FINISH() \
        do { \
            adk::log::BoostLogger::Finish(); \
        } while (false)
#endif

namespace adk
{

namespace log
{

class BoostLogger : public Logger
{
public:
    BoostLogger();

    ~BoostLogger();

    /**
     * @brief 初始化日志库
     *
     * @param log_path 日志保存路径
     * @param app_name 应用名
     * @param console_output 是否输出到控制台
     * @param console_filter 控制台日志是否过滤，为true则只显示Warn及以上级别日志
     * @param brief 是否使用简洁格式（不输出主机名、应用名、进程ID）
     * @param fork_new_file Fork进程时是否使用新文件
     * @param async_sink 是否异步输出日志，程序退出时应调用Finish，否则日志可能输出不完整
     * @param rotate_size 根据日志文件大小自动切换文件，0表示不切换
     * @param rotate_by_day 根据日期自动切换文件
     *
     * @return 无
     */
    static void Init(const boost::filesystem::path& log_dir,
                     const std::string& app_name,
                     bool console_output,
                     bool console_filter = false,
                     bool brief = false,
                     bool fork_new_file = false,
                     bool async_sink = false,
                     uint64_t rotate_size = 0,
                     bool rotate_by_day = true);

    /**
     * @brief 记录日志
     *
     * @param level 日志级别
     * @param code 日志代码
     * @param module_name 模块名
     * @param funciton_name 函数名
     * @param src_line 代码行
     * @param message 日志文字
     */
    virtual void Log(LogLevel level,
                     LogCode code,
                     const std::string& module_name,
                     const std::string& function_name,
                     uint32_t src_line,
                     const std::string& title,
                     const std::string& message);

    /**
     * @brief Fork进程
     */
    virtual void Fork();

    /**
     * @brief 等待异步日志输出结束
     */
    static void Finish();
};

} // namespace log

} // namespace adk

#endif /* ADK_BOOST_LOGGER_H_ */
