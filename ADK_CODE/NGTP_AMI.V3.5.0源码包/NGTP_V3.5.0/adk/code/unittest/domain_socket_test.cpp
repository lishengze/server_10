#define BOOST_TEST_MODULE domain_socket
#include <boost/test/included/unit_test.hpp>

#include <string>
#include <iostream>

#include <adk/error_code.h>
#include <adk/entry_wrapper.h>

#include <adk/domain_socket.h>

using namespace adk;

struct ContextStatus
{
    bool is_leader = false;
    bool is_has_slave = false;
    bool is_quit = false;
};

std::string socket_name = "./domain_socket.text";
uint64_t total_msg_cnt = 10000;
uint64_t server_recv_cnt = 0;
uint64_t client_send_cnt = total_msg_cnt;

void StartServer()
{
    volatile bool is_running = true;
    std::string error_info;
    uint32_t max_message_size = sizeof(ContextStatus);
    char recv_data[max_message_size];
    uint32_t read_len = 0;

    UnixSocket* server = UnixSocket::CreateServerSocket(socket_name, error_info);
    BOOST_REQUIRE_MESSAGE(server != nullptr, error_info);

    while (is_running)
    {
        int ret = server->Accept(1000);
        if (ret != ErrorCode::kSuccess)
        {
            error_info = server->GetLastError();
            std::cout << "接收连接错误信息：" << error_info << std::endl;
            continue;
        }
        while (true)
        {
            int recv_ret = server->Recv(recv_data, max_message_size, read_len);
            // std::cout << "收到消息长度：" << recv_len << std::endl;
            if (recv_ret == ErrorCode::kWouldblock)
            {
                usleep(1000 * 1000);
                continue;
            }
            else if (recv_ret == ErrorCode::kFailure)
            {
                error_info = server->GetLastError();
                std::cout << "接收消息错误信息：" << error_info << std::endl;
                break;
            }

            ++server_recv_cnt;

            char* buff = new char[read_len];
            memcpy(buff, recv_data, read_len);

            ContextStatus* ctx_st = reinterpret_cast<ContextStatus*>(recv_data);
            // std::cout << "收到消息：{ leader: " << ctx_st->is_leader << ", is_slave: "
            //             << ctx_st->is_has_slave << ", is_quit: " << ctx_st->is_quit << "}" << std::endl;
            if (ctx_st->is_quit)
            {
                is_running = false;
            }
            delete[] buff;
        }
    }
    
    server->Close();
    std::cout << "处理完所有消息，正常退出" << std::endl;
    BOOST_REQUIRE_MESSAGE(server_recv_cnt == total_msg_cnt, "收到消息数量不正确");
}

void StartClient()
{
    int connect_cnt = 0;
    std::string error_info;
    uint32_t max_message_size = sizeof(ContextStatus);
    char send_data[max_message_size];

    UnixSocket* client = UnixSocket::CreateClientSocket(socket_name, error_info);
    BOOST_REQUIRE_MESSAGE(client != nullptr, error_info);

    do_connect:
    if (connect_cnt == 7)
    {
        return;
    }
    if (client->Connect(1000) != ErrorCode::kSuccess)
    {
        error_info = client->GetLastError();
        std::cout << "连接错误信息：" << error_info << std::endl;
        ++connect_cnt;
        std::cout << "连接Server服务失败, [" << connect_cnt << "/6]" << std::endl;
        // 3s后再次尝试连接
        sleep(3);
        goto do_connect;
    }

    ContextStatus ctx_data;
    while (client_send_cnt != 0)
    {
        ctx_data.is_leader = true;
        ctx_data.is_has_slave = true;
        ctx_data.is_quit = false;
        if (client_send_cnt == 1)
        {
            ctx_data.is_quit = true;
        }
        
        memcpy(send_data, &ctx_data, sizeof(ContextStatus));
        
        resend:
        uint32_t write_len = 0;
        int32_t send_ret = client->Send(send_data, sizeof(ContextStatus), write_len);
        if (send_ret == ErrorCode::kWouldblock)
        {
            usleep(1000);
            goto resend;
        }
        else if (send_ret == ErrorCode::kFailure)
        {
            error_info = client->GetLastError();
            std::cout << "发送消息错误：" << error_info << std::endl;
            break;
        }
        BOOST_REQUIRE_MESSAGE(write_len == sizeof(ContextStatus), "实际发送的字节数不等于预期");

        --client_send_cnt;
        // std::cout << "发送消息：{ leader: " << ctx_data.is_leader << ", is_slave: "
        //             << ctx_data.is_has_slave << ", is_quit: " << ctx_data.is_quit << "}" << std::endl;
    }
    while (server_recv_cnt != total_msg_cnt)
    {
        usleep(10 * 1000);
    }
    client->Close();
    std::cout << "发送完所有消息，正常退出" << std::endl;
    BOOST_REQUIRE_MESSAGE(client_send_cnt == 0, "存在发送消息失败");
}

BOOST_AUTO_TEST_CASE(test_connect)
{
    std::thread server_thd = adk::std_thread("domain socket server", 
                                             "start a domain socket server", std::bind(StartServer));
    
    sleep(3);

    std::thread client_thd = adk::std_thread("domain socket clienr",
                                              "start a domain socket client", std::bind(StartClient));

    if (client_thd.joinable())
    {
        client_thd.join();
    }
    
    if (server_thd.joinable())
    {
        server_thd.join();
    }
    
}

BOOST_AUTO_TEST_CASE(no_reciver_quit)
{
    std::cout << "no_reciver_quit case start" << std::endl;
    std::string error_info;
    UnixSocket* server = UnixSocket::CreateServerSocket(socket_name, error_info);
    BOOST_REQUIRE_MESSAGE(server != nullptr, error_info);

    int ret = server->Accept(1000);
    BOOST_REQUIRE_MESSAGE(ret != ErrorCode::kSuccess, "Accept需要超时失败");
    if (ret != ErrorCode::kSuccess)
    {
        error_info = server->GetLastError();
        std::cout << "接收连接错误信息：" << error_info << std::endl;
    }
    std::cout << "no_reciver_quit case success" << std::endl;
}
