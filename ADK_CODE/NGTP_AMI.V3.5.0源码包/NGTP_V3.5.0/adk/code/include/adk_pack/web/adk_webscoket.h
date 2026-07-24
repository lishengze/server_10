/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_IMPL_WEB_SOCKET_H_
#define ADK_IMPL_WEB_SOCKET_H_

#include <string>
#include <memory>
#include <thread>
#include <map>
#include <system_error>

#include <boost/asio.hpp>
#include <boost/core/noncopyable.hpp>
#include <boost/thread.hpp>

namespace websocketpp
{
namespace config
{
struct asio;
struct asio_client;
}

template <typename config> class connection;
template <typename config> class server;
template <typename config> class client;

}

namespace adk
{
namespace web
{

//webscoket 消息码
namespace ws_opcode 
{
enum value 
{
    continuation = 0x0,
    text = 0x1,
    binary = 0x2,
    rsv3 = 0x3,
    rsv4 = 0x4,
    rsv5 = 0x5,
    rsv6 = 0x6,
    rsv7 = 0x7,
    close = 0x8,
    ping = 0x9,
    pong = 0xA,
    control_rsvb = 0xB,
    control_rsvc = 0xC,
    control_rsvd = 0xD,
    control_rsve = 0xE,
    control_rsvf = 0xF,

    CONTINUATION = 0x0,
    TEXT = 0x1,
    BINARY = 0x2,
    RSV3 = 0x3,
    RSV4 = 0x4,
    RSV5 = 0x5,
    RSV6 = 0x6,
    RSV7 = 0x7,
    CLOSE = 0x8,
    PING = 0x9,
    PONG = 0xA,
    CONTROL_RSVB = 0xB,
    CONTROL_RSVC = 0xC,
    CONTROL_RSVD = 0xD,
    CONTROL_RSVE = 0xE,
    CONTROL_RSVF = 0xF
}; // enum value

} // namespace ws_opcode

// webscoket 关闭状态码
namespace ws_close 
{
enum status 
{
    /// A blank value for internal use.
    blank = 0,

    /// Close the connection without a WebSocket close handshake.
    /**
     * This special value requests that the WebSocket connection be closed
     * without performing the WebSocket closing handshake. This does not comply
     * with RFC6455, but should be safe to do if necessary. This could be useful
     * for clients that need to disconnect quickly and cannot afford the
     * complete handshake.
     */
    omit_handshake = 1,

    /// Close the connection with a forced TCP drop.
    /**
     * This special value requests that the WebSocket connection be closed by
     * forcibly dropping the TCP connection. This will leave the other side of
     * the connection with a broken connection and some expensive timeouts. this
     * should not be done except in extreme cases or in cases of malicious
     * remote endpoints.
     */
    force_tcp_drop = 2,

    /// Normal closure, meaning that the purpose for which the connection was
    /// established has been fulfilled.
    normal = 1000,

    /// The endpoint was "going away", such as a server going down or a browser
    /// navigating away from a page.
    going_away = 1001,

    /// A protocol error occurred.
    protocol_error = 1002,

    /// The connection was terminated because an endpoint received a type of
    /// data it cannot accept.
    /**
     * (e.g., an endpoint that understands only text data MAY send this if it
     * receives a binary message).
     */
    unsupported_data = 1003,

    /// A dummy value to indicate that no status code was received.
    /**
     * This value is illegal on the wire.
     */
    no_status = 1005,

    /// A dummy value to indicate that the connection was closed abnormally.
    /**
     * In such a case there was no close frame to extract a value from. This
     * value is illegal on the wire.
     */
    abnormal_close = 1006,

    /// An endpoint received message data inconsistent with its type.
    /**
     * For example: Invalid UTF8 bytes in a text message.
     */
    invalid_payload = 1007,

    /// An endpoint received a message that violated its policy.
    /**
     * This is a generic status code that can be returned when there is no other
     * more suitable status code (e.g., 1003 or 1009) or if there is a need to
     * hide specific details about the policy.
     */
    policy_violation = 1008,

    /// An endpoint received a message too large to process.
    message_too_big = 1009,

    /// A client expected the server to accept a required extension request
    /**
     * The list of extensions that are needed SHOULD appear in the /reason/ part
     * of the Close frame. Note that this status code is not used by the server,
     * because it can fail the WebSocket handshake instead.
     */
    extension_required = 1010,

    /// An endpoint encountered an unexpected condition that prevented it from
    /// fulfilling the request.
    internal_endpoint_error = 1011,

    /// Indicates that the service is restarted. A client may reconnect and if
    /// if it chooses to do so, should reconnect using a randomized delay of
    /// 5-30s
    service_restart = 1012,

    /// Indicates that the service is experiencing overload. A client should
    /// only connect to a different IP (when there are multiple for the target)
    /// or reconnect to the same IP upon user action.
    try_again_later = 1013,

    /// Indicates that the server was acting as a gateway or proxy and received
    /// an invalid response from the upstream server. This is similar to 502
    /// HTTP Status Code.
    bad_gateway = 1014,

    /// An endpoint failed to perform a TLS handshake
    /**
     * Designated for use in applications expecting a status code to indicate
     * that the connection was closed due to a failure to perform a TLS
     * handshake (e.g., the server certificate can't be verified). This value is
     * illegal on the wire.
     */
    tls_handshake = 1015,
    
    /// A generic subprotocol error
    /**
     * Indicates that a subprotocol error occurred. Typically this involves
     * receiving a message that is not formatted as a valid message for the
     * subprotocol in use.
     */
    subprotocol_error = 3000,
    
    /// A invalid subprotocol data
    /**
     * Indicates that data was received that violated the specification of the
     * subprotocol in use.
     */
    invalid_subprotocol_data = 3001,

    /// First value in range reserved for future protocol use
    rsv_start = 1016,
    /// Last value in range reserved for future protocol use
    rsv_end = 2999
}; // enum status

} // namespace ws_close 

class WebSocketServer : private boost::noncopyable
{
public:
    using connection_hdl = std::weak_ptr<void>;

    // 消息
    struct Message
    {
        ws_opcode::value opcode; // 消息码
        std::string payload; // 消息data
    };

    using MessagePtr = std::shared_ptr<Message>;

    // websocket 客户端请求描述
    struct Request
    {
        std::string remote_endpoint; // remote_endpoint
        std::string path; // wss 请求路径
    };

    // websocket 连接
    class Connection
    {   
    public:
        ~Connection() = default;

        // 消息发送
        bool Send(const std::string &payload, ws_opcode::value op);

        // 消息发送
        bool Send(void const *payload, size_t len, ws_opcode::value op);

        // 主动关闭连接
        void Close(const ws_close::status status, const std::string &reason);

        // 获取 websocket 客户端请求信息
        const Request& GetRequest() const;

        // 获取当前链接主动关闭连接所设置的关闭状态码
        // 只有在 OnClose 回调时调用此函数有效
        ws_close::status GetLocalCloseCode() const;

        // 获取当前链接主动关闭连接所设置的错误描述
        // 只有在 OnClose 回调时调用此函数有效
        std::string const& GetLocalCloseReason() const;

        // 获取当前链接被对端关闭时所设置的关闭状态码
        // 只有在 OnClose 回调时调用此函数有效
        ws_close::status GetRemoteCloseCode() const;

        // 获取当前链接被对端关闭时所设置的错误描述
        // 只有在 OnClose 回调时调用此函数有效
        std::string const& GetRemoteCloseReason() const;

        // 获取该链接被非正常关闭的错误码，即非应用主动关闭而是内部出错
        // 只有在 OnClose 回调时调用此函数有效
        // 具体错误描述通过 error_code::message() 成员函数获取
        std::error_code GetErrorCode() const;

    private:
        Connection(Request &req,
                   connection_hdl con_hd, 
                   WebSocketServer *ws_server);

        connection_hdl con_hdl_ptr_;
        WebSocketServer* server_ptr_;
        Request request_;
        friend class WebSocketServer;
    };
    
    using ConnectionPtr = std::shared_ptr<Connection>;
    using OnOpen = std::function<void(ConnectionPtr)>;
    using OnClose = std::function<void(ConnectionPtr)>;
    using OnMessage = std::function<void(ConnectionPtr, MessagePtr)>;

    WebSocketServer() = default;

    ~WebSocketServer();

    struct Config
    {
        // websocket 服务端口
        unsigned short port = 80;

        // websocket 服务 线程数量
        std::size_t thread_pool_size = 1;

        // ipv4 或者 ipv6 地址
        std::string address;

        /// 端口复用
        bool reuse_address = true;

    };

    // 配置设置
    Config config;

    // 当有客户端连接上时，则回调该函数，应用设置此回调函数，并且注意线程安全处理
    OnOpen on_open;

    // 链接关闭的回调函数，应用设置此回调函数，并且注意线程安全处理，
    // 在该回调函数中处理关闭的链接
    OnClose on_close;

    // 收到消息的回调函数，应用设置此回调函数, 并且注意线程安全处理，
    // 在该回调函数中处理收到消息
    OnMessage on_message;

    // 开启 websocekt 服务，该函数会返回
    // 如果出错则抛出异常
    void Start();

    // 停止 websocekt 服务，该函数会阻塞直到所有线程退出
    // 停止 websocekt 该函数会阻塞直到后台线程退出
    // 在调用 Stop 接口之后，应用确保 Connection 对象不再调用 Send 接口，否则会造成 coredump
    // 一般地，在销毁 WebSocketServer 对象之前或者调用 WebSocketClient::Stop 接口之前，
    // 应用应该先释放所有 Connection 对象
    void Stop();

private:
    using ws_server = websocketpp::server<websocketpp::config::asio>;

    void Run();

    void OnSocketInit_(connection_hdl hdl, boost::asio::ip::tcp::socket &socket);

    void OnMessage_(connection_hdl hdl, void *msg);

    void OnOpenHandler_(connection_hdl hdl);

    void OnCloseHandler_(connection_hdl hdl);

    std::string GetPassword();

    std::shared_ptr<ws_server> ws_server_ptr_;
    std::thread ws_work_thr_;
    bool is_stopped = false;
    using RWMutex = boost::shared_mutex;
    using ReadLock = boost::shared_lock<RWMutex>;
    using WriteLock = boost::unique_lock<RWMutex>;
    RWMutex con_mtx_;
    std::map<connection_hdl, ConnectionPtr, std::owner_less<connection_hdl>> con_map_;
};

class WebSocketClient : private boost::noncopyable
{
public:
    using connection_hdl = std::weak_ptr<void>;
    using ws_connection_ptr = std::shared_ptr<websocketpp::connection<websocketpp::config::asio_client>>;

    // 消息
    struct Message
    {
        ws_opcode::value opcode; // 消息码
        std::string payload; // 消息data
    };

    using MessagePtr = std::shared_ptr<Message>;

    // websocket 连接
    class Connection
    {   
    public:
        ~Connection() = default;

        // 主动发送消息
        bool Send(const std::string &payload, ws_opcode::value op);

        // 主动发送消息
        bool Send(void const *payload, size_t len, ws_opcode::value op);

        // 主动关闭该链接
        void Close(const ws_close::status status, const std::string &reason);

        // 获取 链接的路径
        const std::string& GetConnectionPath() const;

        // 获取当前链接主动关闭连接所设置的关闭状态码
        // 只有在 OnClose 回调时调用此函数有效
        ws_close::status GetLocalCloseCode() const;

        // 获取当前链接主动关闭连接所设置的错误描述
        // 只有在 OnClose 回调时调用此函数有效
        std::string const& GetLocalCloseReason() const;

        // 获取当前链接被对端关闭时所设置的关闭状态码
        // 只有在 OnClose 回调时调用此函数有效
        ws_close::status GetRemoteCloseCode() const;

        // 获取当前链接被对端关闭时所设置的错误描述
        // 只有在 OnClose 回调时调用此函数有效
        std::string const& GetRemoteCloseReason() const;

        // 获取该链接被非正常关闭的错误码，即非应用主动关闭而是内部出错
        // 只有在 OnClose 回调时调用此函数有效
        // 具体错误描述通过 error_code::message() 成员函数获取
        std::error_code GetErrorCode() const;

    private:
        Connection(ws_connection_ptr ws_con_ptr,
                   const std::string &con_path);

        ws_connection_ptr ws_con_ptr_;
        std::string connection_path_;
        friend class WebSocketClient;
    };

    using ConnectionPtr = std::shared_ptr<Connection>;
    using OnOpen = std::function<void(ConnectionPtr)>;
    using OnClose = std::function<void(ConnectionPtr)>;
    using OnMessage = std::function<void(ConnectionPtr, MessagePtr)>;

    WebSocketClient() = default;

    ~WebSocketClient();

    // 链接打开的回调函数，应用按需设置此回调函数，并且注意线程安全处理
    // 在该回调函数中处理关闭的链接
    OnOpen on_open;

    // 链接关闭的回调函数，应用按需设置此回调函数，并且注意线程安全处理
    // 在该回调函数中处理关闭的链接
    OnClose on_close;

    // 消息回调函数，应用设置此回调函数, 并且注意线程安全处理
    // 在该回调函数中处理收到消息
    OnMessage  on_message;

    // 建立一条到 websocket 服务器连接，path 格式为 "ws://127.0.0.1:80",
    // timeout_mill 为超时时间，单位毫秒
    // 失败返回 nullptr，成功返回 ConnectionPtr，应用可以使用 ConnectionPtr 发送消息
    ConnectionPtr Connect(const std::string &path, uint32_t timeout_mill);

    // 开启 websocekt 后台后台线程 ，该函数会返回
    // 应用在调用 Connect 函数之前先调用此方法
    // 如果出错则抛出异常
    void Start();

    // 停止 websocekt，该函数会阻塞直到后台线程退出
    // 在调用 Stop 接口之后，应用确保 Connection 对象不再调用 Send 接口，否则造成 coredump
    // 一般地，在销毁 WebSocketClient 对象之前或者调用 WebSocketClient::Stop之前，
    // 应用应该先释放所有 Connection 对象
    void Stop();

private:
    using ws_client = websocketpp::client<websocketpp::config::asio_client>;

    void Run();

    void OnSocketInit_(connection_hdl hdl, boost::asio::ip::tcp::socket &socket);

    void OnMessage_(connection_hdl hdl, void *msg);

    void OnOpenHandler_(connection_hdl hdl);

    void OnCloseHandler_(connection_hdl hdl);

    std::shared_ptr<ws_client> ws_client_ptr_;
    std::thread ws_work_thr_;
    bool is_stopped = false;
    using RWMutex = boost::shared_mutex;
    using ReadLock = boost::shared_lock<RWMutex>;
    using WriteLock = boost::unique_lock<RWMutex>;
    RWMutex con_mtx_;
    std::map<ws_connection_ptr, ConnectionPtr, std::owner_less<ws_connection_ptr>> con_map_;
};

} // namespace web  
} // namespace adk


#endif