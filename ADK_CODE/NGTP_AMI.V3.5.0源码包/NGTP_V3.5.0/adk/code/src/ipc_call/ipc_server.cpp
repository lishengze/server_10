#include "ipc_server.h"
#include "ipc_method.h"
#include "ipc_msg.h"
#include <functional>
#include <iostream>
#include <stdint.h>
#include <unistd.h>
#include <unordered_map>

namespace adk_impl
{

IpcServer::IpcServer(int read_fd, int write_fd)
{
    read_fd_  = read_fd;
    write_fd_ = write_fd;
}

void IpcServer::RegistMethod(const uint32_t method_id, CallBack func)
{
    handlers[method_id] = func;
}

void IpcServer::RunOnce()
{
    //Ipc服务正式启动前，Server端先向Client发送一个 双方约定好的numb，并校验numb的数值。
    write_ipc(write_fd_, (char*)&magic_numb_, sizeof(magic_numb_));
    LOG_DEBUG("ipc server write magic numb")
    Request req;
    Response rsp;
    if (!req.RecvHeader(read_fd_))
    {
        return;
    }
    if (!req.RecvBuf(read_fd_))
    {
        return;
    }

    //根据Request中的MethodId去调用函数
    auto func = handlers[req.GetMethodId()];
    LOG_DEBUG("method : " << req.GetMethodId() << " go to invoked")
    int ec = func(req, rsp);
    rsp.SetStatus(ec);

    if (!rsp.SendHeader(write_fd_))
    {
        return;
    }
    if (!rsp.SendBuf(write_fd_))
    {
        return;
    }
}

IpcServer::~IpcServer()
{
    close(read_fd_);
    close(write_fd_);
}

}