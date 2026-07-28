#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/client.hpp>

#include <adk_pack/web/adk_webscoket.h>
#include <adk_pack/entry_wrapper.h>

namespace adk
{
namespace web
{

WebSocketServer::Connection::Connection(WebSocketServer::Request &req,
                                        connection_hdl con_hd, 
                                        WebSocketServer* ws_server)
{
    request_ = std::move(req);
    con_hdl_ptr_ = con_hd;
    server_ptr_ = ws_server;
}

bool WebSocketServer::Connection::Send(const std::string &payload, ws_opcode::value op)
{
    websocketpp::lib::error_code ec;
    auto con_ptr = server_ptr_->ws_server_ptr_->get_con_from_hdl(con_hdl_ptr_, ec);
    if (ec)
    {
        return false;
    }

    ec = con_ptr->send(payload, static_cast<websocketpp::frame::opcode::value>(op));
    return ec ? false : true;
}

bool WebSocketServer::Connection::Send(void const *payload, size_t len, ws_opcode::value op)
{
    websocketpp::lib::error_code ec;
    auto con_ptr = server_ptr_->ws_server_ptr_->get_con_from_hdl(con_hdl_ptr_, ec);
    if (ec)
    {
        return false;
    }

    ec = con_ptr->send(payload, len, static_cast<websocketpp::frame::opcode::value>(op));
    return ec ? false : true; 
}

void WebSocketServer::Connection::Close(const ws_close::status status, const std::string &reason)
{
    websocketpp::lib::error_code ec;
    auto con_ptr = server_ptr_->ws_server_ptr_->get_con_from_hdl(con_hdl_ptr_, ec);
    if (ec)
    {
        return;
    }

    con_ptr->close(static_cast<websocketpp::close::status::value>(status), reason, ec);
}

const WebSocketServer::Request& WebSocketServer::Connection::GetRequest() const
{
    return request_;
}

// 获取当前链接主动关闭连接所设置的关闭状态码
// 只有在 OnClose 回调时调用此函数有效
ws_close::status WebSocketServer::Connection::GetLocalCloseCode() const
{
    websocketpp::lib::error_code ec;
    {
        // Retrieves a connection_ptr from a connection_hdl
        // 如果转换失败，则直接返回 ec，如果 ec 为 false，con_ptr 保证不为 nullptr
        auto con_ptr = server_ptr_->ws_server_ptr_->get_con_from_hdl(con_hdl_ptr_, ec);
        return ec ? ws_close::status::no_status
                    : static_cast<ws_close::status>(con_ptr->get_local_close_code());
    }
}

// 获取当前链接主动关闭连接所设置的错误描述
// 只有在 OnClose 回调时调用此函数有效
std::string const& WebSocketServer::Connection::GetLocalCloseReason() const
{
    static const std::string err_cast = "converting weak pointer to shared_ptr failed";
    websocketpp::lib::error_code ec;
    {
        // Retrieves a connection_ptr from a connection_hdl
        // 如果转换失败，则直接返回 ec，如果 ec 为 false，con_ptr 保证不为 nullptr
        auto con_ptr = server_ptr_->ws_server_ptr_->get_con_from_hdl(con_hdl_ptr_, ec);
        return ec ? err_cast : con_ptr->get_local_close_reason();
    }
}

// 获取当前链接被对端关闭时所设置的关闭状态码
// 只有在 OnClose 回调时调用此函数有效
ws_close::status WebSocketServer::Connection::GetRemoteCloseCode() const
{
    websocketpp::lib::error_code ec;
    {
        // Retrieves a connection_ptr from a connection_hdl
        // 如果转换失败，则直接返回 ec，如果 ec 为 false，con_ptr 保证不为 nullptr
        auto con_ptr = server_ptr_->ws_server_ptr_->get_con_from_hdl(con_hdl_ptr_, ec);
        return ec ? ws_close::status::no_status
                    : static_cast<ws_close::status>(con_ptr->get_remote_close_code());
    }
}

// 获取当前链接被对端关闭时所设置的错误描述
// 只有在 OnClose 回调时调用此函数有效
std::string const& WebSocketServer::Connection::GetRemoteCloseReason() const
{
    static const std::string err_cast = "converting weak pointer to shared_ptr failed";
    websocketpp::lib::error_code ec;
    {
        // Retrieves a connection_ptr from a connection_hdl
        // 如果转换失败，则直接返回 ec，如果 ec 为 false，con_ptr 保证不为 nullptr
        auto con_ptr = server_ptr_->ws_server_ptr_->get_con_from_hdl(con_hdl_ptr_, ec);
        return ec ? err_cast : con_ptr->get_remote_close_reason();
    }
}

std::error_code WebSocketServer::Connection::GetErrorCode() const
{
    websocketpp::lib::error_code ec;
    {
        // Retrieves a connection_ptr from a connection_hdl
        // 如果转换失败，则直接返回 ec，如果 ec 为 false，con_ptr 保证不为 nullptr
        auto con_ptr = server_ptr_->ws_server_ptr_->get_con_from_hdl(con_hdl_ptr_, ec);
        return ec ? ec : con_ptr->get_ec();
    }
}

WebSocketServer::~WebSocketServer()
{
    Stop();
}

void WebSocketServer::Start()
{
    ws_server_ptr_.reset(new ws_server);
    ws_server_ptr_->clear_access_channels(websocketpp::log::alevel::all);
    ws_server_ptr_->clear_error_channels(websocketpp::log::alevel::all);

    ws_server_ptr_->init_asio();
    if (config.reuse_address)
    {
        ws_server_ptr_->set_reuse_addr(true);
    }
    
    auto OnMessage = [this](websocketpp::connection_hdl hdl, ws_server::message_ptr msg)
    {
        this->OnMessage_(hdl, msg.get());
    };

    namespace wp = websocketpp::lib::placeholders;
    ws_server_ptr_->set_message_handler(OnMessage);
    ws_server_ptr_->set_socket_init_handler(websocketpp::lib::bind(&WebSocketServer::OnSocketInit_, this, wp::_1, wp::_2));
    ws_server_ptr_->set_open_handler(websocketpp::lib::bind(&WebSocketServer::OnOpenHandler_, this, wp::_1));
    ws_server_ptr_->set_close_handler(websocketpp::lib::bind(&WebSocketServer::OnCloseHandler_, this, wp::_1));

    boost::asio::ip::tcp::endpoint endpoint;
    if(config.address.size() > 0)
    {
        endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(config.address), config.port);
    }
    else
    {
        endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), config.port);
    }

    ws_server_ptr_->listen(endpoint);
    ws_server_ptr_->start_accept();
    ws_work_thr_ = std_thread("websocket server", "websocket server thread", std::bind(&WebSocketServer::Run, this));
}

void WebSocketServer::Stop()
{
    if (!is_stopped && ws_server_ptr_ != nullptr)
    {
        ws_server_ptr_->stop_perpetual();
        ws_server_ptr_->stop();
    }

    if (ws_work_thr_.joinable())
    {
        ws_work_thr_.join();
    }

    is_stopped = true;
}

void WebSocketServer::Run()
{
    try
    {
        std::vector<std::thread> thr_vec;
        if (config.thread_pool_size > 1)
        {
            thr_vec.reserve(config.thread_pool_size - 1);
            for (std::size_t i = 1; i < config.thread_pool_size; ++i)
            {
                thr_vec.emplace_back(std_thread("websocket server", "websocket server thread", [this]()
                {
                    try
                    {
                        // server运行在该线程
                        {
                            this->ws_server_ptr_->run();
                        }
                    }
                    catch(...)
                    {
                    }
                }));
            }
        }
        {
            ws_server_ptr_->run();
        }
        for (auto &thr : thr_vec)
        {
            thr.join();
        }
    }
    catch (...)
    {
    }
}

void WebSocketServer::OnSocketInit_(connection_hdl hdl, boost::asio::ip::tcp::socket &socket)
{
    boost::asio::ip::tcp::no_delay option(true);
    socket.set_option(option);
}

void WebSocketServer::OnMessage_(connection_hdl hdl, void *msg)
{
    websocketpp::lib::error_code ec;
    auto ws_con_ptr = ws_server_ptr_->get_con_from_hdl(hdl, ec);
    if (ec)
    {
        return;
    }

    ReadLock lck(con_mtx_);
    auto it = con_map_.find(hdl);
    {
        if (it == con_map_.end())
        {
            return;
        }
    }

    if (on_message)
    {
        try
        {
           auto mst_ptr = static_cast<ws_server::connection_type::message_type*>(msg);
           MessagePtr message;
           message.reset(new Message);
           message->opcode = static_cast<ws_opcode::value>(mst_ptr->get_opcode());
           message->payload = std::move(mst_ptr->get_raw_payload());
           on_message(it->second, message);
        }
        catch(...)
        {
        }
    }
}

void WebSocketServer::OnOpenHandler_(connection_hdl hdl)
{
    websocketpp::lib::error_code ec;
    auto ws_con_ptr = ws_server_ptr_->get_con_from_hdl(hdl, ec);
    if (ec)
    {
        return;
    }

    WriteLock lck(con_mtx_);
    auto it = con_map_.find(hdl);
    {
        if (it != con_map_.end())
        {
            websocketpp::lib::error_code ec;
            ws_con_ptr->close(websocketpp::close::status::normal, "", ec);
            return;
        }
    }

    auto &ws_req = ws_con_ptr->get_request();
    Request request;
    request.remote_endpoint = ws_con_ptr->get_remote_endpoint();
    request.path = ws_req.get_uri();

    ConnectionPtr con_ptr;
    con_ptr.reset(new Connection(request, hdl, this));
    con_map_.emplace(hdl, con_ptr);
    if (on_open)
    {
        try
        {
            on_open(con_ptr);
        }
        catch(...)
        {
        }
    }
}

void WebSocketServer::OnCloseHandler_(connection_hdl hdl)
{
    WriteLock lck(con_mtx_);
    auto it = con_map_.find(hdl);
    if (it != con_map_.end())
    {
        if (on_close)
        {
            try
            {
                on_close(it->second);
            }
            catch(...)
            {
            }
        }

        con_map_.erase(it);
    }
}


WebSocketClient::Connection::Connection(ws_connection_ptr ws_con_ptr,
                                        const std::string &con_path)
{
    ws_con_ptr_ = ws_con_ptr;
    connection_path_ = con_path;
}

bool WebSocketClient::Connection::Send(const std::string &payload, ws_opcode::value op)
{
    websocketpp::lib::error_code ec;
    {
        // 利用非tls的连接对象发送消息
        ec = ws_con_ptr_->send(payload, static_cast<websocketpp::frame::opcode::value>(op));
    }
    return ec ? false : true;
}

bool WebSocketClient::Connection::Send(void const *payload, size_t len, ws_opcode::value op)
{
    websocketpp::lib::error_code ec;
    {
        // 利用非tls的连接对象发送消息
        ec = ws_con_ptr_->send(payload, len, static_cast<websocketpp::frame::opcode::value>(op));
    }
    return ec ? false : true; 
}

void WebSocketClient::Connection::Close(const ws_close::status status, const std::string &reason)
{
    websocketpp::lib::error_code ec;
    {
        // 关闭非tls的连接对象
        ws_con_ptr_->close(static_cast<websocketpp::close::status::value>(status), reason, ec);
    }
}

// 获取当前链接主动关闭连接所设置的关闭状态码
// 只有在 OnClose 回调时调用此函数有效
ws_close::status WebSocketClient::Connection::GetLocalCloseCode() const
{
    auto close_code = ws_con_ptr_->get_local_close_code();
    return static_cast<ws_close::status>(close_code);
}

// 获取当前链接主动关闭连接所设置的错误描述
// 只有在 OnClose 回调时调用此函数有效
std::string const& WebSocketClient::Connection::GetLocalCloseReason() const
{
    return ws_con_ptr_->get_local_close_reason();
}

// 获取当前链接被对端关闭时所设置的关闭状态码
// 只有在 OnClose 回调时调用此函数有效
ws_close::status WebSocketClient::Connection::GetRemoteCloseCode() const
{
    auto close_code = ws_con_ptr_->get_remote_close_code();
    return static_cast<ws_close::status>(close_code);
}

// 获取当前链接被对端关闭时所设置的错误描述
// 只有在 OnClose 回调时调用此函数有效
std::string const& WebSocketClient::Connection::GetRemoteCloseReason() const
{
    return ws_con_ptr_->get_remote_close_reason();
}

std::error_code WebSocketClient::Connection::GetErrorCode() const
{
    return ws_con_ptr_->get_ec();
}

const std::string& WebSocketClient::Connection::GetConnectionPath() const
{
    return connection_path_;
}

WebSocketClient::~WebSocketClient()
{
    Stop();
}

WebSocketClient::ConnectionPtr WebSocketClient::Connect(const std::string &path, uint32_t timeout_mill)
{
    websocketpp::lib::error_code ec;
    auto ws_con_ptr = ws_client_ptr_->get_connection(path, ec);
    if (ec)
    {
        return nullptr;
    }

    ConnectionPtr con_ptr(new Connection(ws_con_ptr, path));
    WriteLock lck(con_mtx_);
    con_map_.emplace(ws_con_ptr, con_ptr);
    lck.unlock();

    try
    {
        ws_client_ptr_->connect(ws_con_ptr);
        uint32_t wait_time = 0;
        while (ws_con_ptr->get_state() != websocketpp::session::state::value::open)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            wait_time += 100;
            if (wait_time >= timeout_mill)
            {
                break;
            }
        }

        if (ws_con_ptr->get_state() != websocketpp::session::state::value::open)
        {
            ws_con_ptr->close(websocketpp::close::status::normal, "", ec);
            return nullptr;
        }

        return con_ptr;
    }
    catch(...)
    {
        ws_con_ptr->close(websocketpp::close::status::normal, "", ec);
        return nullptr;
    }
}

void WebSocketClient::Start()
{
    ws_client_ptr_.reset(new ws_client);
    ws_client_ptr_->clear_access_channels(websocketpp::log::alevel::all);
    ws_client_ptr_->clear_error_channels(websocketpp::log::alevel::all);
    ws_client_ptr_->init_asio();

    auto OnMessage = [this](websocketpp::connection_hdl hdl, ws_client::message_ptr msg)
    {
        this->OnMessage_(hdl, msg.get());
    };

    namespace wp = websocketpp::lib::placeholders;
    ws_client_ptr_->set_message_handler(OnMessage);
    ws_client_ptr_->set_socket_init_handler(websocketpp::lib::bind(&WebSocketClient::OnSocketInit_, this, wp::_1, wp::_2));
    ws_client_ptr_->set_open_handler(websocketpp::lib::bind(&WebSocketClient::OnOpenHandler_, this, wp::_1));
    ws_client_ptr_->set_close_handler(websocketpp::lib::bind(&WebSocketClient::OnCloseHandler_, this, wp::_1));
    ws_client_ptr_->start_perpetual();
    ws_work_thr_ = std_thread("websocket client", "websocket client", std::bind(&WebSocketClient::Run, this));
}

void WebSocketClient::Run()
{
    try
    {
        // run
        {
            ws_client_ptr_->run();
        }
    }
    catch (const std::exception &err)
    {
        fprintf(stderr, "%s\n", err.what());
    }
}

void WebSocketClient::Stop()
{
    if (!is_stopped && ws_client_ptr_ != nullptr)
    {
        ws_client_ptr_->stop_perpetual();
        ws_client_ptr_->stop();
    }

    if (ws_work_thr_.joinable())
    {
        ws_work_thr_.join();
    }

    is_stopped = true;
}

void WebSocketClient::OnSocketInit_(connection_hdl hdl, boost::asio::ip::tcp::socket &socket)
{
    boost::asio::ip::tcp::no_delay option(true);
    socket.set_option(option);
}

void WebSocketClient::OnMessage_(connection_hdl hdl, void *msg)
{
    websocketpp::lib::error_code ec;
    auto ws_con_ptr = ws_client_ptr_->get_con_from_hdl(hdl, ec);
    if (ec)
    {
        return;
    }

    ReadLock lck(con_mtx_);
    auto it = con_map_.find(ws_con_ptr);
    {
        if (it == con_map_.end())
        {
            return;
        }
    }

    if (on_message)
    {
        try
        {
           auto mst_ptr = static_cast<ws_client::connection_type::message_type*>(msg);
           MessagePtr message;
           message.reset(new Message);
           message->opcode = static_cast<ws_opcode::value>(mst_ptr->get_opcode());
           message->payload = std::move(mst_ptr->get_raw_payload());
           on_message(it->second, message);
        }
        catch(...)
        {
        }
    }
}

void WebSocketClient::OnOpenHandler_(connection_hdl hdl)
{
    websocketpp::lib::error_code ec;
    auto ws_con_ptr = ws_client_ptr_->get_con_from_hdl(hdl, ec);
    if (ec)
    {
        return;
    }

    WriteLock lck(con_mtx_);
    auto it = con_map_.find(ws_con_ptr);
    {
        if (it == con_map_.end())
        {
            websocketpp::lib::error_code ec;
            ws_con_ptr->close(websocketpp::close::status::normal, "", ec);
            return;
        }
    }

    if (on_open)
    {
        try
        {
            on_open(it->second);
        }
        catch(...)
        {
        }
    }
}

void WebSocketClient::OnCloseHandler_(connection_hdl hdl)
{
    websocketpp::lib::error_code ec;
    auto ws_con_ptr = ws_client_ptr_->get_con_from_hdl(hdl, ec);
    if (ec)
    {
        return;
    }

    WriteLock lck(con_mtx_);
    auto it = con_map_.find(ws_con_ptr);
    if (it != con_map_.end())
    {
        if (on_close)
        {
            try
            {
                on_close(it->second);
            }
            catch(...)
            {
            }
        }

        con_map_.erase(it);
    }
}

}
}