#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <boost/format.hpp>
#include <boost/filesystem.hpp>

#include <adk/pipe.h>
#include <adk/util.h>
#include <adk/error_code.h>
#include <adk/timeout_counter.h>

namespace adk_impl
{

Pipe::Pipe()
    :   pfd_(-1)
{}

Pipe::~Pipe()
{
    if (pfd_ != -1)
        close(pfd_);
}

static bool g_is_ignore_sig = false;
static std::string g_pipe_root_path;

class InitPipeRootPath
{
public:
    InitPipeRootPath()
    {
        if (GetLoginUserName() == "root")
        {
            g_pipe_root_path = (boost::format("/root/.%1%_pipe/") %GetLoginUserName()).str();
        }
        else
        {
            g_pipe_root_path = (boost::format("/home/%1%/.%1%_pipe/") %GetLoginUserName()).str();
        }
        // mkdir(g_pipe_root_path.c_str(), 0660);
        boost::filesystem::path dir_path = g_pipe_root_path;
        boost::system::error_code ec;
        boost::filesystem::create_directories(dir_path, ec);
    }
}g_do_init;

static std::string GetActualPath(const std::string& path)
{
    return (boost::format("%1%/%2%") % g_pipe_root_path % path).str();
}

Pipe* Pipe::Create(const std::string& path,
                   PipeFlag rw_flag,
                   bool is_block)
{
    if (!g_is_ignore_sig)
    {
        signal(SIGPIPE, SIG_IGN);
        g_is_ignore_sig = true;
    }

    std::string actual_path = GetActualPath(path);
    if (mkfifo(actual_path.c_str(), 0660) < 0)
    {
        if (errno != EEXIST)
        {
            return nullptr;
        }
    }

    int flags = 0;
    switch (rw_flag)
    {
        case PipeFlag::kReadOnly:
            flags |= O_RDONLY;
            break;
        case PipeFlag::kWriteOnly:
            flags |= O_WRONLY;
            break;
        case PipeFlag::kReadWrite:
            flags |= O_RDWR;
            break;
        default:
            return nullptr;
    }

    if (!is_block)
    {
        flags |= O_NONBLOCK;
    }

    int fd = open(actual_path.c_str(), flags);
    if (fd < 0)
    {
        return nullptr;
    }

    Pipe* new_pipe = new Pipe();
    new_pipe->pfd_ = fd;

    int cur_flags = fcntl(new_pipe->pfd_, F_GETFL, 0);
    fcntl(new_pipe->pfd_, F_SETFL, cur_flags|O_NONBLOCK);
    return new_pipe;
}

void Pipe::ChangeRootPath(const std::string& rpath)
{
    g_pipe_root_path = rpath;
    boost::filesystem::path dir_path = g_pipe_root_path;
    boost::system::error_code ec;
    boost::filesystem::create_directories(dir_path, ec);
}

int32_t Pipe::Write(const void* buf, uint32_t& len, uint64_t timeout_ns)
{
    ssize_t ret = write(pfd_, buf, len);
    if (ret == len)
    {
        return ErrorCode::kSuccess;
    }

    if (timeout_ns == 0)
    {
        if (ret >= 0)
        {
            len = ret;
            return ErrorCode::kWouldblock;
        }

        if (errno == EAGAIN)
        {
            len = 0;
            return ErrorCode::kWouldblock;
        }

        return ErrorCode::kFailure;
    }

    uint32_t wsize;
    uint32_t left;
    if (ret > 0)
    {
        wsize = ret;
        left = len - ret;
    }
    else
    {
        wsize = 0;
        left = len;
    }

    TimeoutCounter toc(1, timeout_ns);
    do
    {
        ret = write(pfd_, ((char*)buf) + wsize, left);
        if (ret == left)
        {
            return ErrorCode::kSuccess;
        }

        if (ret >= 0)
        {
            left -= ret;
            wsize += ret;
        }

        if (toc.IsTimeout())
        {
            if (ret < 0)
            {
                if (errno == EAGAIN)
                {
                    len = wsize;
                    return ErrorCode::kWouldblock;
                }

                return ErrorCode::kFailure;
            }
            len = wsize;
            return ErrorCode::kWouldblock;
        }

        toc.Run();

    } while (true);
}

int32_t Pipe::Read(void* buf, uint32_t& len, uint64_t timeout_ns)
{
    ssize_t ret = read(pfd_, buf, len);
    if (ret > 0)
    {
        len = ret;
        return ErrorCode::kSuccess;
    }

    if (timeout_ns == 0)
    {
        if (ret < 0)
        {
            if (errno == EAGAIN)
            {
                return ErrorCode::kWouldblock;
            }
            // other errors
        }

        // ret == 0
        return ErrorCode::kFailure;
    }

    TimeoutCounter* toc = nullptr;
    do
    {
        ret = read(pfd_, buf, len);
        if (ret > 0)
        {
            len = ret;
            return ErrorCode::kSuccess;
        }

        if (toc == nullptr)
        {
            toc = new TimeoutCounter(1, timeout_ns);
        }

        if (toc->IsTimeout())
        {
            if (ret < 0)
            {
                if (errno == EAGAIN)
                    return ErrorCode::kWouldblock;
            }

            // ret == 0
            return ErrorCode::kFailure;
        }

        toc->Run();

    } while (true);
}

int32_t Pipe::Destroy(const std::string& path)
{
    if (remove(GetActualPath(path).c_str()) < 0)
        return ErrorCode::kFailure;
    return ErrorCode::kSuccess;
}

} // adk
