#include "ipc_msg.h"
#include "string.h"
#include <iostream>
#include <stdint.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>

namespace adk_impl
{

uint32_t read_ipc(int rfd, char* buf, uint32_t size)
{
    ssize_t n      = 0;
    uint32_t total = 0;
    while (total < size)
    {
        n = read(rfd, buf, size - total);
        if (n > 0)
        {
            total += n;
            buf += n;
        }
        if (0 == n)
        {
            LOG_DEBUG("read pipe broken")
            return 0;
        }
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                usleep(1000);
                continue;
            }
            LOG_DEBUG("read pipe errno: " << errno)
            return 0;
        }
    }
    return total;
};

uint32_t write_ipc(int wfd, char* buf, uint32_t size)
{
    ssize_t n      = 0;
    uint32_t total = 0;
    while (total < size)
    {
        n = write(wfd, buf, size - total);
        if (n > 0)
        {
            total += n;
            buf += n;
        }
        if (0 == n)
        {
            continue;
        }
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                usleep(1000);
                continue;
            }
            LOG_DEBUG("write ipc errno: " << errno)
            return 0;
        }
    }
    return total;
};

IPCMessage::IPCMessage()
{
    buf_size_  = 1024;
    write_len_ = 0;
    read_len_  = 0;
    buf_ptr_   = (char*)malloc(buf_size_);
    write_ptr_ = buf_ptr_;
    read_ptr_  = buf_ptr_;
};

IPCMessage::~IPCMessage()
{
    if (buf_ptr_ != nullptr)
        free(buf_ptr_);
    buf_ptr_ = nullptr;
};

void IPCMessage::ExpandBuf(uint32_t new_size)
{
    if (new_size <= buf_size_)
    {
        return;
    }
    char* tmp_ptr = buf_ptr_;
    while (buf_size_ < new_size)
    {
        buf_size_ = buf_size_ * 2;
    }
    buf_ptr_ = (char*)malloc(buf_size_);
    if (nullptr == buf_ptr_)
    {
        LOG_DEBUG("ipc msg malloc failed")
        free(tmp_ptr);
        return;
    }
    memcpy(buf_ptr_, tmp_ptr, write_len_);
    free(tmp_ptr);
    tmp_ptr    = nullptr;
    write_ptr_ = buf_ptr_ + write_len_;
    read_ptr_  = buf_ptr_;
};

int IPCMessage::Append(const std::string& str)
{
    const char* ptr = str.c_str();
    uint32_t len    = str.size();
    return Append(ptr, len);
};

int IPCMessage::Append(const char* ptr, uint32_t len)
{
    //将ptr,len对应的数据追加到动态内存区
    uint32_t new_size = write_len_ + len + sizeof(uint32_t);
    if (new_size > buf_size_)
    {
        ExpandBuf(new_size);
    }
    memcpy(write_ptr_, &len, sizeof(uint32_t));
    write_ptr_ = write_ptr_ + sizeof(uint32_t);
    write_len_ = write_len_ + sizeof(uint32_t);
    memcpy(write_ptr_, ptr, len);
    write_ptr_ = write_ptr_ + len;
    write_len_ = write_len_ + len;
    return 0;
};

std::string IPCMessage::SubStr()
{
    //从动态内存区中获取string的数据
    uint32_t str_len = 0;
    memcpy(&str_len, read_ptr_, sizeof(uint32_t));
    read_ptr_ += sizeof(uint32_t);
    std::string str(read_ptr_, str_len);
    read_ptr_ += str_len;
    return str;
};

void Request::SetMethodId(uint32_t method_id)
{
    req_header_.method_id_ = method_id;
}

uint32_t Request::GetMethodId()
{
    return req_header_.method_id_;
}

bool Request::SendHeader(int write_fd)
{
    req_header_.msg_len_ = write_len_;
    uint32_t len         = sizeof(Request_Header);
    if (len != write_ipc(write_fd, (char*)&req_header_, sizeof(Request_Header)))
    {
        return false;
    }
    return true;
}

bool Request::SendBuf(int write_fd)
{
    uint32_t len = req_header_.msg_len_;
    if (len != write_ipc(write_fd, buf_ptr_, len))
    {
        return false;
    }
    return true;
}

bool Request::RecvHeader(int read_fd)
{
    uint32_t len = sizeof(Request_Header);
    if (len != read_ipc(read_fd, (char*)&req_header_, len))
    {
        return false;
    }
    return true;
}

bool Request::RecvBuf(int read_fd)
{
    uint32_t len = req_header_.msg_len_;
    ExpandBuf(len);
    if (len != read_ipc(read_fd, buf_ptr_, req_header_.msg_len_))
    {
        return false;
    }
    return true;
}

void Response::SetStatus(uint32_t status)
{
    rsp_header_.status_ = status;
}

uint32_t Response::GetStatus()
{
    return rsp_header_.status_;
}

bool Response::SendHeader(int write_fd)
{
    rsp_header_.msg_len_ = write_len_;
    uint32_t len         = sizeof(Request_Header);
    if (len != write_ipc(write_fd, (char*)&rsp_header_, len))
    {
        return false;
    }
    return true;
}

bool Response::SendBuf(int write_fd)
{
    uint32_t len = rsp_header_.msg_len_;
    if (len != write_ipc(write_fd, buf_ptr_, len))
    {
        return false;
    }
    return true;
}

bool Response::RecvHeader(int read_fd)
{
    uint32_t len = sizeof(Request_Header);
    if (len != read_ipc(read_fd, (char*)&rsp_header_, len))
    {
        return false;
    }
    return true;
}

bool Response::RecvBuf(int read_fd)
{
    uint32_t len = rsp_header_.msg_len_;
    ExpandBuf(len);
    if (len != read_ipc(read_fd, buf_ptr_, len))
    {
        return false;
    }
    return true;
}

}