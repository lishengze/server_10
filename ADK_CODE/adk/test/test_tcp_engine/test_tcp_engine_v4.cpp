#include "test_tcp_engine.h"

#include <time.h>

#include <vector>
#include <thread>
#include <string>

constexpr uint32_t kGroupMaxSize = 8;
constexpr uint32_t kGroupMaxSizeMask = kGroupMaxSize - 1;

using std::thread;
using std::vector;
using std::string;

class EventHandler final : public EventHandlerBase
{
public:
};

class AcceptHandler final : public AcceptHandlerBase
{
public:

};

class ConnectHandler final : public ConnectHandlerBase
{
public:
    void OnConnect(Endpoint* endpoint, Property& ep_props) override;
};

uint64_t g_recv_counter = 0;

class MessageHandler final : public adk::io_engine::MessageHandler
{
public:
    MessageHandler()
    {
        for (uint32_t index = 0; index < kGroupMaxSize; ++index)
        {
            expect_sqn_[index] = index;
        }
    }

    int32_t OnMessage(Message* message) override
    {
        const char* msg_data = message->const_data();
        uint32_t left_len = message->data_len();

        const char* consume_data = msg_data;
        while (left_len >= kPayloadSize)
        {
            Endpoint* const endpoint = message->endpoint();
            const uint64_t recv_data = *((uint64_t*)consume_data);
            const uint32_t sub_index = recv_data & kGroupMaxSizeMask;

            uint64_t& expect_value = expect_sqn_[sub_index];
            if (recv_data != expect_value)
            {
                string detail = (format("Endpoint<%1%:%2%> OnMessage: recv<%3%> expect<%4%>")
                    % endpoint->endpoint_id() % (sub_index + 1) % recv_data % expect_value).str();
                std::cout << detail << std::endl;
            }

            __sync_fetch_and_add(&g_recv_counter, 1);
            expect_value = recv_data + kGroupMaxSize;
            left_len -= kPayloadSize;
            consume_data += kPayloadSize;
        }

        if (0 != left_len)
        {
            message->set_follow_up(consume_data - msg_data, kPayloadSize - left_len);
            return adk::io_engine::MessageHandler::Result::kFollowUp;
        }

        return ErrorCode::kSuccess;
    }

private:
    uint64_t expect_sqn_[kGroupMaxSize];
};

class ClientEp
{
public:
    ClientEp(TcpEngine* const tcp_engine, const string& remote_ip, uint16_t remote_port)
    {
        tcp_engine_ = tcp_engine;
        client_name_ = (format("client<%1%:%2%>") % remote_ip % remote_port).str();
        client_props_(adk::io_engine::config::endpoint::kLocalIp, "192.168.102.218")
                     (adk::io_engine::config::endpoint::kRemoteIp, remote_ip)
                     (adk::io_engine::config::endpoint::kRemotePort, remote_port)                     
                     (adk::io_engine::config::endpoint::kMessageHandler, &message_handler_)
                     (adk::io_engine::config::endpoint::kHeartbeatTimeoutMilli, 2000)
            ;
        endpoint_size_ = 1;
        is_running_ = true;
    }

    virtual ~ClientEp() = default;

    bool Init()
    {
        for (uint32_t index = 0; index < endpoint_size_; ++index)
        {
            Endpoint* const endpoint = tcp_engine_->Connect(client_props_);
            if (nullptr != endpoint)
            {
                endpoint->set_share_ctx(this);
            }
            else
            {
                std::cout << client_name_ << " connect failed" << std::endl;
                return false;
            }
        }               

        return true;
    }

    virtual void Start(Endpoint* const endpoint)
    {
        thread_hdls_vec_.push_back(thread(&ClientEp::ClientThread, this, endpoint));
    }

    void Stop()
    {
        is_running_ = false;
        for (auto iter = thread_hdls_vec_.begin(); iter != thread_hdls_vec_.end(); ++iter)
        {
            iter->join();
        }
    }

protected:
    void ClientThread(Endpoint* const endpoint)
    {
        Endpoint* const scoped_endpoint = endpoint;
        const uint32_t sub_index = scoped_endpoint->sub_index();

        uint64_t send_data = sub_index - 1;
        while (is_running_)
        {
            Message* const message = scoped_endpoint->NewMessage(kPayloadSize);
            assert(message);

            *((uint64_t*)(message->data())) = send_data;            
            message->set_data_len(kPayloadSize);
            send_data += kGroupMaxSize;

            sleep(1);
        retry:
            const auto ec = scoped_endpoint->SendMsg(message);
            if (ADK_UNLIKELY(ErrorCode::kSuccess != ec))
            {
                goto retry;
            }
        }

        scoped_endpoint->Close();
    }

    volatile bool  is_running_;
    TcpEngine*     tcp_engine_;
    string         client_name_;
    Property       client_props_;
    MessageHandler message_handler_;
    uint32_t       endpoint_size_;    
    vector<thread> thread_hdls_vec_;
};

class ClientGroup : public ClientEp
{
public:
    ClientGroup(TcpEngine* const tcp_engine, const string& remote_ip, uint16_t remote_port) : ClientEp(tcp_engine, remote_ip, remote_port)
    {
        client_props_(adk::io_engine::config::endpoint::kIsSingleton, true)
                     (adk::io_engine::config::endpoint::kMessageHandler, &message_handler_)
            ;

        endpoint_size_ = (rand() % kGroupMaxSize) + 1;
    }
};

void ConnectHandler::OnConnect(Endpoint* endpoint, Property& ep_props)
{
    ConnectHandlerBase::OnConnect(endpoint, ep_props);
    ClientEp* const client_ep = (ClientEp*)endpoint->share_ctx();
    client_ep->Start(endpoint);
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cout << "please input the server ip and port" << std::endl;
        return -1;
    }

    const string server_ip = argv[1];
    const uint16_t server_port = atoi(argv[2]);

    EventHandler event_handler;
    AcceptHandler accept_handler;
    ConnectHandler connect_handler;

    Property engine_props;
    engine_props(adk::io_engine::config::kTxThreadNum, 1)
                (adk::io_engine::config::kRxThreadNum, 1)
                (adk::io_engine::config::kEventHandler, &event_handler)
                (adk::io_engine::config::kAcceptHandler, &accept_handler)
                (adk::io_engine::config::kConnectHandler, &connect_handler)
                (adk::io_engine::config::kMaxConnections, kMaxConnections)
        ;

    srand((unsigned)time(0));
    TcpEngine* const tcp_engine = TcpEngine::Create(engine_props);
    if (nullptr == tcp_engine)
    {
        std::cout << "Create tcp engine failed" << std::endl;
        return -1;
    }

    constexpr uint32_t kClientSize = 200;
    constexpr uint32_t kClientGroupSize = 1;

    ClientEp* client_eps[kClientSize];
    ClientGroup* client_groups[kClientGroupSize];

    for (uint32_t index = 0; index < kClientSize; ++index)
    {
        ClientEp* const client_ep = new ClientEp(tcp_engine, server_ip, server_port);
        client_ep->Init();
        client_eps[index] = client_ep;
    }

    for (uint32_t index = 0; index < kClientGroupSize; ++index)
    {
        ClientGroup* const client_group = new ClientGroup(tcp_engine, server_ip, server_port);
        client_group->Init();
        client_groups[index] = client_group;
    }

    uint64_t last_record = 0;
    for (uint32_t index = 0; index < 10; ++index)
    {
        sleep(1);
        const uint64_t temp_data = g_recv_counter;
        std::cout << "Recv message diff = " << temp_data - last_record << std::endl;
        last_record = temp_data;
    }

    for (uint32_t index = 0; index < kClientSize; ++index)
    {
        ClientEp* const client_ep = client_eps[index];
        client_ep->Stop();
        //delete client_ep;
    }

    for (uint32_t index = 0; index < kClientGroupSize; ++index)
    {
        ClientGroup* const client_group = client_groups[index];
        client_group->Stop();
        //delete client_group;
    }

    TcpEngine::Destroy(tcp_engine);
    
    return 0;
}