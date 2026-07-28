#include <unistd.h>
#include <iostream>
#include <mutex>
#include <set>
#include <adk/web/adk_webscoket.h>

using namespace adk::web;

std::mutex con_list_mtx;
std::set<WebSocketServer::ConnectionPtr, std::owner_less<WebSocketServer::ConnectionPtr>> con_list;

void OnOpenHandler(WebSocketServer::ConnectionPtr con_ptr)
{
    // 可以在此判断 web scoekt 请求连接是否合法
    std::cout << con_ptr->GetRequest().path << std::endl;

    std::lock_guard<std::mutex> lck(con_list_mtx);
    con_list.insert(con_ptr);
}

void OnCloseHandler(WebSocketServer::ConnectionPtr con_ptr)
{
    std::cout << "close connection: " << con_ptr.get() << std::endl;
    std::lock_guard<std::mutex> lck(con_list_mtx);
    con_list.erase(con_ptr);
}

void OnMessageHandler(WebSocketServer::ConnectionPtr con_ptr, WebSocketServer::MessagePtr msg_ptr)
{
    std::cout << msg_ptr->payload  << std::endl;
    con_ptr->Send("hello from server", ws_opcode::text);
}

int main(int argc, char const *argv[])
{
    adk::web::WebSocketServer ws_server;
    ws_server.config.address = "127.0.0.1";
    ws_server.config.port = 8001;
    ws_server.on_open = OnOpenHandler;
    ws_server.on_close = OnCloseHandler;
    ws_server.on_message = OnMessageHandler;

    ws_server.Start();

    sleep(3600);

    ws_server.Stop();
    return 0;
}

