#ifndef AMI_TC_SOCKET_RAW_H_
#define AMI_TC_SOCKET_RAW_H_

#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <linux/types.h>

#include <string>
#include <adk/arch/generic.h>

constexpr int32_t kEpollError = -1;
constexpr int32_t kSocketError = -1;
constexpr int32_t kEpollWaitoutMilli = 1000;

class SocketRaw
{
public:
    int32_t socket_fd()
    {
        return socket_fd_;
    }

    static bool SetBlocking(int32_t sock_fd);
    static bool SetUnblocking(int32_t sock_fd);

    inline bool SendTo(const void *buf, size_t len, const struct sockaddr_in& dest_addr)
    {
        constexpr socklen_t sock_len = sizeof(struct sockaddr_in);
        ssize_t result = sendto(socket_fd_, buf, len, 0, (const sockaddr*)&dest_addr, sock_len);

    retry:
        if (ADK_UNLIKELY(kSocketError == result))
        {
            const auto error_no = errno;
            if ((EAGAIN == error_no) || (EWOULDBLOCK == error_no))
            {
                result = sendto(socket_fd_, buf, len, 0, (const sockaddr*)&dest_addr, sock_len);
                goto retry;
            }
            else
            {
                return false;
            }
        }

        return true;
    }

    inline bool SendMMsg(struct mmsghdr* msgvec, unsigned int vlen)
    {
#if ((__GLIBC__ > 2) || ((__GLIBC__ == 2) && (__GLIBC_MINOR__ >= 14)))
        ssize_t result = sendmmsg(socket_fd_, msgvec, vlen, 0);
        if (ADK_UNLIKELY(result != (ssize_t)vlen))
        {
            int32_t left_len = vlen;
        retry:
            if (ADK_UNLIKELY(result < 0))
            {
                return false;
            }

            left_len -= result;
            if (0 == left_len)
            {
                return true;
            }

            result = sendmmsg(socket_fd_, msgvec + (vlen - left_len), left_len, 0);
            goto retry;
        }
#else
        for (unsigned int vidx = 0; vidx < vlen; ++vidx)
        {
            if (ADK_UNLIKELY(!SendMsg(msgvec[vidx].msg_hdr)))
            {
                return false;
            }
        }
#endif
        return true;
    }

    inline bool SendMsg(const struct msghdr& msg)
    {
        ssize_t result = sendmsg(socket_fd_, &msg, 0);

    retry:
        if (ADK_UNLIKELY(kSocketError == result))
        {
            const auto error_no = errno;
            if ((EAGAIN == error_no) || (EWOULDBLOCK == error_no))
            {
                result = sendmsg(socket_fd_, &msg, 0);
                goto retry;
            }
            else
            {
                return false;
            }
        }
        return true;
    }

    inline ssize_t Recvfrom(void* buffer, uint32_t length)
    {
        return recvfrom(socket_fd_, buffer, length, 0, nullptr, nullptr);
    }

    inline ssize_t Recvfrom(void* buffer, uint32_t length, struct sockaddr_in& remote_addr)
    {
        socklen_t sock_len = sizeof(struct sockaddr_in);
        return recvfrom(socket_fd_, buffer, length, 0, (struct sockaddr*)&remote_addr, &sock_len);
    }

    inline int RecvMMsg(struct mmsghdr* msgvec, uint32_t vlen)
    {
        return recvmmsg(socket_fd_, msgvec, vlen, MSG_DONTWAIT, NULL);
    }

    bool EpollWait(int32_t timeout_ms = kEpollWaitoutMilli)
    {
        struct epoll_event event;
        const int ep_waits = epoll_wait(epoll_fd_, &event, 1, timeout_ms);
        if ((1 == ep_waits) && (event.events & EPOLLIN))
        {
            return true;
        }

        return false;
    }

    bool SetBlocking()
    {
        return SetBlocking(socket_fd_);
    }

    bool SetUnblocking()
    {
        return SetUnblocking(socket_fd_);
    }

protected:
    SocketRaw();

    bool Open(uint16_t port, const std::string& local_addr, bool reuse_addr);

    void Close();

protected:
    uint16_t bind_port_;
    int32_t  epoll_fd_;
    int32_t  socket_fd_;
    struct sockaddr_in sock_addr_;
};

#endif // !AMI_TC_SOCKET_RAW_H_
