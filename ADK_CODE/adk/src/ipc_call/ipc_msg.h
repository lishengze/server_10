#ifndef IPC_MSG_H
#define IPC_MSG_H

#include <stdint.h>
#include <string>

#ifdef NDEBUG
#define LOG_DEBUG(...)
#else
#include <iostream>
#define LOG_DEBUG(msg)                 \
    {                                  \
        std::cout << msg << std::endl; \
    }
#endif

namespace adk_impl
{

enum IpcStatus
{
    kIpcSucess = 0,
    kIpcError
};

//管道读，传入size后会循环读直到读到size
//读取成功：返回size, 读取失败：返回0
uint32_t read_ipc(int rfd, char* buf, uint32_t size);

//管道写，传入size后会循环写直到写入size
//写入成功：返回size, 写入失败：返回0
uint32_t write_ipc(int wfd, char* buf, uint32_t size);

class IPCMessage
{
public:
    IPCMessage();
    ~IPCMessage();
    //向内存中添加string
    int Append(const std::string& str);
    int Append(const char* ptr, uint32_t len);
    //从内存中获取string
    std::string SubStr();

protected:
    void ExpandBuf(uint32_t new_size);
    uint32_t write_len_;  //已写内存
    uint32_t read_len_;   //已读内存
    uint32_t buf_size_;   //实际内存
    char* write_ptr_;     //可写位置
    char* read_ptr_;      //可读位置
    char* buf_ptr_;       //内存位置
};

struct Request_Header
{
    uint32_t method_id_;  //调用函数的ID
    uint32_t msg_len_;    //消息体的长度
};

class Request : public IPCMessage
{
public:
    void SetMethodId(uint32_t method_id);
    uint32_t GetMethodId();
    bool SendHeader(int write_fd);  //向管道写入Request的头部
    bool SendBuf(int write_fd);     //向管道写入Request的请求消息体
    bool RecvHeader(int read_fd);   //从管道读取Request的头部
    bool RecvBuf(int read_fd);      //从管道读取Request的请求消息体

private:
    Request_Header req_header_;
};

struct Response_Header
{
    uint32_t status_;   //函数调用结果
    uint32_t msg_len_;  //消息体的长度
};

class Response : public IPCMessage
{
public:
    void SetStatus(uint32_t status);
    uint32_t GetStatus();
    bool SendHeader(int write_fd);
    bool SendBuf(int write_fd);
    bool RecvHeader(int read_fd);
    bool RecvBuf(int read_fd);

private:
    Response_Header rsp_header_;
};

}

#endif