#ifndef IPC_CALL_H
#define IPC_CALL_H

#include "ipc_msg.h"
#include <mutex>
#include <stdint.h>

namespace adk_impl
{

class IpcClient
{
public:
    static IpcClient* GetIpc();
    static void Exit();
    int Call(Request& req, Response& rsp);
    static IpcClient* ipc_;
    static std::mutex mtx_;
    static pid_t child_;

private:
    IpcClient();
    IpcClient(int read_fd, int write_fd);
    int read_fd_;
    int write_fd_;
    const uint32_t magic_numb_ = 0xCAFEBABE;
};

}
#endif