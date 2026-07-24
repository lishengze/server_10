#include <adk/log.h>
#include <adk_pack/logger.h>

namespace adk
{

namespace log
{

using LoggerImpl = adk_impl::log::Logger;

Logger::Logger()
{
    new ((void*)this) LoggerImpl();
}

Logger::~Logger()
{
}

std::string Logger::TimeString()
{
    return LoggerImpl::TimeString();
}

const char* Logger::LevelString(LogLevel level)
{
    return LoggerImpl::LevelString(level);
}

const char* Logger::FixLenLevelString(LogLevel level)
{
    return LoggerImpl::FixLenLevelString(level);
}

const char* Logger::process_name()
{
    return LoggerImpl::process_name();
}

pid_t Logger::pid()
{
    return LoggerImpl::pid();
}

pid_t Logger::tid()
{
    return LoggerImpl::tid();
}

void Logger::UpdatePidTid()
{
    LoggerImpl::UpdatePidTid();
}

void Logger::ConsoleLog(LogLevel level, LogCode code, const std::string& module_name,
    const std::string& function_name, uint32_t src_line, const std::string& title, const std::string& message)
{
    LoggerImpl::ConsoleLog(level, code, module_name, function_name, src_line, title, message);
}

void Logger::Log(LogLevel level, LogCode code, const std::string& module_name,
    const std::string& function_name, uint32_t src_line, const std::string& title, const std::string& message)
{
}

void Logger::Fork()
{
}

LogLevel Logger::min_log_level() const
{
    return reinterpret_cast<const LoggerImpl*>(this)->min_log_level();
}

void Logger::set_min_log_level(LogLevel min_log_level)
{
    reinterpret_cast<LoggerImpl*>(this)->set_min_log_level(min_log_level);
}

std::locale& Logger::locale()
{
    return LoggerImpl::locale();
}

Logger** g_logger = (Logger**)(&(adk_impl::log::g_logger));
LogLevel* g_log_min_level = &(adk_impl::log::g_log_min_level);

void Logger::InstallLogger(Logger* nlogger)
{
    adk_impl::log::g_logger = (LoggerImpl*)nlogger;
}
}

}