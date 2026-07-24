/**
 * @file logger.h
 * @brief 日志记录器
 * @author Li Yunchong
 * @version 0.1
 * @date 2016-11-27
 */
#ifndef ADK_IMPL_LOGGER_H_
#define ADK_IMPL_LOGGER_H_

#include <adk/libadk.h>
#include <adk/arch/generic.h>

#include <string>
#include <locale>
#include <sys/types.h>

namespace adk_impl
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

#ifdef _MSC_VER
 /**
 * @brief 获取线程ID
 */
static pid_t tid()
{
    static thread_local pid_t tid_ = ADK_GET_TID;
    return tid_;
}
#endif

/**
 * @brief 日志记录器抽象类
 *
 * 应用应实现一个类继承该类，实现Log方法，以提供日志记录功能。
 */
class ADK_API Logger
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
    static const char* LevelString(LogLevel level)
    {
        return level_strings_[level < kLogLevelNum ? level : kLogLevelNum];
    }

    /**
     * @brief 获取空格补齐的固定长度的级别字符串
     */
    static const char* FixLenLevelString(LogLevel level)
    {
        return fix_len_level_strings_[level < kLogLevelNum ? level : kLogLevelNum];
    }

    /**
     * @brief 获取进程名
     */
    static const char* process_name()
    {
        return process_name_;
    }

    /**
     * @brief 获取进程ID
     */
    static pid_t pid()
    {
        return pid_;
    }

#ifdef __GNUC__
    /**
     * @brief 获取线程ID
     */
    static pid_t tid()
    {
        return tid_;
    }
#endif

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

    static void ConsoleLog(pid_t pid,
                           pid_t tid,
                           LogLevel level,
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
                     const std::string& message)
    {
        ConsoleLog(level, code, module_name, function_name, src_line, title, message);
    }

    virtual void Log(pid_t pid,
                     pid_t tid,
                     LogLevel level,
                     LogCode code,
                     const std::string& module_name,
                     const std::string& function_name,
                     uint32_t src_line,
                     const std::string& title,
                     const std::string& message)
    {
        ConsoleLog(pid, tid, level, code, module_name, function_name, src_line, title, message);
    }

    /**
     * @brief Fork进程
     *
     * 应用fork进程后，应在子进程中调用ADK_LOG_FORK宏，该宏将调用日志实现的该方法。
     * 日志实现可执行必要操作，如切换日志文件等
     */
    virtual void Fork()
    {
    }

    /**
     * @brief 最低日志记录级别，小于该级别的日志将不会记录
     *
     * @return 当前设置的最低日志记录级别
     */
    LogLevel min_log_level() const
    {
        return min_log_level_;
    }

    /**
     * @brief 设置日志最低记录级别
     *
     * @param level 日志最低记录级别
     */
    void set_min_log_level(LogLevel min_log_level)
    {
        min_log_level_ = min_log_level;
    }

    static std::locale& locale();

private:
    static char process_name_[1024];
    static pid_t pid_;
#ifdef __GNUC__
    static thread_local pid_t tid_;
#endif
    static const char* level_strings_[kLogLevelNum + 1];
    static const char* fix_len_level_strings_[kLogLevelNum + 1];

    LogLevel min_log_level_;    ///< 日志最低记录级别，默认为kInfo
};

} // namespace log

} // namespace adk_impl

#endif /* ADK_LOGGER_H_ */
