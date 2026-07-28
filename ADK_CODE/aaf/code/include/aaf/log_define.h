#ifndef AAF_LOG_DEFINE_H_
#define AAF_LOG_DEFINE_H_

#include <adk/log.h>
#include <adk/exception.h>

namespace aaf
{

#define ADK_LOG_FATAL_STOP(code, title, message) {\
    ADK_LOG_FATAL(code, title, message); \
    ADK_THROW(std::string(title) + " | " + std::string(message)); }

#define ADK_LOG_FATAL_STOP_AC(title, message) {\
    ADK_LOG_FATAL_AC(title, message); \
    ADK_THROW(std::string(title) + " | " + std::string(message)); }

#define ADK_LOG_FATAL_STOP_TF(code, title, ...) {\
    ADK_LOG_FATAL_TF(code, title, __VA_ARGS__); \
    ADK_THROW(adk::log::_FormatLog(title) + " | " + adk::log::_FormatLog(__VA_ARGS__)); }

#define ADK_LOG_FATAL_STOP_AC_TF(title, ...) {\
    ADK_LOG_FATAL_AC_TF(title, __VA_ARGS__); \
    ADK_THROW(adk::log::_FormatLog(title) + " | " + adk::log::_FormatLog(__VA_ARGS__)); }

} 

#endif // AAF_LOG_DEFINE_H_
