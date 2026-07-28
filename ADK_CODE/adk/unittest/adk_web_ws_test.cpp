#define BOOST_TEST_MODULE adh_web_test
#include <arpa/inet.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <assert.h>
#include <random>
#include <functional>
#include <fstream>
#include <mutex>

#include <boost/test/included/unit_test.hpp>

#include <adk/web/adk_webscoket.h>

#ifndef adk
#define adk adk_impl
#endif

static uint16_t GetRandPort()
{
    static std::default_random_engine random_engine(time(nullptr));
    static std::uniform_int_distribution<unsigned> uniform(4000, 65535);
    static std::string addr = "0.0.0.0";
    static char addr_arry[sizeof(struct in_addr)];

    inet_pton(AF_INET, addr.c_str(), addr_arry);

    while (true)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
        {
            continue;
        }

        auto port = uniform(random_engine);
        struct sockaddr_in sock_addr;
        bzero(&sock_addr, sizeof(sock_addr));
        sock_addr.sin_family = AF_INET;
        sock_addr.sin_port   = htons(port);
        memcpy(addr_arry, &sock_addr.sin_addr, sizeof(struct in_addr));
        if (bind(sock, (struct sockaddr*)&sock_addr, sizeof(sock_addr)) != 0)
        {
            close(sock);
            continue;
        }

        close(sock);
        return port;
    }
}

BOOST_AUTO_TEST_CASE(adk_websocket_test1)
{
    //signal(SIGABRT,SIG_DFL);

    auto port = GetRandPort();
    std::string ws_addr = "ws://127.0.0.1:";
    ws_addr.append(std::to_string(port));
    adk::web::WebSocketServer ws_server;
    ws_server.config.address = "127.0.0.1";
    ws_server.config.port = port;
    std::mutex ws_server_con_set_mtx;
    std::set<adk::web::WebSocketServer::ConnectionPtr, std::owner_less<adk::web::WebSocketServer::ConnectionPtr>> ws_server_con_set;
    ws_server.on_open = [&](adk::web::WebSocketServer::ConnectionPtr con_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_server_con_set_mtx);
        ws_server_con_set.insert(con_ptr);
    };

    ws_server.on_close = [&](adk::web::WebSocketServer::ConnectionPtr con_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_server_con_set_mtx);
        ws_server_con_set.erase(con_ptr);
    };

    ws_server.on_message = [&](adk::web::WebSocketServer::ConnectionPtr con_ptr, adk::web::WebSocketServer::MessagePtr msg_ptr)
    {
        con_ptr->Send(msg_ptr->payload, msg_ptr->opcode);
    };

    BOOST_REQUIRE_NO_THROW(ws_server.Start());

    adk::web::WebSocketClient ws_client;
    std::mutex ws_client_con_set_mtx;
    std::set<adk::web::WebSocketClient::ConnectionPtr, std::owner_less<adk::web::WebSocketClient::ConnectionPtr>> ws_client_con_set;

    ws_client.on_open = [&](adk::web::WebSocketClient::ConnectionPtr con_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_client_con_set_mtx);
        ws_client_con_set.insert(con_ptr);
    };

    ws_client.on_close = [&](adk::web::WebSocketClient::ConnectionPtr con_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_client_con_set_mtx);
        ws_client_con_set.erase(con_ptr);
    };

    ws_client.on_message = [&](adk::web::WebSocketClient::ConnectionPtr con_ptr, adk::web::WebSocketClient::MessagePtr msg_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_client_con_set_mtx);
        if (ws_client_con_set.find(con_ptr) == ws_client_con_set.end())
        {
            BOOST_REQUIRE(false);
        }

        if (msg_ptr->opcode != adk::web::ws_opcode::text)
        {
            BOOST_REQUIRE(false);
        }

        if (msg_ptr->payload != "test")
        {
            BOOST_REQUIRE(false);
        }
    };

    BOOST_REQUIRE_NO_THROW(ws_client.Start());
    adk::web::WebSocketClient::ConnectionPtr con_ptr;
    BOOST_REQUIRE_NO_THROW(con_ptr = ws_client.Connect(ws_addr, 6*1000));
    BOOST_REQUIRE(con_ptr != nullptr);
    con_ptr->Send("test", adk::web::ws_opcode::text);

    ws_client.Stop();
    ws_server.Stop();
}

BOOST_AUTO_TEST_CASE(adk_websocket_test2)
{
    //signal(SIGABRT, SIG_DFL);
    auto port = GetRandPort();
    std::string ws_addr = "ws://127.0.0.1:";
    ws_addr.append(std::to_string(port));
    adk::web::WebSocketServer ws_server;
    ws_server.config.address = "127.0.0.1";
    ws_server.config.port = port;
    ws_server.config.thread_pool_size = 10;
    std::mutex ws_server_con_set_mtx;
    std::set<adk::web::WebSocketServer::ConnectionPtr, std::owner_less<adk::web::WebSocketServer::ConnectionPtr>> ws_server_con_set;
    ws_server.on_open = [&](adk::web::WebSocketServer::ConnectionPtr con_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_server_con_set_mtx);
        ws_server_con_set.insert(con_ptr);
    };

    ws_server.on_close = [&](adk::web::WebSocketServer::ConnectionPtr con_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_server_con_set_mtx);
        ws_server_con_set.erase(con_ptr);
    };

    ws_server.on_message = [&](adk::web::WebSocketServer::ConnectionPtr con_ptr, adk::web::WebSocketServer::MessagePtr msg_ptr)
    {
        con_ptr->Send(msg_ptr->payload, msg_ptr->opcode);
    };

    BOOST_REQUIRE_NO_THROW(ws_server.Start());

    adk::web::WebSocketClient ws_client;
    std::mutex ws_client_con_set_mtx;
    std::set<adk::web::WebSocketClient::ConnectionPtr, std::owner_less<adk::web::WebSocketClient::ConnectionPtr>> ws_client_con_set;
    ws_client.on_open = [&](adk::web::WebSocketClient::ConnectionPtr con_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_client_con_set_mtx);
        ws_client_con_set.insert(con_ptr);
    };

    ws_client.on_close = [&](adk::web::WebSocketClient::ConnectionPtr con_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_client_con_set_mtx);
        ws_client_con_set.erase(con_ptr);
    };

    ws_client.on_message = [&](adk::web::WebSocketClient::ConnectionPtr con_ptr, adk::web::WebSocketClient::MessagePtr msg_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_client_con_set_mtx);
        if (ws_client_con_set.find(con_ptr) == ws_client_con_set.end())
        {
            BOOST_REQUIRE(false);
        }

        if (msg_ptr->opcode != adk::web::ws_opcode::text)
        {
            BOOST_REQUIRE(false);
        }

        if (msg_ptr->payload != "test")
        {
            BOOST_REQUIRE(false);
        }
    };

    BOOST_REQUIRE_NO_THROW(ws_client.Start());

    std::vector<std::thread> thr_vec;
    thr_vec.reserve(10);

    for (int i = 0; i < 10; i++)
    {
        thr_vec.emplace_back(std::thread([&]()
        {
            adk::web::WebSocketClient::ConnectionPtr con_ptr;
            con_ptr = ws_client.Connect(ws_addr, 120*1000);
            if (con_ptr == nullptr)
            {
                BOOST_REQUIRE(false);
            }
            con_ptr->Send("test", adk::web::ws_opcode::text);
        }));
    }

    for (auto &thr : thr_vec)
    {
        thr.join();
    }

    ws_server_con_set_mtx.lock();
    ws_client_con_set_mtx.lock();
    BOOST_REQUIRE(ws_server_con_set.size() == 10);
    BOOST_REQUIRE(ws_client_con_set.size() == 10);
    ws_server_con_set_mtx.unlock();
    ws_client_con_set_mtx.unlock();

    std::set<adk::web::WebSocketClient::Connection*> tmp_ws_client_con_set;
    for (auto &con : ws_client_con_set)
    {
        tmp_ws_client_con_set.insert(con.get());
    }
    for (auto &con : tmp_ws_client_con_set)
    {
        con->Close(adk::web::ws_close::normal, "");
    }

    sleep(3);
    ws_client.Stop();
    ws_server.Stop();

    ws_server_con_set_mtx.lock();
    ws_client_con_set_mtx.lock();
    BOOST_REQUIRE(ws_server_con_set.empty());
    BOOST_REQUIRE(ws_client_con_set.empty());
}

BOOST_AUTO_TEST_CASE(adk_websocket_test3)
{
    //signal(SIGABRT,SIG_DFL);

    auto port = GetRandPort();
    std::string ws_addr = "ws://127.0.0.1:";
    ws_addr.append(std::to_string(port));
    adk::web::WebSocketServer ws_server;
    ws_server.config.address = "127.0.0.1";
    ws_server.config.port = port;
    std::mutex ws_server_con_set_mtx;
    std::set<adk::web::WebSocketServer::ConnectionPtr, std::owner_less<adk::web::WebSocketServer::ConnectionPtr>> ws_server_con_set;
    ws_server.on_open = [&](adk::web::WebSocketServer::ConnectionPtr con_ptr)
    {
        if (con_ptr->GetRequest().path != "/test")
        {
            BOOST_REQUIRE(false);
        }

        std::lock_guard<std::mutex> lck(ws_server_con_set_mtx);
        ws_server_con_set.insert(con_ptr);
    };

    ws_server.on_close = [&](adk::web::WebSocketServer::ConnectionPtr con_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_server_con_set_mtx);
        ws_server_con_set.erase(con_ptr);
    };

    ws_server.on_message = [&](adk::web::WebSocketServer::ConnectionPtr con_ptr, adk::web::WebSocketServer::MessagePtr msg_ptr)
    {
        con_ptr->Send(msg_ptr->payload, msg_ptr->opcode);
    };

    BOOST_REQUIRE_NO_THROW(ws_server.Start());

    adk::web::WebSocketClient ws_client;

    BOOST_REQUIRE_NO_THROW(ws_client.Start());
    adk::web::WebSocketClient::ConnectionPtr con_ptr;

    ws_addr.append("/test");
    BOOST_REQUIRE_NO_THROW(con_ptr = ws_client.Connect(ws_addr, 6*1000));
    BOOST_REQUIRE(con_ptr != nullptr);
    ws_client.Stop();
    ws_server.Stop();
}


/*begin
 *  @desc
 *      测试主动关闭链接所提供的错误码，对端能够获取
 *
 *  @precondition
 *      无
 * 
 *  @steps
 *      1.启动 websocket server，启动 websocket client
 *      2.client 建立链接到 server
 *      3.client 关闭链接，提供错误码以及信息
 *      4.server 检验证查错误码、描述信息是否符合预期
 * 
 *  @expect
 *      server 检验证查错误码、描述信息符合预期
 * 
 *  @author
 *      龚锋恒
 */
BOOST_AUTO_TEST_CASE(error_code_test1)
{
    //signal(SIGABRT,SIG_DFL);

    auto port = GetRandPort();
    std::string ws_addr = "ws://127.0.0.1:";
    ws_addr.append(std::to_string(port));
    adk::web::WebSocketServer ws_server;
    ws_server.config.address = "127.0.0.1";
    ws_server.config.port = port;
    std::mutex ws_server_con_set_mtx;
    std::set<adk::web::WebSocketServer::ConnectionPtr, std::owner_less<adk::web::WebSocketServer::ConnectionPtr>> ws_server_con_set;
    ws_server.on_open = [&](adk::web::WebSocketServer::ConnectionPtr con_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_server_con_set_mtx);
        ws_server_con_set.insert(con_ptr);
    };

    ws_server.on_close = [&](adk::web::WebSocketServer::ConnectionPtr con_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_server_con_set_mtx);
        BOOST_REQUIRE(con_ptr->GetRemoteCloseCode() == adk::web::ws_close::status::normal);
        BOOST_REQUIRE(con_ptr->GetRemoteCloseReason() == "test for close at client");
        ws_server_con_set.erase(con_ptr);
    };

    ws_server.on_message = [&](adk::web::WebSocketServer::ConnectionPtr con_ptr, adk::web::WebSocketServer::MessagePtr msg_ptr)
    {
        con_ptr->Send(msg_ptr->payload, msg_ptr->opcode);
    };

    BOOST_REQUIRE_NO_THROW(ws_server.Start());

    adk::web::WebSocketClient ws_client;
    std::mutex ws_client_con_set_mtx;
    std::set<adk::web::WebSocketClient::ConnectionPtr, std::owner_less<adk::web::WebSocketClient::ConnectionPtr>> ws_client_con_set;

    ws_client.on_open = [&](adk::web::WebSocketClient::ConnectionPtr con_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_client_con_set_mtx);
        ws_client_con_set.insert(con_ptr);
    };

    ws_client.on_close = [&](adk::web::WebSocketClient::ConnectionPtr con_ptr)
    {    
        std::lock_guard<std::mutex> lck(ws_client_con_set_mtx);
        BOOST_REQUIRE(con_ptr->GetLocalCloseCode() == adk::web::ws_close::status::normal);
        BOOST_REQUIRE(con_ptr->GetLocalCloseReason() == "test for close at client");
        ws_client_con_set.erase(con_ptr);
    };

    ws_client.on_message = [&](adk::web::WebSocketClient::ConnectionPtr con_ptr, adk::web::WebSocketClient::MessagePtr msg_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_client_con_set_mtx);
        if (ws_client_con_set.find(con_ptr) == ws_client_con_set.end())
        {
            BOOST_REQUIRE(false);
        }

        if (msg_ptr->opcode != adk::web::ws_opcode::text)
        {
            BOOST_REQUIRE(false);
        }

        if (msg_ptr->payload != "test")
        {
            BOOST_REQUIRE(false);
        }
    };

    BOOST_REQUIRE_NO_THROW(ws_client.Start());
    adk::web::WebSocketClient::ConnectionPtr con_ptr;
    BOOST_REQUIRE_NO_THROW(con_ptr = ws_client.Connect(ws_addr, 6*1000));
    BOOST_REQUIRE(con_ptr != nullptr);
    con_ptr->Send("test", adk::web::ws_opcode::text);
    con_ptr->Close(adk::web::ws_close::status::normal, "test for close at client");

    while (ws_server_con_set.size() != 0)
    {
        usleep(200 * 1000);
    }

    ws_client.Stop();
    ws_server.Stop();
}

/*begin
 *  @desc
 *      测试主动关闭链接所提供的错误码，对端能够获取
 *
 *  @precondition
 *      无
 * 
 *  @steps
 *      1.启动 websocket server，启动 websocket client
 *      2.client 建立链接到 server
 *      3.server 关闭链接，提供错误码以及信息
 *      4.client 检验证查错误码、描述信息是否符合预期
 * 
 *  @expect
 *      client 检验证查错误码、描述信息符合预期
 * 
 *  @author
 *      龚锋恒
 */
BOOST_AUTO_TEST_CASE(error_code_test2)
{
    //signal(SIGABRT,SIG_DFL);

    auto port = GetRandPort();
    std::string ws_addr = "ws://127.0.0.1:";
    ws_addr.append(std::to_string(port));
    adk::web::WebSocketServer ws_server;
    ws_server.config.address = "127.0.0.1";
    ws_server.config.port = port;
    std::mutex ws_server_con_set_mtx;
    bool is_on_message = false;
    std::set<adk::web::WebSocketServer::ConnectionPtr, std::owner_less<adk::web::WebSocketServer::ConnectionPtr>> ws_server_con_set;
    ws_server.on_open = [&](adk::web::WebSocketServer::ConnectionPtr con_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_server_con_set_mtx);
        ws_server_con_set.insert(con_ptr);
    };

    ws_server.on_close = [&](adk::web::WebSocketServer::ConnectionPtr con_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_server_con_set_mtx);
        BOOST_REQUIRE(con_ptr->GetLocalCloseCode() == adk::web::ws_close::status::normal);
        BOOST_REQUIRE(con_ptr->GetLocalCloseReason() == "test for close at server");
        ws_server_con_set.erase(con_ptr);
    };

    ws_server.on_message = [&](adk::web::WebSocketServer::ConnectionPtr con_ptr, adk::web::WebSocketServer::MessagePtr msg_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_server_con_set_mtx);
        con_ptr->Send(msg_ptr->payload, msg_ptr->opcode);
        is_on_message = true;
    };

    BOOST_REQUIRE_NO_THROW(ws_server.Start());

    adk::web::WebSocketClient ws_client;
    std::mutex ws_client_con_set_mtx;
    std::set<adk::web::WebSocketClient::ConnectionPtr, std::owner_less<adk::web::WebSocketClient::ConnectionPtr>> ws_client_con_set;

    ws_client.on_open = [&](adk::web::WebSocketClient::ConnectionPtr con_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_client_con_set_mtx);
        ws_client_con_set.insert(con_ptr);
    };

    ws_client.on_close = [&](adk::web::WebSocketClient::ConnectionPtr con_ptr)
    {    
        std::lock_guard<std::mutex> lck(ws_client_con_set_mtx);
        BOOST_REQUIRE(con_ptr->GetLocalCloseCode() == adk::web::ws_close::status::normal);
        BOOST_REQUIRE(con_ptr->GetLocalCloseReason() == "test for close at server");
        ws_client_con_set.erase(con_ptr);
    };

    ws_client.on_message = [&](adk::web::WebSocketClient::ConnectionPtr con_ptr, adk::web::WebSocketClient::MessagePtr msg_ptr)
    {
        std::lock_guard<std::mutex> lck(ws_client_con_set_mtx);
        if (ws_client_con_set.find(con_ptr) == ws_client_con_set.end())
        {
            BOOST_REQUIRE(false);
        }

        if (msg_ptr->opcode != adk::web::ws_opcode::text)
        {
            BOOST_REQUIRE(false);
        }

        if (msg_ptr->payload != "test")
        {
            BOOST_REQUIRE(false);
        }
    };

    BOOST_REQUIRE_NO_THROW(ws_client.Start());
    adk::web::WebSocketClient::ConnectionPtr con_ptr;
    BOOST_REQUIRE_NO_THROW(con_ptr = ws_client.Connect(ws_addr, 6*1000));
    BOOST_REQUIRE(con_ptr != nullptr);
    con_ptr->Send("test", adk::web::ws_opcode::text);

    while (true)
    {
        ws_client_con_set_mtx.lock();
        if (is_on_message)
        {
            ws_client_con_set_mtx.unlock();
            break;
        }
        ws_client_con_set_mtx.unlock();
        usleep(200 * 1000);
    }

    BOOST_REQUIRE(ws_server_con_set.size() == 1);
    (*ws_server_con_set.begin())->Close(adk::web::ws_close::status::normal, "test for close at server");

    while (ws_client_con_set.size() != 0)
    {
        usleep(200 * 1000);
    }

    ws_client.Stop();
    ws_server.Stop();
}