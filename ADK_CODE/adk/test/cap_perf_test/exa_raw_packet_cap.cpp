#include "tcp_decoder.h"
#include "message_decoder_impl.h"

#include <unistd.h>
#include <adk/exa/exanic_cap.h>

#include <string>
#include <thread>
#include <boost/program_options.hpp>

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("message-ip", boost::program_options::value<std::string>(), "set message ip to select net interface")
        ("cap-dest-ip", boost::program_options::value<std::string>(), "set capture destination ip address")
        ("cap-dest-port", boost::program_options::value<uint16_t>(), "set capture destination port")
        ;

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    if (vm.count("help") || !vm.count("message-ip"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    const std::string message_ip = vm["message-ip"].as<std::string>();
    std::cout << "capture net interface <" << message_ip << ">" << std::endl;

    std::vector<adk::ExanicCap::TcpFilter> filters;
    if (vm.count("cap-dest-ip") || vm.count("cap-dest-port"))
    {
        adk::ExanicCap::TcpFilter filter;
        if (vm.count("cap-dest-ip"))
        {
            filter.dst_ip = vm["cap-dest-ip"].as<std::string>();
        }

        filter.src_port = 0;

        if (vm.count("cap-dest-port"))
        {
            filter.dst_port = vm["cap-dest-port"].as<uint16_t>();
        }
        filters.push_back(filter);
    }

    adk::ExanicCap* exanic_cap = adk::ExanicCap::Create(message_ip, filters);
    if (nullptr == exanic_cap)
    {
        std::cout << "create exanic capture <" << message_ip << "> failed" << std::endl;
        return 0;
    }

    cap::MessageDecoderImpl message_decoder_impl;
    cap::TcpDecoder         tcp_decoder(&message_decoder_impl);
    std::thread observe_handle = std::thread([&]() {
        do 
        {
            sleep(1);
            message_decoder_impl.PrintStatistics();
        } while (true);
    });

    do
    {
        exanic_cap->RecvMsg([&](const char* data, uint32_t len) {
            tcp_decoder.OnRawPacket(data, len);
        });
    } while (true);

    return 0;
}