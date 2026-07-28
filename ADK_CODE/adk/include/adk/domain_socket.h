#pragma once

#include <string>
#include <adk/arch/generic.h>

namespace adk_impl
{

class UnixSocket
{

public:
    const static int32_t kInvalidSocket = -1;

    UnixSocket() = default;

    /**
     * @brief Create a Server UnixSocket object
     * 
     * @param socket_name 
     * @param error_info record error information during creating Server UnixSocket object
     * @return UnixSocket* 
     * 
     * @note create a socket, bind and listen
     */
    static UnixSocket* CreateServerSocket(const std::string& socket_name, std::string& error_info);

    /**
     * @brief Create a Client UnixSocket object
     * 
     * @param socket_name
     * @param error_info record error information during creating Client UnixSocket object
     * @return UnixSocket* 
     * 
     * @note create a socket
     */
    static UnixSocket* CreateClientSocket(const std::string& socket_name, std::string& error_info);

    /**
     * @brief Connect to Server
     * 
     * @param timeout_ms connection timeout
     * @return int32_t ErrorCode like kSuccess or kFailure 
     * 
     * @note Non-Block connect to server 
     */
    int32_t Connect(const uint32_t timeout_ms);
    
    /**
     * @brief Server UnixSocket accept a connection
     * 
     * @param timeout_ms accept timeout
     * @return int32_t ErrorCode like kSuccess or kFailure 
     * 
     * @note Non-Block wait to connect
     */
    int32_t Accept(const uint32_t timeout_ms);

    /**
     * @brief send data by socket fd
     * 
     * @param data 
     * @param total_len total data length
     * @param write_len the actual length of data sent 
     * @return int32_t ErrorCode like kSuccess, kFailure, kWouldblock
     * 
     * @note Non-Block send data
     */

    int32_t Send(char* data, uint32_t total_len, uint32_t& write_len);

    /**
     * @brief Receive data from remote socketfd
     * 
     * @param data 
     * @param max_len max receive length
     * @param read_len the actual length of data reveived
     * @return int32_t ErrorCode like kSuccess, kFailure, kWouldblock
     * 
     * @note Non-Block recv data
     */
    int32_t Recv(char* data, const uint32_t max_len, uint32_t& read_len);

    /**
     * @brief check unixsocket whether is valid
     * 
     * @return true valid
     * @return false invalid
     */
    bool IsSocketOk();

    /**
     * @brief close socket
     * 
     */
    void Close();

    /**
     * @brief get socket error
     * 
     * @return std::string 
     */
    std::string GetLastError();

private:  
    std::string error_info_;
    int local_socketfd_ = -1;
    int remote_socketfd_ = -1;
    std::string socket_name_;
};

} // namespace ami
