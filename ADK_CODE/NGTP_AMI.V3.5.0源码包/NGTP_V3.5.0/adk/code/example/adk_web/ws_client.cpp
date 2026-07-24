#include <unistd.h>
#include <iostream>
#include <mutex>
#include <set>
#include <adk/web/adk_webscoket.h>

using namespace adk::web;

std::mutex con_list_mtx;
std::set<WebSocketClient::ConnectionPtr, std::owner_less<WebSocketClient::ConnectionPtr>> con_list;

void OnOpenHandler(WebSocketClient::ConnectionPtr con_ptr)
{
    std::cout << con_ptr->GetConnectionPath() << std::endl;

    std::lock_guard<std::mutex> lck(con_list_mtx);
    con_list.insert(con_ptr);
}

void OnCloseHandler(WebSocketClient::ConnectionPtr con_ptr)
{
    std::cout << "close connection: " << con_ptr.get() << std::endl;
    std::lock_guard<std::mutex> lck(con_list_mtx);
    con_list.erase(con_ptr);
}

void OnMessageHandler(WebSocketClient::ConnectionPtr con_ptr, WebSocketClient::MessagePtr msg_ptr)
{
    std::cout << msg_ptr->payload  << std::endl;
}

int main(int argc, char const *argv[])
{
    adk::web::WebSocketClient ws_client;
    ws_client.on_open = OnOpenHandler;
    ws_client.on_close = OnCloseHandler;
    ws_client.on_message = OnMessageHandler;
    ws_client.Start();

    auto con_ptr1 = ws_client.Connect("ws://127.0.0.1:8001", 6*10000000);
    auto con_ptr2 = ws_client.Connect("ws://127.0.0.1:8001/test_path", 6*1000);
    if (con_ptr1 == nullptr)
    {
        std::cout << "Connect failed" << std::endl;
        sleep(5);
        return 0;
    }

    while (true)
    {
        std::cout << "con_list size: " << con_list.size() << std::endl;
        con_ptr1->Send("hello from clinet 1", ws_opcode::text);
        sleep(1);
        con_ptr1->Send("hello from clinet 2", ws_opcode::text);
        sleep(2);
    }

    ws_client.Stop();
    return 0;
}

