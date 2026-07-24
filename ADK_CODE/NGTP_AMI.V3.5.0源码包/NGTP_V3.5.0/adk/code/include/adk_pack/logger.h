/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_LOGGER_H_
#define ADK_LOGGER_H_

#include <locale>
#include <string>
#include <sys/types.h>

namespace adk
{
namespace log
{

/**
 * @brief 日志级别
 */
typedef uint32_t LogLevel;

/**
 * @brief 日志代码
 */
typedef uint32_t LogCode;

/**
 * @brief 日志记录器抽象类
 *
 * 应用应实现一个类继承该类，实现Log方法，以提供日志记录功能。
 */
class Logger
{
public:
    static const uint32_t kLogLevelNum = 6;

    /**
     * @brief 获取时间字符串
     */
    static std::string TimeString();

    /**
     * @brief 获取级别字符串
     */
    static const char* LevelString(LogLevel level);

    /**
     * @brief 获取空格补齐的固定长度的级别字符串
     */
    static const char* FixLenLevelString(LogLevel level);

    /**
     * @brief 获取进程名
     */
    static const char* process_name();

    /**
     * @brief 获取进程ID
     */
    static pid_t pid();

    /**
     * @brief 获取线程ID
     */
    static pid_t tid();

    /**
     * @brief 更新进程及线程ID，调用ADK_LOG_FORK宏时将调用该函数
     */
    static void UpdatePidTid();

    /**
     * @brief 在终端输出日志
     *
     * @param level 日志级别
     * @param code 日志代码
     * @param module_name 模块名
     * @param funciton_name 函数名
     * @param src_line 代码行
     * @param message 日志文字
     */
    static void ConsoleLog(LogLevel level,
                           LogCode code,
                           const std::string& module_name,
                           const std::string& function_name,
                           uint32_t src_line,
                           const std::string& title,
                           const std::string& message);

    /**
     * @brief 构造函数
     */
    Logger();

    /**
     * @brief 析构函数
     */
    virtual ~Logger();

    /**
     * @brief 记录日志
     *
     * @param level 日志级别
     * @param code 日志代码
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
     *
     * 应用fork进程后，应在子进程中调用ADK_LOG_FORK宏，该宏将调用日志实现的该方法。
     * 日志实现可执行必要操作，如切换日志文件等
     */
    virtual void Fork();

    /**
     * @brief 最低日志记录级别，小于该级别的日志将不会记录
     *
     * @return 当前设置的最低日志记录级别
     */
    LogLevel min_log_level() const;

    /**
     * @brief 设置日志最低记录级别
     *
     * @param level 日志最低记录级别
     */
    void set_min_log_level(LogLevel min_log_level);

    static std::locale& locale();

    static void InstallLogger(Logger* nlogger);

private:
    LogLevel min_log_level_;    ///< 日志最低记录级别，默认为kInfo
};

} // namespace log
} // namespace adk

#endif /* ADK_LOGGER_H_ */
