#include "ipc_call.h"
#include "ipc_msg.h"
#include <adk/util.h>
#include <mutex>
#include <stdint.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if (defined __AMI_TEST_FRAMEWORK__) || (defined __AMI_TEST__)
#include <stdlib.h>
#else
#endif

namespace adk_impl
{

IpcClient* IpcClient::ipc_ = nullptr;
std::mutex IpcClient::mtx_;
pid_t IpcClient::child_ = -1;

IpcClient* IpcClient::GetIpc()
{
    //创建单例
    if (nullptr == ipc_)
    {
        std::lock_guard<std::mutex> mtx(mtx_);
        if (nullptr == ipc_)
        {
            int ctos_fd[2];
            pipe(ctos_fd);
            int stoc_fd[2];
            pipe(stoc_fd);
            child_ = fork();
            if (child_ < 0)
            {
                LOG_DEBUG("child process fork failed")
                return nullptr;
            }
            else if (child_ > 0)  //父进程
            {
                close(ctos_fd[0]);
                close(stoc_fd[1]);
                ipc_ = new IpcClient(stoc_fd[0], ctos_fd[1]);
            }
            else  //子进程
            {
                close(ctos_fd[1]);
                close(stoc_fd[0]);

                std::string ipc_path;
                char* path = getenv("ADK_IPC_TOOL_PATH");
                if (path)
                {
                    ipc_path = std::string(path) + "/ipc_service";
                }
                else
                {
                    std::string ami_pkg_path = adk_impl::GetInstallPath("libadk.so");
                    ipc_path                 = ami_pkg_path + "/bin/tools/encrypt_tool/ipc_service";
                }

                if (0 != access(ipc_path.c_str(), F_OK | X_OK))
                {
                    LOG_DEBUG("ipc_serivce not accessible, path :" << ipc_path)
                    return nullptr;
                }

                execl(ipc_path.c_str(),
                      std::to_string(ctos_fd[0]).c_str(),
                      std::to_string(stoc_fd[1]).c_str(),
                      NULL);
                LOG_DEBUG("child process execl failed")
                _exit(1);
            }
        }
    }
    return ipc_;
}

void IpcClient::Exit()
{
    //单例退出
    std::lock_guard<std::mutex> mtx(mtx_);
    if (ipc_ != nullptr)
    {
        close(ipc_->read_fd_);
        close(ipc_->write_fd_);
        delete ipc_;
        ipc_ = nullptr;
    }
    waitpid(child_, NULL, 0);
}

int IpcClient::Call(Request& req, Response& rsp)
{
    std::lock_guard<std::mutex> mtx(mtx_);
    if (!req.SendHeader(write_fd_))
    {
        return adk_impl::IpcStatus::kIpcError;
    }
    if (!req.SendBuf(write_fd_))
    {
        return adk_impl::IpcStatus::kIpcError;
    }
    if (!rsp.RecvHeader(read_fd_))
    {
        return adk_impl::IpcStatus::kIpcError;
    }
    if (!rsp.RecvBuf(read_fd_))
    {
        return adk_impl::IpcStatus::kIpcError;
    }
    return rsp.GetStatus();
}

IpcClient::IpcClient(int read_fd, int write_fd)
{
    read_fd_          = read_fd;
    write_fd_         = write_fd;
    uint32_t recv_num = 0;

    auto len = sizeof(uint32_t);
    if (len != read_ipc(read_fd_, (char*)&recv_num, len))
    {
        LOG_DEBUG("recv numb failed , ipc init failed")
        return;
    }
    if (recv_num != magic_numb_)
    {
        LOG_DEBUG("server send wrong numb , ipc init failed")
    }
}

}