#include <adk/domain_socket.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

#include <time.h>
#include <iostream>

#include <adk/error_code.h>

#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>

namespace adk_impl
{

UnixSocket* UnixSocket::CreateServerSocket(const std::string& socket_name, std::string& error_info)
{
    boost::system::error_code ec;
    boost::filesystem::path socket_file_path(socket_name);
    if (boost::filesystem::exists(socket_file_path, ec))
    {
        boost::filesystem::remove(socket_file_path, ec);
    }

    auto server_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sockfd == kInvalidSocket)
    {
        error_info = (boost::format("create server unix socket failed, errno: %1%")
                                    % strerror(errno)).str();
        return nullptr;
    }

    //非阻塞模式
    auto opts = fcntl(server_sockfd, F_GETFL);
    if (fcntl(server_sockfd, F_SETFL, opts | O_NONBLOCK) != 0)
    {
        error_info = "set socket to nonblock error.";
        return nullptr;
    }

    struct sockaddr_un sun_address;
    sun_address.sun_family = AF_UNIX;
    strcpy(sun_address.sun_path, socket_name.c_str());

    if (0 != bind(server_sockfd, 
                  (struct sockaddr*)(&sun_address), 
                  sizeof(struct sockaddr_un)))
    {
        error_info = (boost::format("bind server unix socket failed, file path: %1%, errno: %2%")
                                      % socket_name % strerror(errno)).str();
        close(server_sockfd);
        return nullptr;
    }

    if (0 != listen(server_sockfd, 3))
    {
        error_info = (boost::format("listen server unix socket failed, file path: %1%, errno: %2%")
                                      % socket_name % strerror(errno)).str();
        close(server_sockfd);
        return nullptr;
    }

    UnixSocket* server_unix_sock = new UnixSocket();
    if (server_unix_sock != nullptr)
    {
        server_unix_sock->local_socketfd_ = server_sockfd;
        server_unix_sock->socket_name_ = socket_name;
        return server_unix_sock;
    }

    error_info = "Create a Server UnixSocket object failed";
    close(server_sockfd);
    return nullptr;

}

UnixSocket* UnixSocket::CreateClientSocket(const std::string& socket_name, std::string& error_info)
{
    auto client_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_sockfd == kInvalidSocket)
    {
        error_info = (boost::format("create client unix socket failed, errno: %1%")
                                      % strerror(errno)).str();
        return nullptr;
    }

    //非阻塞模式
    auto opts = fcntl(client_sockfd, F_GETFL);
    if (fcntl(client_sockfd, F_SETFL, opts | O_NONBLOCK) != 0)
    {
        error_info = "set socket to nonblock error.";
        return nullptr;
    }

    UnixSocket* client_unix_sock = new UnixSocket();
    if (client_unix_sock != nullptr)
    {
        client_unix_sock->local_socketfd_ = client_sockfd;
        client_unix_sock->socket_name_ = socket_name;
        return client_unix_sock;
    }

    error_info = "Create a Client UnixSocket object failed";
    close(client_sockfd);
    return nullptr;
}

int32_t UnixSocket::Connect(const uint32_t timeout_ms)
{
    struct sockaddr_un sun_address;
    sun_address.sun_family = AF_UNIX;
    strcpy(sun_address.sun_path, socket_name_.c_str());

    for (;;)
	{
		int ret = connect(local_socketfd_, 
                          (struct sockaddr*)(&sun_address), 
                          sizeof(struct sockaddr_un));
		if (ret == 0)
		{
			error_info_ = "connect to server successfully.";
            remote_socketfd_ = local_socketfd_;
            return ErrorCode::kSuccess;
		} 
		else if (ret == -1) 
		{
			if (errno == EINTR)
			{
				//connect 动作被信号中断，重试connect
				continue;
			} else if (errno == EINPROGRESS)
			{
				//连接正在尝试中
				break;
			} else {
				//真的出错了
                error_info_ = (boost::format("connect unix socket failed, file path: %1%, errno: %2%")
                                        % socket_name_ % strerror(errno)).str();
				return ErrorCode::kFailure;
			}
		}
	}

    struct pollfd conn_event;
    conn_event.fd = local_socketfd_;
    conn_event.events = POLLOUT;

    int result = poll(&conn_event, 1, timeout_ms);

    if (result > 0)
    {
        remote_socketfd_ = local_socketfd_;
        return ErrorCode::kSuccess;
    }
    error_info_ = "unix socket connection timeout";
    return ErrorCode::kFailure;
}

int32_t UnixSocket::Accept(const uint32_t timeout_ms)
{
    if (remote_socketfd_ != kInvalidSocket)
    {
        remote_socketfd_ = kInvalidSocket;
    }
    struct timespec start_time;
    struct timespec current_time;

    clock_gettime(CLOCK_MONOTONIC_RAW, &start_time);

    do
    {
        remote_socketfd_ = accept(local_socketfd_, nullptr, nullptr);

        if (remote_socketfd_ < 0)
        {
            if (EAGAIN == errno || EINTR == errno)
            {
                clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
                if (current_time.tv_sec - start_time.tv_sec > (timeout_ms / (uint32_t)1000))
                {
                    error_info_ = "unix socket accept connection timeout";
                    return ErrorCode::kFailure;
                }
                usleep(500);
                continue;
            }
            else
            {
                error_info_ = (boost::format("accept unix socket failed, errno: %1%")
                                        % strerror(errno)).str();
                return ErrorCode::kFailure;
            }
        }
        return ErrorCode::kSuccess;

    } while (true);
    
}

int32_t UnixSocket::Send(char* data, uint32_t total_len, uint32_t& write_len)
{
    if (remote_socketfd_ == kInvalidSocket)
    {
        error_info_ = "send data failed, invalid socket fd";
        return ErrorCode::kFailure;
    }

    const auto send_ec = send(remote_socketfd_, data, total_len, 0);
    // 发送成功
    if (send_ec > 0)
    {
        // FIXME: 内核缓冲区大小不够，只发送了部分数据
        //        返回一个已发送的数据长度，由外部调用者判断
        // if (ADK_UNLIKELY(send_ec != (int32_t)len))
        // {
        //     error_info_ = (boost::format("send data failed, respect length<%1%> send result<%2%>")
        //                                 % len % send_ec).str();
        //     return ErrorCode::kFailure;
        // }
        write_len = (uint32_t)send_ec;
        return ErrorCode::kSuccess;
    }
    // 发送失败
    else if (send_ec < 0)
    {   
        // 阻塞 or 信号中断
        if (errno == EWOULDBLOCK || errno == EINTR)
        {
            return ErrorCode::kWouldblock;
        }
        else
        {
            error_info_ = (boost::format("send data error, errno: %1%")
                                      % strerror(errno)).str();
            close(remote_socketfd_);
            remote_socketfd_ = kInvalidSocket;
            return ErrorCode::kFailure;
        }
    }
    else
    {
        //对端关闭了连接
        error_info_ = (boost::format("the peer close the connection, errno: %1%")
                                        % strerror(errno)).str();
        close(remote_socketfd_);
        remote_socketfd_ = kInvalidSocket;
        return ErrorCode::kFailure;
    }
    
}

int32_t UnixSocket::Recv(char* data, const uint32_t max_len, uint32_t& read_len)
{
    if (remote_socketfd_ == kInvalidSocket)
    {
        error_info_ = "recv data failed, invalid socket fd";
        return ErrorCode::kFailure;
    }

    const auto recv_len = recv(remote_socketfd_, 
                               data, 
                               max_len, 
                               0);

    if (recv_len > 0)
    {
        read_len = recv_len;
        return ErrorCode::kSuccess;
    }
    else if (recv_len < 0)
    {
        // 阻塞 or 信号中断
        if (errno == EWOULDBLOCK || errno == EINTR)
        {
            return ErrorCode::kWouldblock;
        }
        else
        {
            error_info_ = (boost::format("recv unix socket failed, errno: %1%")
                                      % strerror(errno)).str();
            close(remote_socketfd_);
            remote_socketfd_ = kInvalidSocket;
            return ErrorCode::kFailure;
        }
    }
    else
    {
        // 对端关闭了连接
        error_info_ = (boost::format("read end of stream of unix socket, errno: %1%")
                                        % strerror(errno)).str();
        close(remote_socketfd_);
        remote_socketfd_ = kInvalidSocket;
        return ErrorCode::kFailure;
    }
}

bool UnixSocket::IsSocketOk()
{
    if (local_socketfd_ != -1 && remote_socketfd_ != -1)
    {
        return true;
    }
    return false;
}

void UnixSocket::Close()
{
    if (local_socketfd_ != -1)
    {
        close(local_socketfd_);
        local_socketfd_ = kInvalidSocket;
    }
    if (remote_socketfd_ != -1)
    {
        close(remote_socketfd_);
        remote_socketfd_ = kInvalidSocket;
    }
}

std::string UnixSocket::GetLastError()
{
    return error_info_;
}

}
