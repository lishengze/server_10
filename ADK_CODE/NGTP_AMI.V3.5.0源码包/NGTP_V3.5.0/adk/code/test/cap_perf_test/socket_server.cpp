#include "../../src/io_engine/socket_impl.h"
#include <iostream>
#include <unistd.h>
#include <boost/locale.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/program_options.hpp>
#include <adk/token_buckets.h>
#include <string>
#include <adk/io_engine/config_key.h>
#include <memory>

using namespace std;
using namespace adk::io_engine;

bool g_is_running = true;

enum ErrorCode
{
    kSuccess = 0,
    kFailure,
};

uint64_t g_min;
uint64_t g_max;
uint64_t g_total;
uint64_t g_recv_counter = 0;
bool g_reset;

void Reset()
{
    g_min = 0xffffffff;
    g_max = 0;
    g_total = 0;
    g_recv_counter = 0;
    g_reset = false;
}

class SocketHandle
{
public:
    virtual int32_t OnMessage(ITcpEndpoint* tcp_ep, const char* data, const uint32_t length) = 0;
};

template<typename CallbackType>
class MessageHandler: public SocketHandle
{
public:
    int32_t OnMessage(ITcpEndpoint* tcp_ep, const char* data, const uint32_t length) override
    {
        const char* consume_data = nullptr;
        uint32_t left_len = 0;

        if (remain_len_)
        {
            std::memcpy(merge_buf_ + remain_len_, data, length);
            consume_data = merge_buf_;
            left_len = remain_len_ + length;
        }
        else
        {
            consume_data = data;
            left_len = length;
        }

        do
        {
            if (left_len < 4)
            {
                break;
            }

            const uint32_t data_len = *((uint32_t*)consume_data);
            if (left_len >= data_len)
            {
                static_cast<CallbackType*>(this)->ProcessMsg(tcp_ep, consume_data, data_len);

                left_len -= data_len;
                consume_data += data_len;
            }
            else
            {
                break;
            }
        } while (true);

        if (left_len != 0)
        {
            if (left_len > length)
            {
                cout << "BUG on left_len" << endl;
                exit(1);
            }
            std::memcpy(merge_buf_, consume_data, left_len);
            remain_len_ = left_len;
        }
        return ErrorCode::kSuccess;
    }

private:
    char merge_buf_[4096] = { 0 };
    uint32_t remain_len_ = 0;

};

//ping-pong
class MessageHandler1 final : public MessageHandler<MessageHandler1>
{
public:
    void ProcessMsg(ITcpEndpoint* tcp_ep, const char* data, uint32_t length)
    {
        auto left_length = length;

    retry:
        const auto tx_res = tcp_ep->Send(data + length - left_length, left_length);
        if (tx_res > 0)
        {
            if (tx_res < left_length)
            {
                left_length -= tx_res;
                goto retry;
            }
        }
        else if (EWOULDBLOCK != errno)
        {
            std::cout << "socket error, " << strerror(errno) << std::endl;
        }
    }
};

class MessageHandler2 final : public MessageHandler<MessageHandler2>
{
public:
    MessageHandler2()
    {
        recv_counter_ = 0;
        statistics_thread_ = std::thread([&]() {
            uint64_t counter_rec = recv_counter_;
            do
            {
                sleep(1);
                const uint64_t temp_counter = *(volatile uint64_t*)(&recv_counter_);
                std::cout << "recv rate = " << temp_counter - counter_rec << std::endl;
                counter_rec = temp_counter;
            } while (true);
        });
    }

    void ProcessMsg(ITcpEndpoint* tcp_ep, const char* data, uint32_t length)
    {
        ++recv_counter_;
    }
private:
    uint64_t recv_counter_;
    std::thread statistics_thread_;
};

//receiver lantency test case
class MessageHandler3 final : public MessageHandler<MessageHandler3>
{
public:
    MessageHandler3()
    {
        Reset();
        counter_ = 0;
        statistics_thread_ = std::thread([&]() {
            uint64_t counter_rec = counter_;
            do
            {
                sleep(1);
                PrintStatistics();
            } while (true);
        });
    }

    void ProcessMsg(ITcpEndpoint* tcp_ep, const char* data, uint32_t length)
    {
        if (ACCESS_ONCE(reset_))
        {
            Reset();
        }
        struct timespec current_tp;
        clock_gettime(CLOCK_REALTIME, &current_tp);
        struct timespec* message_tp = (struct timespec*)(data + sizeof(uint32_t));
        const auto time_diff = current_tp.tv_sec * 1000000000 + current_tp.tv_nsec
            - message_tp->tv_sec * 1000000000 - message_tp->tv_nsec;

        min_ = std::min<uint64_t>(min_, time_diff);
        max_ = std::max<uint64_t>(max_, time_diff);
        total_ += time_diff;
        ++counter_;
    }
    void PrintStatistics()
    {
        const auto counter = ACCESS_ONCE(counter_);
        if (0 != counter)
        {
            std::cout.precision(3);
            std::cout << boost::posix_time::second_clock::local_time()
                << " | total:" << std::setw(10) << counter
                << " | avg(us):" << std::setw(10) << std::fixed << (double)ACCESS_ONCE(total_) / (double)(counter * 1000)
                << " | min(us):" << std::setw(10) << std::fixed << (double)ACCESS_ONCE(min_) / (double)1000
                << " | max(us):" << std::setw(10) << std::fixed << (double)ACCESS_ONCE(max_) / (double)1000
                << std::endl;
        }
        else
        {
            std::cout << boost::posix_time::second_clock::local_time()
                << " | total:" << std::setw(10) << 0
                << " | avg(us):" << std::setw(10) << "NA"
                << " | min(us):" << std::setw(10) << "NA"
                << " | max(us):" << std::setw(10) << "NA"
                << std::endl;
        }
        reset_ = true;
    }

private:
    void Reset()
    {
        min_ = 0xffffffff;
        max_ = 0;
        total_ = 0;
        counter_ = 0;
        reset_ = false;
    }

    uint64_t min_;
    uint64_t max_;
    uint64_t total_;
    uint64_t counter_;
    bool reset_;
    std::thread statistics_thread_;
};


int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("listen-ip", boost::program_options::value<string>()->default_value(string()), "set listen ip address")
        ("listen-port", boost::program_options::value<uint16_t>()->default_value(50000), "set listen port")
        ("mode", boost::program_options::value<uint32_t>()->default_value(1), "set test mode [1: pingpong / 2: throughput / 3: lantency]")
        ("tx-low-latency", boost::program_options::value<bool>()->default_value(true), "set tx low latency")
        ("rx-low-latency", boost::program_options::value<bool>()->default_value(true), "set rx low latency")
        ("block", "set tcp blocking send")
        ("tcp-no-delay", "set tcp no delay")
        ;

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    if (vm.count("help"))
    {
        std::cout << desc << std::endl;
        return 0;
    }
    uint32_t mode = vm["mode"].as<uint32_t>();
    std::unique_ptr<SocketHandle> message_handler;
    if (1 == mode)
    {
        message_handler.reset(new MessageHandler1);
    }
    else if (2 == mode)
    {
        message_handler.reset(new MessageHandler2);
    }
    else if (3 == mode)
    {
        message_handler.reset(new MessageHandler3);
    }
    else
    {
        cout << "invalid mode value" << endl;
        exit(1);
    }


    adk::io_engine::CTcpServer* tcp_server = new adk::io_engine::CTcpServer
;
    string listren_ip = vm["listen-ip"].as<string>();
    uint16_t listen_port = vm["listen-port"].as<uint16_t>();

    adk::io_engine::Property accept_props;
    accept_props
    (adk::io_engine::config::endpoint::kSocketSendBufferKBytes, 8192)
        (adk::io_engine::config::endpoint::kSocketRecvBufferKBytes, 8192)
        (adk::io_engine::config::endpoint::kTxMinResidentMicro, 10000000)
        (adk::io_engine::config::endpoint::kRxMinResidentMicro, 10000000)
        (adk::io_engine::config::endpoint::kTcpNoDelay, !!(vm.count("tcp-no-delay")));

    if (true != tcp_server->Open("", listen_port, false))
    {
        cout << "Client open failed <" << listren_ip << " : " << listen_port << "> " << endl;
        return 1;
    }

    int user_flag = O_NONBLOCK;
    if (vm.count("block"))
    {
        user_flag = 0;
        cout << "blocking send" << endl;
    }

    while (true)
    {
        adk::io_engine::ITcpEndpoint* tcp_ep = tcp_server->Accept(user_flag);
        if (nullptr == tcp_ep)
        {
            cout << "Server Accept failed " << listren_ip << " : " << listen_port << endl;
            return 1;
        }
        else
        {
            cout << "accept new connection" << endl;
        }
        if (!tcp_ep->SetOption(accept_props))
        {
            cout << "Set Options failed" << endl;
            return 1;
        }

        char buff[1024] = { 0 };
        while (g_is_running)
        {
            int32_t length = tcp_ep->Recv(buff, sizeof(buff));
            if (length > 0)
            {
                message_handler->OnMessage(tcp_ep, buff, length);
            }
            else if (0 == length)
            {
                cout << "peer is closed" << endl;
                break;
            }
            else if (EWOULDBLOCK != errno)
            {
                std::cout << "socket error, " << strerror(errno) << std::endl;
                break;
            }
        }
    }
    return 0;
}