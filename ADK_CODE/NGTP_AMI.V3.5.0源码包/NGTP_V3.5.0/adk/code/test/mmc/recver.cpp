#include "multicast.h"

#include <stdint.h>

#include <vector>
#include <thread>
#include <iostream>
#include <boost/program_options.hpp>

struct RecverInfo
{
    MulticastRaw* multicast;
    volatile uint64_t* recv_nr;
    volatile uint64_t* lost_nr;

    uint64_t recv_nr_rec;
    uint64_t lost_nr_rec;
};

void TestThread(RecverInfo* recv_info)
{
    uint64_t recv_nr = 0;
    uint64_t lost_nr = 0;
    auto* const multicast = recv_info->multicast;
    recv_info->recv_nr = &recv_nr;
    recv_info->lost_nr = &lost_nr;

    char* buffer = new char[1024];
    do 
    {
        if (multicast->Recvfrom(buffer, 1024) > 0)
        {
            const uint64_t value = *reinterpret_cast<uint64_t*>(buffer);
            if (value != recv_nr + 1)
            {
                lost_nr += value - recv_nr - 1;
            }
            recv_nr = value;
        }
    } while (true);
}

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("message-ip", boost::program_options::value<std::string>()->default_value("127.0.0.1"), "for select interface")
        ("multicast-address", boost::program_options::value<std::string>()->default_value("239.239.239.239"), "set multicast address for test")
        ("multicast-port", boost::program_options::value<uint16_t>()->default_value(29998), "set multicast port for test")
        ("receiver-size", boost::program_options::value<uint32_t>()->default_value(16), "set test receiver number")
        ;

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    if (vm.count("help"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    const std::string message_ip = vm["message-ip"].as<std::string>();
    const std::string mc_addr = vm["multicast-address"].as<std::string>();
    const uint16_t mc_port = vm["multicast-port"].as<uint16_t>();

    std::cout << "message ip <" << message_ip << ">, multicast address <"
              << mc_addr << ":" << mc_port << ">" << std::endl;

    const uint32_t recver_size = vm["receiver-size"].as<uint32_t>();

    std::vector<RecverInfo> recver_info_vec(recver_size);
    std::vector<std::thread> recver_thread_vec;

    uint32_t recver_cnt = 0;
    for (; recver_cnt < recver_size; recver_cnt++)
    {
        auto* const mc_endpoint = MulticastRaw::Create(message_ip, mc_port);
        if (!mc_endpoint->JoinMcGroup(mc_addr))
        {
            std::cout << "Receiver <" << recver_cnt << "> JoinMcGroup <"
                      << mc_addr << "> failed, error info <" << strerror(errno) << ">" << std::endl;
            break;
        }

        auto recv_info_ptr = &recver_info_vec[recver_cnt];
        recv_info_ptr->multicast = mc_endpoint;
        recv_info_ptr->recv_nr = nullptr;
        recv_info_ptr->lost_nr = nullptr;
        recv_info_ptr->recv_nr_rec = 0;
        recv_info_ptr->lost_nr_rec = 0;

        recver_thread_vec.push_back(std::thread(TestThread, recv_info_ptr));
    }

    if (0 == recver_cnt)
    {
        std::cout << "all receivers are invalid" << std::endl;
        return -1;
    }

    do 
    {
        sleep(1);

        for (uint32_t recver_index = 0; recver_index < recver_cnt; ++recver_index)
        {
            auto recv_info_ptr = &recver_info_vec[recver_index];
            const auto recv_nr = *(recv_info_ptr->recv_nr);
            const auto lost_nr = *(recv_info_ptr->lost_nr);

            std::cout << "recv_index <" << recver_index << "> \t" 
                      << "recv_nr <" << recv_nr - recv_info_ptr->recv_nr_rec << "> \t"
                      << "lost_nr <" << lost_nr - recv_info_ptr->lost_nr_rec << ">" << std::endl;

            recv_info_ptr->recv_nr_rec = recv_nr;
            recv_info_ptr->lost_nr_rec = lost_nr;
        }
    } while (true);

    for (auto& thrd : recver_thread_vec)
    {
        thrd.join();
    }

    return 0;
}