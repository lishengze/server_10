#include "../../src/io_engine/socket_impl.h"
#include <iostream>
#include <unistd.h>
#include <boost/locale.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/program_options.hpp>
#include <adk/token_buckets.h>
#include <adk/arch/generic.h>
#include <string>
#include <thread>
#include <adk/io_engine/message.h>
#include <adk/io_engine/config_key.h>
#include <fcntl.h>

using namespace std;

bool g_is_running = true;

uint64_t min_;
uint64_t max_;
uint64_t total_;
uint64_t counter_;
bool reset_;

void Reset()
{
    min_ = 0xffffffff;
    max_ = 0;
    total_ = 0;
    counter_ = 0;
    reset_ = false;
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

char merge_buf_[4096] = { 0 };
uint32_t remain_len_ = 0;

void OnMessage(const char* data, const uint32_t length)
{

    if (ACCESS_ONCE(reset_))
    {
        Reset();
    }
    const char* consume_data = nullptr;
    uint32_t left_len = 0;

    if(remain_len_)
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


    struct timespec current_tp;
    clock_gettime(CLOCK_REALTIME, &current_tp);

    do
    {
        if (left_len < 4)
        {
            break;
        }

        const uint32_t data_len = *((uint32_t*)consume_data);
        if (left_len >= data_len)
        {
            struct timespec* message_tp = (struct timespec*)(consume_data + sizeof(uint32_t));
            const auto time_diff = current_tp.tv_sec * 1000000000 + current_tp.tv_nsec
                - message_tp->tv_sec * 1000000000 - message_tp->tv_nsec;

            min_ = std::min<uint64_t>(min_, time_diff);
            max_ = std::max<uint64_t>(max_, time_diff);
            total_ += time_diff;
            ++counter_;

            left_len -= data_len;
            consume_data += data_len;
        }
        else
        {
            break;
        }
    } while (true);

    if(left_len == 0)
    {
        return;
    }

    if(left_len > length)
    {
        cout << "BUG on left_len" << endl;
        exit(1);
    }
    std::memcpy(merge_buf_, consume_data, left_len);
    remain_len_ = left_len;
}
int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("remote-ip", boost::program_options::value<string>(), "set remote ip address")
        ("remote-port", boost::program_options::value<uint16_t>()->default_value(50000), "set remote port")
        ("local-port", boost::program_options::value<uint16_t>()->default_value(50001), "set remote port")
        ("message-size", boost::program_options::value<uint32_t>()->default_value(128), "set message size")
        ("transmit-rate", boost::program_options::value<double>()->default_value(100000), "set transmit rate")
        ("tx-low-latency", boost::program_options::value<bool>()->default_value(true), "set tx low latency")
        ("rx-low-latency", boost::program_options::value<bool>()->default_value(true), "set rx low latency")
        ("block", "set tcp blocking send")
        ("tcp-no-delay", "set tcp no delay")
        ;

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    if (vm.count("help") || !vm.count("remote-ip"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    const auto rate_limit = vm["transmit-rate"].as<double>();
    adk::TokenBucket* rate_control = adk::RateControl::GetInstance<adk::rate_unit::Second>(rate_limit, rate_limit < 3000001);
    if (nullptr == rate_control)
    {
        std::cout << "Create rate control failed" << std::endl;
        return 1;
    }

    adk::io_engine::CTcpEndpoint* tcp_client = new adk::io_engine::CTcpEndpoint;
    string remote_ip = vm["remote-ip"].as<string>();
    uint16_t remote_port = vm["remote-port"].as<uint16_t>();
    uint16_t local_port = vm["local-port"].as<uint16_t>();
    adk::io_engine::Property accept_props;
    accept_props
        (adk::io_engine::config::endpoint::kSocketSendBufferKBytes, 8192)
        (adk::io_engine::config::endpoint::kSocketRecvBufferKBytes, 8192)
        (adk::io_engine::config::endpoint::kTxMinResidentMicro, 10000000)
        (adk::io_engine::config::endpoint::kRxMinResidentMicro, 10000000)
        (adk::io_engine::config::endpoint::kTcpNoDelay, !!(vm.count("tcp-no-delay")));
    
    int user_flag = O_NONBLOCK;
    if(vm.count("block"))
    {
        user_flag = 0;
        cout << "blocking send" << endl;
    }
    if (true != tcp_client->Open("", local_port, false, user_flag))
    {
        cout << "Client open failed " << "0.0.0.0" << " : " << local_port << endl;
        return 1;
    }
    
    if (!tcp_client->SetOption(accept_props))
    {
        cout << "Set Options failed" << endl;
        return 1;
    }

    if( 0 != tcp_client->Connect(remote_ip, remote_port))
    {
        cout << "Client connect failed " << remote_ip << " : " << remote_port << endl;
        return 1;
    }
    
    const uint32_t msg_size = vm["message-size"].as<uint32_t>();
    char* send_buf = new char[msg_size];

    std::thread observe_handle = std::thread([&]() {
        char buff[1024] = { 0 };
        //msg->set_data_len(4096 - );
        do
        {
            int32_t length = tcp_client->Recv(buff, 1024);
            if (length > 0)
            {
                OnMessage(buff, length);
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
        } while (true);
    });

    std::thread statistics_handle = std::thread([&]() {
        do
        {
            sleep(1);
            PrintStatistics();
        } while (true);
    });

    uint32_t send_len = 0;
    while (g_is_running)
    {

    retry_acq:
        if (adk::ErrorCode::kSuccess != rate_control->TryAcquire(1))
        {
            for (uint32_t index = 0; index < 32; ++index)
            {
                ADK_PAUSE();
            }
            goto retry_acq;
        }

        *(uint32_t*)(send_buf) = msg_size;
        clock_gettime(CLOCK_REALTIME, (struct timespec*)(send_buf + sizeof(uint32_t)));
        send_len = 0;
        while (send_len < msg_size)
        {
            int ret = tcp_client->Send(send_buf + send_len, msg_size - send_len);
            if (-1 == ret)
            {
                cout << "send msg failed" << endl;
                return 1;
            }
            else
            {
                send_len += ret;
            }
        }
    }


    return 0;
}