#include "tcp_decoder.h"
#include "message_decoder_impl.h"

#include <unistd.h>
#include <adk/rdma/rdma_raw_packet.h>

#include <string>
#include <thread>
#include <boost/program_options.hpp>

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("message-ip", boost::program_options::value<std::string>(), "set message ip to select net interface")
        ("cap-dest-ip", boost::program_options::value<std::string>()->default_value("0.0.0.0"), "set capture destination ip address")
        ("cap-dest-port", boost::program_options::value<uint16_t>()->default_value(0), "set capture destination port")
        ("cap-to-this", boost::program_options::value<bool>()->default_value(false), "capture to specified net interface")
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

    auto flow = vm["cap-to-this"].as<bool>() ? adk::rdma::RawPktEndpoint::kDstToThis
                                             : adk::rdma::RawPktEndpoint::kSniffer;

    auto raw_pkt_endpoint = adk::rdma::RawPktEndpoint::Create(message_ip, 0, flow);
    if (ADK_UNLIKELY(nullptr == raw_pkt_endpoint))
    {
        std::cout << "create rdma raw endpoint failed" << std::endl;
        return 0;
    }

    cap::MessageDecoderImpl message_decoder_impl;
    cap::TcpDecoder         tcp_decoder(&message_decoder_impl);
    tcp_decoder.set_dest_filter(vm["cap-dest-ip"].as<std::string>(), vm["cap-dest-port"].as<uint16_t>());

    std::thread observe_handle = std::thread([&]() {
        do 
        {
            sleep(1);
            message_decoder_impl.PrintStatistics();
        } while (true);
    });

    do
    {
        raw_pkt_endpoint->RecvMsg([&](const char* data, uint32_t len) {
            tcp_decoder.OnRawPacket(data, len);
        });
    } while (true);

    return 0;
}