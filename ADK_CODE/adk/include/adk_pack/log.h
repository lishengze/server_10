/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_LOG_H_
#define ADK_LOG_H_

#include "i18n.h"
#include "logger.h"

#include <chrono>
#include <boost/preprocessor.hpp>
#include <boost/locale/format.hpp>
#include <boost/locale/message.hpp>

// 编译开关：使用boost log作为日志实现
// 定义该宏则不需要自定义日志实现类
#if defined(ADK_LOG_USE_BOOST_LOG)
#include "boost_logger.h"
#endif

// 编译开关：编译时日志级别下限
// 低于ADK_LOG_THRESHOLD级别的日志输出语句将不会被编译在目标代码中
#if !defined(ADK_LOG_THRESHOLD)
    #define ADK_LOG_THRESHOLD ADK_LOG_LEVEL_TRACE
#endif

// 编译开关：日志格式化参数数量上限
// _TF系列宏使用的参数数量不能超过该限制
#if !defined(ADK_LOG_PARAM_NUM_LIMIT)
    #define ADK_LOG_PARAM_NUM_LIMIT 20
#endif

// 日志级别常量
#ifndef ADK_LOG_LEVEL_TRACE
#define ADK_LOG_LEVEL_TRACE 0
#endif

#ifndef ADK_LOG_LEVEL_DEBUG
#define ADK_LOG_LEVEL_DEBUG 1
#endif

#ifndef ADK_LOG_LEVEL_INFO
#define ADK_LOG_LEVEL_INFO 2
#endif

#ifndef ADK_LOG_LEVEL_WARN
#define ADK_LOG_LEVEL_WARN 3
#endif

#ifndef ADK_LOG_LEVEL_ERROR
#define ADK_LOG_LEVEL_ERROR 4
#endif

#ifndef ADK_LOG_LEVEL_FATAL
#define ADK_LOG_LEVEL_FATAL 5
#endif

// 使用自定义日志实现
// 应用可继承adk::Logger类实现自定义日志实现类。
// 如果未使用日志实现，日志将以默认格式输出到标准输出。
// @param logger 日志实现类指针
#ifndef ADK_LOG_USE_LOGGER
#define ADK_LOG_USE_LOGGER(logger) \
    do { \
        if (logger) { \
            adk::log::Logger::InstallLogger(logger);  \
            (*adk::log::g_logger)->set_min_log_level(*adk::log::g_log_min_level); \
        } \
    } while(false)
#endif // !ADK_LOG_USE_LOGGER

// 设置运行时日志级别下限
// @param level 日志级别下限，低于该级别的日志不会输出
#ifndef ADK_LOG_SET_THRESHOLD
#define ADK_LOG_SET_THRESHOLD(level) \
    do { \
        *adk::log::g_log_min_level = static_cast<adk::log::LogLevel>(level); \
        if ((*adk::log::g_logger)) \
            (*adk::log::g_logger)->set_min_log_level(level); \
    } while (false)
#endif // !ADK_LOG_SET_THRESHOLD

// 日志接口声明
// 需要输出非自动编号日志的类应在类声明中使用该宏，建议声明为private
#ifndef ADK_LOG_DECLARE
#define ADK_LOG_DECLARE() \
    static const std::string _module_name;
#endif // !ADK_LOG_DECLARE

// 自动编号日志接口声明
// 需要使用输出自动编号日志的类应在类声明中使用该宏，建议声明为private
// 如果在头文件中输出日志，则该声明应位于第一条日志输出语句之前
// @param log_code_base 日志起始编号
#ifndef ADK_LOG_DECLARE_AC
#define ADK_LOG_DECLARE_AC(log_code_base) \
    ADK_LOG_DECLARE() \
    static const int32_t _log_code_base = log_code_base - __COUNTER__ - 1;
#endif // !ADK_LOG_DECLARE_AC

// 日志接口定义
// 与ADK_LOG_DECLARE/ADK_LOG_DECLARE_AC配合使用，需要输出日志的类应在cpp文件中使用该宏
// @param class_name 需要输出日志的类名
#ifndef ADK_LOG_DEFINE
#define ADK_LOG_DEFINE(class_name) \
    const std::string class_name::_module_name = #class_name;
#endif // !ADK_LOG_DEFINE

// 类模板日志接口定义
// 与ADK_LOG_DECLARE/ADK_LOG_DECLARE_AC配合使用，需要输出日志的类模板应在cpp文件中使用该宏
// 举例：类模板
//     template <typename T, int n> class Foo {...};
//   应采用以下形式定义日志接口：
//     template <typename T, int n> ADK_LOG_DEFINE_TMPL(Foo, T, n);
// @param class_name 需要输出日志的类名
// @param ... 类模板形参
#ifndef ADK_LOG_DEFINE_TMPL
#define ADK_LOG_DEFINE_TMPL(class_name, ...) \
    const std::string class_name<__VA_ARGS__>::_module_name = #class_name;
#endif // !ADK_LOG_DEFINE_TMPL

// 日志接口声明/定义
// 需要输出非自动编号日志的非类代码（如全局函数），可使用该宏声明/定义日志接口
// @param module_name 日志输出模块名
#ifndef ADK_LOG_LOCAL
#define ADK_LOG_LOCAL(module_name) \
    static const std::string _module_name = module_name;
#endif // !ADK_LOG_LOCAL

// 自动编号日志接口声明/定义
// 需要输出自动编号日志的非类代码（如全局函数），可使用该宏声明/定义日志接口
// @param module_name   日志输出模块名
// @param log_code_base 日志起始编号
#ifndef ADK_LOG_LOCAL_AC
#define ADK_LOG_LOCAL_AC(module_name, log_code_base) \
    ADK_LOG_LOCAL(module_name) \
    static const int32_t _log_code_base = log_code_base - __COUNTER__ - 1;
#endif // !ADK_LOG_LOCAL_AC

// Fork进程
// 应用执行fork进程后，应在子进程中调用该宏，通知日志实现执行必要操作
#ifndef ADK_LOG_FORK
#define ADK_LOG_FORK() \
    do { \
        adk::log::Logger::UpdatePidTid(); \
        if ((*adk::log::g_logger)) \
            (*adk::log::g_logger)->Fork(); \
    } while (false)
#endif // !ADK_LOG_FORK

// 原始日志输出
// 除非用于为第三方代码实现日志接口，否则不建议直接使用该宏
// @param level       日志级别
// @param code        日志代码
// @param module_name 模块名
// @param function    函数名
// @param line        代码行数
// @param message     日志正文
#ifndef ADK_LOG_RAW
#define ADK_LOG_RAW(level, code, module_name, function, line, title, message) \
    do { \
        if ((*adk::log::g_logger)) {\
            if ((*adk::log::g_logger)->min_log_level() <= level) \
                (*adk::log::g_logger)->Log(level, code, module_name, function, line, \
                                       title, message); \
        } else \
            if (*adk::log::g_log_min_level <= level) \
                adk::log::Logger::ConsoleLog(level, code, module_name, function, line, \
                                             title, message); \
    } while (false)
#endif // !ADK_LOG_RAW

// 通用日志输出
// 除非日志级别为变量，否则不建议应用直接使用该宏
// @param level   日志级别
// @param code    日志代码
// @param message 日志正文
#ifndef ADK_LOG
#if defined(ADK_LOG_OLD_INTERFACE)
#define ADK_LOG(level, code, title, message) \
    ADK_LOG_RAW(level, code, _module_name, __FUNCTION__, __LINE__, title, \
                adk::log::_ToString(message))
#else
#define ADK_LOG(level, code, title, message) \
    ADK_LOG_RAW(level, code, _module_name, __FUNCTION__, __LINE__, title, message)
#endif
#endif // !ADK_LOG

// 通用日志格式化输出
// 除非日志级别为变量，否则不建议应用直接使用该宏
// @param level 日志级别
// @param code  日志代码
// @param ...   日志正文格式化字符串及参数
#ifndef ADK_LOG_TF
#define ADK_LOG_TF(level, code, title, ...) \
    ADK_LOG(level, code, adk::log::_FormatLog(title), adk::log::_FormatLog(__VA_ARGS__))
#endif // !ADK_LOG_TF

#ifndef ADK_LOG_RAW_TF
#define ADK_LOG_RAW_TF(level, code, module_name, function, line, title, message) \
    ADK_LOG_RAW(level, code, module_name, function, line, adk::log::_FormatLog(title), \
                adk::log::_FormatLog(message))
#endif // !ADK_LOG_RAW_TF

#ifndef _ADK_LOG_AUTO_CODE
#define _ADK_LOG_AUTO_CODE (_log_code_base + __COUNTER__)
#endif // !_ADK_LOG_AUTO_CODE

#ifndef _ADK_LOG_NULL
#define _ADK_LOG_NULL __COUNTER__
#endif // !_ADK_LOG_NULL

// 按级别输出日志
//
// ADK_LOG_xxx(code, title, message)
// 输出xxx级别日志
// @param code    日志代码
// @param message 日志正文
//
// ADK_LOG_xxx_AC(title, message)
// 输出xxx级别自动编码日志
// 每条日志输出语句会自动分配一个递增的日志代码，日志代码从ADK_LOG_DECLARE_AC或
//   ADK_LOG_LOCAL_AC中使用的log_code_base开始编号，不保证连续性
// @param message 日志正文
//
// ADK_LOG_xxx_TF(code, title, ...)
// 输出xxx级别日志，支持翻译及格式化（翻译暂时未实现）
// 举例：
//   ADK_LOG_ERROR_TF(10001, "Read file {1} failed, error: {2}", file_name, error_desc);
// @param code 日志代码
// @param ...  日志格式字符串及参数
//
// ADK_LOG_xxx_AC_TF(title, ...)
// 输出xxx级别自动编码日志，支持翻译及格式化（翻译暂时未实现）
// @param ...  日志格式字符串及参数
#if ADK_LOG_THRESHOLD <= ADK_LOG_LEVEL_TRACE
    #define ADK_LOG_TRACE(code, title, message) ADK_LOG(ADK_LOG_LEVEL_TRACE, code, title, message)
    #define ADK_LOG_TRACE_AC(title, message) ADK_LOG_TRACE(_ADK_LOG_AUTO_CODE, title, message)
    #define ADK_LOG_TRACE_TF(code, title, ...) ADK_LOG_TF(ADK_LOG_LEVEL_TRACE, code, title, __VA_ARGS__)
    #define ADK_LOG_TRACE_AC_TF(title, ...) ADK_LOG_TRACE_TF(_ADK_LOG_AUTO_CODE, title, __VA_ARGS__)
#else
    #define ADK_LOG_TRACE(code, title, message)
    #define ADK_LOG_TRACE_AC(title, message) _ADK_LOG_NULL
    #define ADK_LOG_TRACE_TF(code, title, ...)
    #define ADK_LOG_TRACE_AC_TF(title, ...) _ADK_LOG_NULL
#endif

#if ADK_LOG_THRESHOLD <= ADK_LOG_LEVEL_DEBUG
    #define ADK_LOG_DEBUG(code, title, message) ADK_LOG(ADK_LOG_LEVEL_DEBUG, code, title, message)
    #define ADK_LOG_DEBUG_AC(title, message) ADK_LOG_DEBUG(_ADK_LOG_AUTO_CODE, title, message)
    #define ADK_LOG_DEBUG_TF(code, title, ...) ADK_LOG_TF(ADK_LOG_LEVEL_DEBUG, code, title, __VA_ARGS__)
    #define ADK_LOG_DEBUG_AC_TF(title, ...) ADK_LOG_DEBUG_TF(_ADK_LOG_AUTO_CODE, title, __VA_ARGS__)
#else
    #define ADK_LOG_DEBUG(code, title, message)
    #define ADK_LOG_DEBUG_AC(title, message) _ADK_LOG_NULL
    #define ADK_LOG_DEBUG_TF(code, title, ...)
    #define ADK_LOG_DEBUG_AC_TF(title, ...) _ADK_LOG_NULL
#endif

#if ADK_LOG_THRESHOLD <= ADK_LOG_LEVEL_INFO
    #define ADK_LOG_INFO(code, title, message) ADK_LOG(ADK_LOG_LEVEL_INFO, code, title, message)
    #define ADK_LOG_INFO_AC(title, message) ADK_LOG_INFO(_ADK_LOG_AUTO_CODE, title, message)
    #define ADK_LOG_INFO_TF(code, title, ...) ADK_LOG_TF(ADK_LOG_LEVEL_INFO, code, title, __VA_ARGS__)
    #define ADK_LOG_INFO_AC_TF(title, ...) ADK_LOG_INFO_TF(_ADK_LOG_AUTO_CODE, title, __VA_ARGS__)
#else
    #define ADK_LOG_INFO(code, title, message)
    #define ADK_LOG_INFO_AC(title, message) _ADK_LOG_NULL
    #define ADK_LOG_INFO_TF(code, title, ...)
    #define ADK_LOG_INFO_AC_TF(title, ...) _ADK_LOG_NULL
#endif

#if ADK_LOG_THRESHOLD <= ADK_LOG_LEVEL_WARN
    #define ADK_LOG_WARN(code, title, message) ADK_LOG(ADK_LOG_LEVEL_WARN, code, title, message)
    #define ADK_LOG_WARN_AC(title, message) ADK_LOG_WARN(_ADK_LOG_AUTO_CODE, title, message)
    #define ADK_LOG_WARN_TF(code, title, ...) ADK_LOG_TF(ADK_LOG_LEVEL_WARN, code, title, __VA_ARGS__)
    #define ADK_LOG_WARN_AC_TF(title, ...) ADK_LOG_WARN_TF(_ADK_LOG_AUTO_CODE, title, __VA_ARGS__)
#else
    #define ADK_LOG_WARN(code, title, message)
    #define ADK_LOG_WARN_AC(title, message) _ADK_LOG_NULL
    #define ADK_LOG_WARN_TF(code, title, ...)
    #define ADK_LOG_WARN_AC_TF(title, ...) _ADK_LOG_NULL
#endif

#if ADK_LOG_THRESHOLD <= ADK_LOG_LEVEL_ERROR
    #define ADK_LOG_ERROR(code, title, message) ADK_LOG(ADK_LOG_LEVEL_ERROR, code, title, message)
    #define ADK_LOG_ERROR_AC(title, message) ADK_LOG_ERROR(_ADK_LOG_AUTO_CODE, title, message)
    #define ADK_LOG_ERROR_TF(code, title, ...) ADK_LOG_TF(ADK_LOG_LEVEL_ERROR, code, title, __VA_ARGS__)
    #define ADK_LOG_ERROR_AC_TF(title, ...) ADK_LOG_ERROR_TF(_ADK_LOG_AUTO_CODE, title, __VA_ARGS__)
#else
    #define ADK_LOG_ERROR(code, title, message)
    #define ADK_LOG_ERROR_AC(title, message) _ADK_LOG_NULL
    #define ADK_LOG_ERROR_TF(code, title, ...)
    #define ADK_LOG_ERROR_AC_TF(title, ...) _ADK_LOG_NULL
#endif

#if ADK_LOG_THRESHOLD <= ADK_LOG_LEVEL_FATAL
    #define ADK_LOG_FATAL(code, title, message) ADK_LOG(ADK_LOG_LEVEL_FATAL, code, title, message)
    #define ADK_LOG_FATAL_AC(title, message) ADK_LOG_FATAL(_ADK_LOG_AUTO_CODE, title, message)
    #define ADK_LOG_FATAL_TF(code, title, ...) ADK_LOG_TF(ADK_LOG_LEVEL_FATAL, code, title, __VA_ARGS__)
    #define ADK_LOG_FATAL_AC_TF(title, ...) ADK_LOG_FATAL_TF(_ADK_LOG_AUTO_CODE, title, __VA_ARGS__)
#else
    #define ADK_LOG_FATAL(code, title, message)
    #define ADK_LOG_FATAL_AC(title, message) _ADK_LOG_NULL
    #define ADK_LOG_FATAL_TF(code, title, ...)
    #define ADK_LOG_FATAL_AC_TF(title, ...) _ADK_LOG_NULL
#endif

#ifndef _ADK_LOG_MOD_PARAM
#define _ADK_LOG_MOD_PARAM(z, n, data) % data##n
#endif // !_ADK_LOG_MOD_PARAM

#ifndef _ADK_LOG_FORMAT_LOG
#define _ADK_LOG_FORMAT_LOG(z, n, data) \
    template <BOOST_PP_ENUM_PARAMS(BOOST_PP_INC(n), typename T)> \
    inline std::string _FormatLog( \
            const char* message, \
            BOOST_PP_ENUM_BINARY_PARAMS(BOOST_PP_INC(n), const T, &t)) \
    { \
        return (boost::locale::format(ADK_TRANSLATE(message)) \
                BOOST_PP_REPEAT(BOOST_PP_INC(n), _ADK_LOG_MOD_PARAM, t)).str(); \
    } \
    template <BOOST_PP_ENUM_PARAMS(BOOST_PP_INC(n), typename T)> \
    inline std::string _FormatLog( \
            const std::string& message, \
            BOOST_PP_ENUM_BINARY_PARAMS(BOOST_PP_INC(n), const T, &t)) \
    { \
        return (boost::locale::format(ADK_TRANSLATE(message)) \
                BOOST_PP_REPEAT(BOOST_PP_INC(n), _ADK_LOG_MOD_PARAM, t)).str(); \
    }
#endif // !_ADK_LOG_FORMAT_LOG

/******************************************************************************
 * 强制隔一段时间才会打一次的日志
 */
#if ADK_LOG_THRESHOLD <= ADK_LOG_LEVEL_TRACE
#ifndef ADK_INV_LOG_TRACE
#define ADK_INV_LOG_TRACE(inv_logger, code, title, message)     \
    do {                                                \
        if (inv_logger.ToLog())                         \
            ADK_LOG(ADK_LOG_LEVEL_TRACE, code, title, message); \
    }while(false)
#endif // !ADK_INV_LOG_TRACE

#ifndef ADK_INV_LOG_TRACE_AC
#define ADK_INV_LOG_TRACE_AC(inv_logger, title, message)        \
    do {                                                \
        if (inv_logger.ToLog())                         \
            ADK_LOG_TRACE(_ADK_LOG_AUTO_CODE, title, message);  \
    }while(false)
#endif // !ADK_INV_LOG_TRACE_AC

#ifndef ADK_INV_LOG_TRACE_TF
#define ADK_INV_LOG_TRACE_TF(inv_logger, code, title, ...)              \
    do {                                                        \
        if (inv_logger.ToLog())                                 \
            ADK_LOG_TF(ADK_LOG_LEVEL_TRACE, code, title, __VA_ARGS__);  \
    }while(false)
#endif // !ADK_INV_LOG_TRACE_TF

#ifndef ADK_INV_LOG_TRACE_AC_TF
#define ADK_INV_LOG_TRACE_AC_TF(inv_logger, title, ...)                 \
    do {                                                        \
        if (inv_logger.ToLog())                                 \
            ADK_LOG_TRACE_TF(_ADK_LOG_AUTO_CODE, title, __VA_ARGS__);   \
    }while(false)
#endif // !ADK_INV_LOG_TRACE_AC_TF

#else
#ifndef ADK_INV_LOG_TRACE
    #define ADK_INV_LOG_TRACE(code, title, message)
#endif // !ADK_INV_LOG_TRACE

#ifndef ADK_INV_LOG_TRACE_AC
    #define ADK_INV_LOG_TRACE_AC(title, message) _ADK_LOG_NULL
#endif // !ADK_INV_LOG_TRACE_AC

#ifndef ADK_INV_LOG_TRACE_TF
    #define ADK_INV_LOG_TRACE_TF(code, title, ...)
#endif // !ADK_INV_LOG_TRACE_TF

#ifndef ADK_INV_LOG_TRACE_AC_TF
    #define ADK_INV_LOG_TRACE_AC_TF(title, ...) _ADK_LOG_NULL
#endif // !ADK_INV_LOG_TRACE_AC_TF
#endif

#if ADK_LOG_THRESHOLD <= ADK_LOG_LEVEL_DEBUG
#define ADK_INV_LOG_DEBUG(inv_logger, code, title, message)     \
    do {                                                \
        if (inv_logger.ToLog())                         \
            ADK_LOG(ADK_LOG_LEVEL_DEBUG, code, title, message); \
    }while(false)

#define ADK_INV_LOG_DEBUG_AC(inv_logger, title, message)        \
    do {                                                \
        if (inv_logger.ToLog())                         \
            ADK_LOG_DEBUG(_ADK_LOG_AUTO_CODE, title, message);  \
    }while(false)

#define ADK_INV_LOG_DEBUG_TF(inv_logger, code, title, ...)              \
    do {                                                        \
        if (inv_logger.ToLog())                                 \
            ADK_LOG_TF(ADK_LOG_LEVEL_DEBUG, code, title, __VA_ARGS__);  \
    }while(false)

#define ADK_INV_LOG_DEBUG_AC_TF(inv_logger, title, ...)                 \
    do {                                                        \
        if (inv_logger.ToLog())                                 \
            ADK_LOG_DEBUG_TF(_ADK_LOG_AUTO_CODE, title, __VA_ARGS__);   \
    }while(false)
#else
    #define ADK_INV_LOG_DEBUG(code, title, message)
    #define ADK_INV_LOG_DEBUG_AC(title, message) _ADK_LOG_NULL
    #define ADK_INV_LOG_DEBUG_TF(code, title, ...)
    #define ADK_INV_LOG_DEBUG_AC_TF(title, ...) _ADK_LOG_NULL
#endif

#if ADK_LOG_THRESHOLD <= ADK_LOG_LEVEL_INFO
#define ADK_INV_LOG_INFO(inv_logger, code, title, message)     \
    do {                                                \
        if (inv_logger.ToLog())                         \
            ADK_LOG(ADK_LOG_LEVEL_INFO, code, title, message); \
    }while(false)

#define ADK_INV_LOG_INFO_AC(inv_logger, title, message)        \
    do {                                                \
        if (inv_logger.ToLog())                         \
            ADK_LOG_INFO(_ADK_LOG_AUTO_CODE, title, message);  \
    }while(false)

#define ADK_INV_LOG_INFO_TF(inv_logger, code, title, ...)              \
    do {                                                        \
        if (inv_logger.ToLog())                                 \
            ADK_LOG_TF(ADK_LOG_LEVEL_INFO, code, title, __VA_ARGS__);  \
    }while(false)

#define ADK_INV_LOG_INFO_AC_TF(inv_logger, title, ...)                 \
    do {                                                        \
        if (inv_logger.ToLog())                                 \
            ADK_LOG_INFO_TF(_ADK_LOG_AUTO_CODE, title, __VA_ARGS__);   \
    }while(false)
#else
    #define ADK_INV_LOG_INFO(code, title, message)
    #define ADK_INV_LOG_INFO_AC(title, message) _ADK_LOG_NULL
    #define ADK_INV_LOG_INFO_TF(code, title, ...)
    #define ADK_INV_LOG_INFO_AC_TF(title, ...) _ADK_LOG_NULL
#endif

#if ADK_LOG_THRESHOLD <= ADK_LOG_LEVEL_WARN
#define ADK_INV_LOG_WARN(inv_logger, code, title, message)     \
    do {                                                \
        if (inv_logger.ToLog())                         \
            ADK_LOG(ADK_LOG_LEVEL_WARN, code, title, message); \
    }while(false)

#define ADK_INV_LOG_WARN_AC(inv_logger, title, message)        \
    do {                                                \
        if (inv_logger.ToLog())                         \
            ADK_LOG_WARN(_ADK_LOG_AUTO_CODE, title, message);  \
    }while(false)

#define ADK_INV_LOG_WARN_TF(inv_logger, code, title, ...)              \
    do {                                                        \
        if (inv_logger.ToLog())                                 \
            ADK_LOG_TF(ADK_LOG_LEVEL_WARN, code, title, __VA_ARGS__);  \
    }while(false)

#define ADK_INV_LOG_WARN_AC_TF(inv_logger, title, ...)                 \
    do {                                                        \
        if (inv_logger.ToLog())                                 \
            ADK_LOG_WARN_TF(_ADK_LOG_AUTO_CODE, title, __VA_ARGS__);   \
    }while(false)
#else
    #define ADK_INV_LOG_WARN(code, title, message)
    #define ADK_INV_LOG_WARN_AC(title, message) _ADK_LOG_NULL
    #define ADK_INV_LOG_WARN_TF(code, title, ...)
    #define ADK_INV_LOG_WARN_AC_TF(title, ...) _ADK_LOG_NULL
#endif

#if ADK_LOG_THRESHOLD <= ADK_LOG_LEVEL_ERROR
#define ADK_INV_LOG_ERROR(inv_logger, code, title, message)     \
    do {                                                \
        if (inv_logger.ToLog())                         \
            ADK_LOG(ADK_LOG_LEVEL_ERROR, code, title, message); \
    }while(false)

#define ADK_INV_LOG_ERROR_AC(inv_logger, title, message)        \
    do {                                                \
        if (inv_logger.ToLog())                         \
            ADK_LOG_ERROR(_ADK_LOG_AUTO_CODE, title, message);  \
    }while(false)

#define ADK_INV_LOG_ERROR_TF(inv_logger, code, title, ...)              \
    do {                                                        \
        if (inv_logger.ToLog())                                 \
            ADK_LOG_TF(ADK_LOG_LEVEL_ERROR, code, title, __VA_ARGS__);  \
    }while(false)

#define ADK_INV_LOG_ERROR_AC_TF(inv_logger, title, ...)                 \
    do {                                                        \
        if (inv_logger.ToLog())                                 \
            ADK_LOG_ERROR_TF(_ADK_LOG_AUTO_CODE, title, __VA_ARGS__);   \
    }while(false)
#else
    #define ADK_INV_LOG_ERROR(code, title, message)
    #define ADK_INV_LOG_ERROR_AC(title, message) _ADK_LOG_NULL
    #define ADK_INV_LOG_ERROR_TF(code, title, ...)
    #define ADK_INV_LOG_ERROR_AC_TF(title, ...) _ADK_LOG_NULL
#endif

#if ADK_LOG_THRESHOLD <= ADK_LOG_LEVEL_FATAL
#define ADK_INV_LOG_FATAL(inv_logger, code, title, message)     \
    do {                                                \
        if (inv_logger.ToLog())                         \
            ADK_LOG(ADK_LOG_LEVEL_FATAL, code, title, message); \
    }while(false)

#define ADK_INV_LOG_FATAL_AC(inv_logger, title, message)        \
    do {                                                \
        if (inv_logger.ToLog())                         \
            ADK_LOG_FATAL(_ADK_LOG_AUTO_CODE, title, message);  \
    }while(false)

#define ADK_INV_LOG_FATAL_TF(inv_logger, code, title, ...)              \
    do {                                                        \
        if (inv_logger.ToLog())                                 \
            ADK_LOG_TF(ADK_LOG_LEVEL_FATAL, code, title, __VA_ARGS__);  \
    }while(false)

#define ADK_INV_LOG_FATAL_AC_TF(inv_logger, title, ...)                 \
    do {                                                        \
        if (inv_logger.ToLog())                                 \
            ADK_LOG_FATAL_TF(_ADK_LOG_AUTO_CODE, title, __VA_ARGS__);   \
    }while(false)
#else
    #define ADK_INV_LOG_FATAL(code, title, message)
    #define ADK_INV_LOG_FATAL_AC(title, message) _ADK_LOG_NULL
    #define ADK_INV_LOG_FATAL_TF(code, title, ...)
    #define ADK_INV_LOG_FATAL_AC_TF(title, ...) _ADK_LOG_NULL
#endif
/******************************************************************************/

namespace adk
{

namespace log
{

inline std::string _FormatLog(const char* message)
{
    return ADK_TRANSLATE(message);
}

inline std::string _FormatLog(const std::string& message)
{
    return ADK_TRANSLATE(message);
}

BOOST_PP_REPEAT(ADK_LOG_PARAM_NUM_LIMIT, _ADK_LOG_FORMAT_LOG, BOOST_PP_NIL)

extern Logger** g_logger;

extern LogLevel* g_log_min_level;

#ifdef ADK_LOG_OLD_INTERFACE

enum SeverityLevel
{
    kMin = ADK_LOG_LEVEL_TRACE,
    kTrace = ADK_LOG_LEVEL_TRACE,
    kDebug = ADK_LOG_LEVEL_DEBUG,
    kInfo = ADK_LOG_LEVEL_INFO,
    kWarn = ADK_LOG_LEVEL_WARN,
    kError = ADK_LOG_LEVEL_ERROR,
    kFatal = ADK_LOG_LEVEL_FATAL,
    kMax = kFatal,
};

inline const std::string& _ToString(const std::string& str)
{
    return str;
}

inline const char* _ToString(const char* str)
{
    return str;
}

template <typename T>
inline std::string _ToString(const T& t)
{
    std::ostringstream oss;
    oss << t;
    return oss.str();
}

#endif

/**
 * 间隔一段时间才会实际打日志的日志类
 *
 * @par example
 *
 * IntervalLogger logger;
 * ADK_INV_LOG_TRACE(logger, 1, "hello, world");
 * ADK_INV_LOG_DEBUG_AC(logger, "hello, world");
 * ADK_INV_LOG_INFO_TF(logger, 1 "hello, {1}", "world");
 * ADK_INV_LOG_WARN_AC_TF(logger, "hello, {1}", "world");
 */
class IntervalLogger
{
public:
    /**
     * 默认间隔为1s打日志
     */
    explicit IntervalLogger(size_t seconds = 1);
    ~IntervalLogger();

    bool ToLog();
private:
    void* interval_log_impl_;
};

} // namespace log
} // namespace adk

// 编译开关：使用旧版本兼容接口
#ifdef ADK_LOG_OLD_INTERFACE

#define LOG_INIT(...)           ADK_LOG_INIT(__VA_ARGS__)
#define LOG_FINISH()            ADK_LOG_FINISH()
#define LOG_SET_THRESHOLD(x)    ADK_LOG_SET_THRESHOLD(x)
#define LOG_DECLARE()           ADK_LOG_DECLARE()
#define LOG_DECLARE_AC(x)       ADK_LOG_DECLARE_AC(x)
#define LOG_DEFINE(x)           ADK_LOG_DEFINE(x)
#define LOG_DEFINE_TMPL(x, ...) ADK_LOG_DEFINE_TMPL(x, __VA_ARGS__)
#define LOG_LOCAL(x)            ADK_LOG_LOCAL(x)
#define LOG_LOCAL_AC(x, y)      ADK_LOG_LOCAL_AC(x, y)
#define LOG_TRACE(x, y)         ADK_LOG_TRACE(x, y)
#define LOG_TRACE_AC(x)         ADK_LOG_TRACE_AC(x)
#define LOG_TRACE_TF(x, ...)    ADK_LOG_TRACE_TF(x, __VA_ARGS__)
#define LOG_TRACE_AC_TF(...)    ADK_LOG_TRACE_AC_TF(__VA_ARGS__)
#define LOG_DEBUG(x, y)         ADK_LOG_DEBUG(x, y)
#define LOG_DEBUG_AC(x)         ADK_LOG_DEBUG_AC(x)
#define LOG_DEBUG_TF(x, ...)    ADK_LOG_DEBUG_TF(x, __VA_ARGS__)
#define LOG_DEBUG_AC_TF(...)    ADK_LOG_DEBUG_AC_TF(__VA_ARGS__)
#define LOG_INFO(x, y)          ADK_LOG_INFO(x, y)
#define LOG_INFO_AC(x)          ADK_LOG_INFO_AC(x)
#define LOG_INFO_TF(x, ...)     ADK_LOG_INFO_TF(x, __VA_ARGS__)
#define LOG_INFO_AC_TF(...)     ADK_LOG_INFO_AC_TF(__VA_ARGS__)
#define LOG_WARN(x, y)          ADK_LOG_WARN(x, y)
#define LOG_WARN_AC(x)          ADK_LOG_WARN_AC(x)
#define LOG_WARN_TF(x, ...)     ADK_LOG_WARN_TF(x, __VA_ARGS__)
#define LOG_WARN_AC_TF(...)     ADK_LOG_WARN_AC_TF(__VA_ARGS__)
#define LOG_ERROR(x, y)         ADK_LOG_ERROR(x, y)
#define LOG_ERROR_AC(x)         ADK_LOG_ERROR_AC(x)
#define LOG_ERROR_TF(x, ...)    ADK_LOG_ERROR_TF(x, __VA_ARGS__)
#define LOG_ERROR_AC_TF(...)    ADK_LOG_ERROR_AC_TF(__VA_ARGS__)
#define LOG_FATAL(x, y)         ADK_LOG_FATAL(x, y)
#define LOG_FATAL_AC(x)         ADK_LOG_FATAL_AC(x)
#define LOG_FATAL_TF(x, ...)    ADK_LOG_FATAL_TF(x, __VA_ARGS__)
#define LOG_FATAL_AC_TF(...)    ADK_LOG_FATAL_AC_TF(__VA_ARGS__)

#define SEVERITY_LEVEL_MIN_INT      ADK_LOG_LEVEL_TRACE
#define SEVERITY_LEVEL_TRACE_INT    ADK_LOG_LEVEL_TRACE
#define SEVERITY_LEVEL_DEBUG_INT    ADK_LOG_LEVEL_DEBUG
#define SEVERITY_LEVEL_INFO_INT     ADK_LOG_LEVEL_INFO
#define SEVERITY_LEVEL_WARN_INT     ADK_LOG_LEVEL_WARN
#define SEVERITY_LEVEL_ERROR_INT    ADK_LOG_LEVEL_ERROR
#define SEVERITY_LEVEL_FATAL_INT    ADK_LOG_LEVEL_FATAL
#define SEVERITY_LEVEL_MAX_INT      ADK_LOG_LEVEL_FATAL

inline void LOG(adk::log::SeverityLevel level,
                adk::log::LogCode code,
                const std::string& module_name,
                const std::string& function_name,
                int32_t src_line,
                const std::string& title,
                const std::string& message)
{
    ADK_LOG_RAW(level, code, module_name, function_name, src_line, title, message);
}

#endif

#endif /* ADK_LOG_H_ */
