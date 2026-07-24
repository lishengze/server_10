#ifndef IPC_SERVER_H
#define IPC_SERVER_H

#include "ipc_msg.h"
#include <functional>
#include <mutex>
#include <stdint.h>
#include <unordered_map>

namespace adk_impl
{

class IpcServer
{
public:
    using CallBack = std::function<int(Request& req, Response& rsp)>;
    IpcServer(int read_fd, int write_fd);
    ~IpcServer();
    //Server端注册IPC函数，需要和method_id对应
    void RegistMethod(const uint32_t method_id, CallBack func);
    //Server端接从管道获取数据，并进行一次IPC调用
    void RunOnce();

private:
    int read_fd_;
    int write_fd_;
    std::unordered_map<uint32_t, CallBack> handlers;
    const uint32_t magic_numb_ = 0xCAFEBABE;
};

}
#endif