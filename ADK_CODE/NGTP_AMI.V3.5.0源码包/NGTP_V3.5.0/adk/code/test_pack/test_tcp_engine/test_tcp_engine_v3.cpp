#include "test_tcp_engine.h"

#include <time.h>

#include <vector>
#include <thread>
#include <string>

constexpr uint32_t kGroupMaxSize = 4;
constexpr uint32_t kGroupMaxSizeMask = kGroupMaxSize - 1;

using std::thread;
using std::vector;
using std::string;

class EventHandler final : public EventHandlerBase
{
public:
};

class ServerEp;
class AcceptHandler final : public AcceptHandlerBase
{
public:
    void OnAccept(Endpoint* endpoint, Property& ep_props) override;
    ServerEp* server_ep_;
};

class ConnectHandler final : public ConnectHandlerBase
{
public:
    void OnConnect(Endpoint* endpoint, Property& ep_props) override;
};

class MessageHandler final : public adk::io_engine::MessageHandler
{
public:
    MessageHandler()
    {
        for (uint32_t index = 0; index < kGroupMaxSize; ++index)
        {
            expect_sqn_[index] = index;
        }

        recv_counter_ = 0;
    }

    int32_t OnMessage(Message* message) override
    {
        const char* msg_data = message->const_data();
        uint32_t left_len = message->data_len();

        const char* consume_data = msg_data;
        while (left_len >= kPayloadSize)
        {
            const uint64_t recv_data = *((uint64_t*)consume_data);
            uint32_t sub_index = (recv_data & kGroupMaxSizeMask);
            uint64_t& expect_value = expect_sqn_[sub_index];

            if (recv_data != expect_value)
            {
                Endpoint* const endpoint = message->endpoint();
                string detail = (format("Endpoint<%1%:%2%> OnMessage: recv<%3%> expect<%4%>")
                    % endpoint->endpoint_id() % (sub_index+1) % recv_data % expect_value).str();
                std::cout << detail << std::endl;
            }

            ++recv_counter_;
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

    uint64_t recv_counter() const
    {
        return recv_counter_;
    }

private:
    uint64_t recv_counter_;
    uint64_t expect_sqn_[kGroupMaxSize];
};

class ServerEp
{
public:
    ServerEp(TcpEngine* const tcp_engine, const string& listen_ip, uint16_t listen_port)
    {
        tcp_engine_ = tcp_engine;
        client_name_ = (format("server<%1%:%2%>") % listen_ip % listen_port).str();
        client_props_(adk::io_engine::config::endpoint::kListenIp, listen_ip)
                     (adk::io_engine::config::endpoint::kListenPort, listen_port)
            ;
    }

    bool Init()
    {
        acceptor_ = tcp_engine_->Accept(client_props_);
        if (nullptr == acceptor_)
        {
            std::cout << client_name_ << "accept failed" << std::endl;
            return false;
        }

        return true;
    }

    void OnAccept(Endpoint* endpoint, Property& ep_props)
    {
        MessageHandler* const message_handler = new MessageHandler();
        endpoints_vec_.push_back(std::make_pair(endpoint, message_handler));
        ep_props(adk::io_engine::config::endpoint::kMessageHandler, message_handler);
    }

    void Stop()
    {
        acceptor_->Close();
        for (auto iter = endpoints_vec_.begin(); iter != endpoints_vec_.end(); ++iter)
        {
            iter->first->Close();
        }
    }

    vector<std::pair<Endpoint*, MessageHandler*>>& endpoints_vec()
    {
        return endpoints_vec_;
    }

private:
    Acceptor*  acceptor_;
    TcpEngine* tcp_engine_;
    string     client_name_;
    Property   client_props_;
    vector<std::pair<Endpoint*, MessageHandler*>> endpoints_vec_;
};

void AcceptHandler::OnAccept(Endpoint* endpoint, Property& ep_props)
{
    AcceptHandlerBase::OnAccept(endpoint, ep_props);
    server_ep_->OnAccept(endpoint, ep_props);
}

class ClientEp
{
public:
    ClientEp(TcpEngine* const tcp_engine, const string& remote_ip, uint16_t remote_port)
    {
        tcp_engine_ = tcp_engine;
        client_name_ = (format("client<%1%:%2%>") % remote_ip % remote_port).str();
        client_props_(adk::io_engine::config::endpoint::kRemoteIp, remote_ip)
                     (adk::io_engine::config::endpoint::kRemotePort, remote_port)
                     (adk::io_engine::config::endpoint::kHeartbeatTimeoutMilli, 10000)
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
    uint32_t       endpoint_size_;    
    vector<thread> thread_hdls_vec_;
};

class ClientGroup : public ClientEp
{
public:
    ClientGroup(TcpEngine* const tcp_engine, const string& remote_ip, uint16_t remote_port) : ClientEp(tcp_engine, remote_ip, remote_port)
    {
        client_props_(adk::io_engine::config::endpoint::kIsSingleton, true);        
        endpoint_size_ = (rand() % kGroupMaxSize) + 1;
    }
};

void ConnectHandler::OnConnect(Endpoint* endpoint, Property& ep_props)
{
    ConnectHandlerBase::OnConnect(endpoint, ep_props);
    ClientEp* const client_ep = (ClientEp*)endpoint->share_ctx();
    client_ep->Start(endpoint);
}

int main()
{
    EventHandler event_handler;
    AcceptHandler accept_handler;
    ConnectHandler connect_handler;

    Property engine_props;
    engine_props(adk::io_engine::config::kTxThreadNum, 2)
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

    constexpr uint32_t kClientSize = 1;
    constexpr uint32_t kClientGroupSize = 1;

    const string kListenIp = "127.0.0.1";
    constexpr uint16_t kListenPort = 60000;

    const string kRemoteIp = "127.0.0.1";

    ClientEp* client_eps[kClientSize];
    ClientGroup* client_groups[kClientGroupSize];

    ServerEp* const server_eps = new ServerEp(tcp_engine, kListenIp, kListenPort);
    assert(server_eps);

    server_eps->Init();

    accept_handler.server_ep_ = server_eps;
    usleep(100000);

    for (uint32_t index = 0; index < kClientSize; ++index)
    {
        ClientEp* const client_ep = new ClientEp(tcp_engine, kRemoteIp, kListenPort);
        client_ep->Init();
        client_eps[index] = client_ep;
    }

    for (uint32_t index = 0; index < kClientGroupSize; ++index)
    {
        ClientGroup* const client_group = new ClientGroup(tcp_engine, kRemoteIp, kListenPort);
        client_group->Init();
        client_groups[index] = client_group;
    }

    uint64_t last_record = 0;
    auto& endpoints_vec = server_eps->endpoints_vec();
    for (uint32_t index = 0; index < 100; ++index)
    {
        sleep(1);
        uint64_t temp_data = 0;
        for (auto iter = endpoints_vec.begin(); iter != endpoints_vec.end(); ++iter)
        {
            temp_data += iter->second->recv_counter();
        }

        std::cout << "Recv message diff = " << temp_data - last_record << std::endl;
        last_record = temp_data;
    }

    for (uint32_t index = 0; index < kClientSize; ++index)
    {
        ClientEp* const client_ep = client_eps[index];
        client_ep->Stop();
        delete client_ep;
    }

    for (uint32_t index = 0; index < kClientGroupSize; ++index)
    {
        ClientGroup* const client_group = client_groups[index];
        client_group->Stop();
        delete client_group;
    }

    server_eps->Stop();

    TcpEngine::Destroy(tcp_engine);

    delete server_eps;
    
    return 0;
}
