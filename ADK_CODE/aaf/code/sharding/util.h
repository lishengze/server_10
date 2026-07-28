#pragma once

#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string>
#include <adk/util.h>
#include <adk/log.h>

#ifndef AAF_SIGNAL_VALUE_EXIT
#define AAF_SIGNAL_VALUE_EXIT     1
#endif

const char kComEnd = 130;
const char kComStop = 131;

static bool IsPipeAlive(int pipe_fd, char& cmd)
{
    char one_char;
    cmd = '\0';
    int ret = ::read(pipe_fd, &one_char, 1);
    if (ret == 0 || (ret < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)))
    {
        return false;
    }
    else if (ret > 0)
    {
        assert(ret == 1);
        cmd = one_char;
        return false;
    }
    return true;
}

static inline bool IsPipeAlive(int pipe_fd)
{
    char one_char;
    return IsPipeAlive(pipe_fd, one_char);
}

// 检查是否为退出信号，包括系统kill信号和 send_usr1 AAF_SIGNAL_VALUE_EXIT
static inline bool IsQuitSignal(int sig_num, int value)
{
    bool is_signal_exit = false;
    if (sig_num == SIGTERM || sig_num == SIGINT || sig_num == SIGQUIT)
    {
        is_signal_exit = true;
    }

    if (sig_num == SIGUSR1 || sig_num == SIGUSR2)
    {
        if (value == AAF_SIGNAL_VALUE_EXIT)
        {
            is_signal_exit = true;
        }
    }

    return is_signal_exit;
}

static inline std::string MakeShmContMemoryName(const std::string& ctx_name)
{
    return adk::GetLoginUserName() + "_" + ctx_name + "_sharding_cont";
}

static inline std::string MakeShmNamePrefix(const std::string& ctx_name)
{
    return adk::GetLoginUserName() + "_" + ctx_name + "_";
}

static inline std::string MakeShmContChannelName(const std::string& ctx_name)
{
    return ctx_name + "_shm_cont_channel";
}

static inline std::string MakeFlrLaunchShmName(const std::string& ctx_name)
{
    return adk::GetLoginUserName() + "_" + ctx_name + "_follower_launch";
}

static std::map<adk::log::LogLevel, std::string> g_log_level_map {
    {ADK_LOG_LEVEL_TRACE, "Trace"},
    {ADK_LOG_LEVEL_DEBUG, "Debug"},
    {ADK_LOG_LEVEL_INFO, "Info"},
    {ADK_LOG_LEVEL_WARN, "Warn"},
    {ADK_LOG_LEVEL_ERROR, "Error"},
    {ADK_LOG_LEVEL_FATAL, "Fatal"},
};