/**
 * @file logger.cpp
 * @brief 日志记录器
 * @author Li Yunchong
 * @version 0.1
 * @date 2017-02-23
 */

#ifdef __GNUC__
#include <unistd.h>
#include <sys/syscall.h>
#endif

#include <mutex>
#include <boost/locale.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <adk/log.h>
#include <adk/logger.h>
#include <adk/arch/generic.h>

namespace adk_impl
{

namespace log
{

char Logger::process_name_[1024];
pid_t Logger::pid_ = ADK_GET_PID;
#ifdef __GNUC__
thread_local pid_t Logger::tid_ = ADK_GET_TID;
#endif
const char* Logger::level_strings_[] = {"Trace", "Debug", "Info",
                                        "Warn", "Error", "Fatal", "N/A"};
const char* Logger::fix_len_level_strings_[] = {"Trace", "Debug", "Info ",
                                                "Warn ", "Error", "Fatal", "N/A  "};
// std::locale Logger::locale_;

Logger* g_logger;
LogLevel g_log_min_level = ADK_LOG_LEVEL_TRACE;

static std::mutex& s_mutex()
{
    static std::mutex* s_mutex_lock = new std::mutex();
    return *s_mutex_lock;
}


std::locale* LocaleInst()
{
    boost::locale::generator gen;
    const char* path = getenv("TRANSLATER_PATH");
    if (!path)
    {
        path = ".";
    }
    gen.add_messages_path(path);
    gen.add_messages_domain("log");
    return new std::locale(std::locale(gen("zh_CN.UTF-8")),
                           new boost::posix_time::time_facet("%Y-%m-%d %H:%M:%s"));
}

std::locale& Logger::locale()
{
    static std::locale* locale = LocaleInst();
    return *locale;
}

std::string Logger::TimeString()
{
    std::lock_guard<std::mutex> lock(s_mutex());
    std::ostringstream oss;
    oss.imbue(Logger::locale());
    oss << boost::posix_time::microsec_clock::local_time();
    return oss.str();
}

void Logger::UpdatePidTid()
{
    pid_ = ADK_GET_PID;
#ifdef __GNUC__
    tid_ = ADK_GET_TID;
#endif
}

Logger::Logger()
    : min_log_level_(ADK_LOG_LEVEL_INFO)
{
}

Logger::~Logger()
{
}

void Logger::ConsoleLog(LogLevel level,
                        LogCode code,
                        const std::string& module_name,
                        const std::string& function_name,
                        uint32_t src_line,
                        const std::string& title,
                        const std::string& message)
{
    ConsoleLog(pid(), 
               tid(),
               level, 
               code, 
               module_name, 
               function_name, 
               src_line, 
               title, 
               message);
}


void Logger::ConsoleLog(pid_t pid,
                        pid_t tid,
                        LogLevel level,
                        LogCode code,
                        const std::string& module_name,
                        const std::string& function_name,
                        uint32_t src_line,
                        const std::string& title,
                        const std::string& message)
{
    static bool init = false;
    std::lock_guard<std::mutex> lock(s_mutex());
    if (ADK_UNLIKELY(!init))
    {
        std::cerr.imbue(std::locale(std::cerr.getloc(),
                                    new boost::posix_time::time_facet("%Y-%m-%d %H:%M:%s")));

        char app_path[1024] = {0};
#if defined(__GNUC__)
        if (readlink("/proc/self/exe", app_path, 1024) > 0)
        {
            strncpy(process_name_, strrchr(app_path, '/') + 1, 1023);
            process_name_[1023] = '\0';
        }
        else
#endif
        {
            strcpy(process_name_, "_");
        }
        init = true;
    }

    std::cerr << "@ " << boost::posix_time::microsec_clock::local_time()
              << ' ' << process_name_ << ' ' << pid << ' ' << tid
              << ' ' << level << ' ' << LevelString(level)
              << ' ' << module_name << ' ' << function_name << ' ' << src_line
              << ' ' << code << " | " << title << " | " << message << std::endl;
}

} // namespace log
} // namespace adk_impl
