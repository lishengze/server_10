#ifndef ADK_IMPL_FORK_RUN_H_
#define ADK_IMPL_FORK_RUN_H_

#include "util.h"
#include "error_code.h"
#include "arch/generic.h"

#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>

#include <sstream>
#include <boost/function.hpp>

namespace adk_impl
{

class ForkRun
{
public:
    typedef boost::function<bool (std::string& output, std::string& err_desc)> ChildCallbackType;
    typedef boost::function<void (const std::string& input)> ParentCallbackType;

    ForkRun()
    {
        Init();
    }

    ForkRun(const ParentCallbackType& parent_cb,
            const ChildCallbackType& child_cb)
        :   parent_cb_(parent_cb),
            child_cb_(child_cb)
    {
        Init();
    }

    ~ForkRun()
    {}

    int32_t Launch()
    {
        std::string err_desc;
        return Launch(err_desc);
    }

    int32_t Launch(const ParentCallbackType& parent_cb,
                   const ChildCallbackType& child_cb)
    {
        std::string err_desc;
        return Launch(parent_cb, child_cb, err_desc);
    }

    int32_t Launch(const ParentCallbackType& parent_cb,
                   const ChildCallbackType& child_cb,
                   std::string& err_desc)
    {
        parent_cb_ = parent_cb;
        child_cb_ = child_cb;
        return Launch(err_desc);
    }

    int32_t Launch(std::string& err_desc)
    {
        assert(child_cb_);
        assert(parent_cb_);

        int cfg_pipe[2];
        if (pipe2(cfg_pipe, 0) != 0)
        {  
            return ErrorCode::kFailure;
        }

        pid_t child_pid = fork();
        if (child_pid == 0)
        {
            close(cfg_pipe[0]);
            ::write(cfg_pipe[1], &g_com_begin, 1);

            output_.clear();
            err_desc_.clear();
            if (!child_cb_(output_, err_desc_))
            {
                ::write(cfg_pipe[1], err_desc_.c_str(), err_desc_.size());
                ::write(cfg_pipe[1], &g_com_end_err, 1);
            }
            else
            {
                ::write(cfg_pipe[1], output_.c_str(), output_.size());
                ::write(cfg_pipe[1], &g_com_end, 1);
            }
            close(cfg_pipe[1]);
            _exit(0);
        }
        else
        {
            OnExit<> on_exit([child_pid, &cfg_pipe](){
                close(cfg_pipe[0]);
                kill(child_pid, SIGKILL);
                waitpid(child_pid, NULL, 0);
            });

            close(cfg_pipe[1]);
            if (cfg_buf_ == NULL)
            {
                cfg_buf_ = (char*)malloc(cfg_buf_len_);
            }

            size_t cfg_size = 0;
            do {
                size_t cfg_read_size = ::read(cfg_pipe[0],
                                              cfg_buf_ + cfg_size,
                                              cfg_buf_len_ - cfg_size);

                if (cfg_read_size > 0)
                {
                    cfg_size += cfg_read_size;
                    if (cfg_size >= cfg_buf_len_)
                    {
                        cfg_buf_len_ = cfg_buf_len_ << 1;
                        cfg_buf_ = (char*)realloc(cfg_buf_, cfg_buf_len_);    // FIXME: check error
                    }
                    continue;
                }

                if (cfg_read_size == 0)
                    break;

                
                return ErrorCode::kFailure;
            } while (true);

            if (cfg_buf_[0] != g_com_begin 
                || (cfg_buf_[cfg_size - 1] != g_com_end
                    && cfg_buf_[cfg_size - 1] != g_com_end_err))
            {
                err_desc = "unknown";
                return ErrorCode::kFailure;
            }

            int32_t ec;
            if (cfg_buf_[cfg_size - 1] == g_com_end_err)
            {
                ec = ErrorCode::kFailure;
                err_desc.append(&cfg_buf_[1], cfg_size - 2);
            }
            else
            {
                ec = ErrorCode::kSuccess;
                parent_cb_(std::string(&cfg_buf_[1], cfg_size - 2));
            }

            return ec;
        }
    }

private:
    ParentCallbackType  parent_cb_;
    ChildCallbackType   child_cb_;
    std::string         output_;
    std::string         err_desc_;
    uint32_t            cfg_buf_len_;
    char*               cfg_buf_;

    void Init()
    {
        cfg_buf_len_ = 32 * 1024;
        cfg_buf_  = NULL;
    }
    static const char g_com_begin;
    static const char g_com_end;
    static const char g_com_end_err;
};

} // adk
#endif // ADK_FORK_RUN_H_
