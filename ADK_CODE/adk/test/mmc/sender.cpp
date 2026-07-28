#include "multicast.h"

#include <stdint.h>

#include <thread>
#include <iostream>

#include <adk/token_buckets.h>
#include <boost/program_options.hpp>

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("message-ip", boost::program_options::value<std::string>()->default_value("127.0.0.1"), "for select interface")
        ("multicast-address", boost::program_options::value<std::string>()->default_value("239.239.239.239"), "set multicast address for test")
        ("multicast-port", boost::program_options::value<uint16_t>()->default_value(29998), "set multicast port for test")
        ("message-size", boost::program_options::value<uint32_t>()->default_value(128), "set message size")
        ("transmit-rate", boost::program_options::value<double>()->default_value(100000), "set transmit rate")
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

    auto* const mc_endpoint = MulticastRaw::Create(message_ip, mc_port);
    if (nullptr == mc_endpoint)
    {
        std::cout << "Create multicast endpoint failed, error info = " << strerror(errno) << std::endl;
        return -1;
    }

    const auto rate_limit = vm["transmit-rate"].as<double>();
    adk::TokenBucket* rate_control = adk::RateControl::GetInstance<adk::rate_unit::Second>(rate_limit, rate_limit < 3000000);
    if (nullptr == rate_control)
    {
        std::cout << "Create rate control failed" << std::endl;
        return -1;
    }

    struct sockaddr_in remote_addr;
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(mc_port);
    remote_addr.sin_addr.s_addr = inet_addr(mc_addr.c_str());

    const uint32_t message_size = std::max<uint32_t>(vm["message-size"].as<uint32_t>(), sizeof(uint64_t));
    char* transmit_msg = new char[message_size];

    uint64_t counter = 1;
    do
    {
        if (0 != rate_control->TryAcquire(1))
        {
            continue;
        }

        *reinterpret_cast<uint64_t*>(transmit_msg) = counter++;
        if (!mc_endpoint->SendTo(transmit_msg, message_size, remote_addr))
        {
            std::cout << "send message to remote failed" << std::endl;
        }
    } while (true);
    return 0;
}