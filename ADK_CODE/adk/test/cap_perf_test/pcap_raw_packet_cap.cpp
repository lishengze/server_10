#include "tcp_decoder.h"
#include "message_decoder_impl.h"

#include <pcap.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <string>
#include <thread>
#include <boost/program_options.hpp>

void OnRawPacket(uint8_t* user, const struct pcap_pkthdr *pkthdr, const uint8_t *packet)
{
    ((cap::TcpDecoder*)user)->OnRawPacket(packet, (uint32_t)(pkthdr->caplen));
}

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("message-ip", boost::program_options::value<std::string>(), "set message ip to select net interface")
        ("cap-dest-ip", boost::program_options::value<std::string>()->default_value("0.0.0.0"), "set capture destination ip address")
        ("cap-dest-port", boost::program_options::value<uint16_t>()->default_value(0), "set capture destination port")
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

    pcap_if_t* pcap_devs;
    char errbuf[PCAP_ERRBUF_SIZE];
    if (pcap_findalldevs(&pcap_devs, errbuf) < 0)
    {
        std::cout << "pcap_findalldevs failed, error: " << errbuf << std::endl;
        return 0;
    }

    std::string device_name;
    pcap_if_t* current_dev = pcap_devs;
    while (nullptr != current_dev)
    {
        struct pcap_addr* current_addr = current_dev->addresses;
        while (nullptr != current_addr)
        {
            struct sockaddr_in* const dev_addr = (sockaddr_in*)(current_addr->addr);
            if (AF_INET == dev_addr->sin_family)
            {
                if (message_ip == inet_ntoa(dev_addr->sin_addr))
                {
                    device_name = current_dev->name;
                    goto traverse_end;
                }
            }

            current_addr = current_addr->next;
        }

        current_dev = current_dev->next;
    }

traverse_end:
    pcap_freealldevs(pcap_devs);

    if (ADK_UNLIKELY(device_name.empty()))
    {
        std::cout << "can not find net interface with address " << message_ip << std::endl;
        return 0;
    }

    pcap_t* const pcap_handler = pcap_create(device_name.c_str(), errbuf);
    if (ADK_UNLIKELY(nullptr == pcap_handler))
    {
        std::cout << "pcap_create failed, error: " << errbuf << std::endl;
        return 0;
    }

    std::cout << "capture net interface <" << device_name << ":" << message_ip << ">" << std::endl;

    int status = PCAP_ERROR;
    do 
    {
        status = pcap_set_snaplen(pcap_handler, 65535);
        if (status < 0)
        {
            break;
        }

        status = pcap_set_promisc(pcap_handler, 1);
        if (status < 0)
        {
            break;
        }

        status = pcap_set_timeout(pcap_handler, 1);
        if (status < 0)
        {
            break;
        }

        status = pcap_set_buffer_size(pcap_handler, 64 * 1024 * 1024);
        if (status < 0)
        {
            break;
        }

        status = pcap_activate(pcap_handler);
        if (status < 0)
        {
            break;
        }

        struct bpf_program prog;
        const std::string pcap_filter = "(vlan and tcp) or tcp";
        if (pcap_compile(pcap_handler, &prog, pcap_filter.c_str(), 1, 0) < 0)
        {
            std::cout << (boost::format("pcap_compile failed, filter <%1%>, error: %2%") 
                                        % pcap_filter
                                        % pcap_geterr(pcap_handler)).str()
                      << std::endl;
            return 0;
        }

        if (pcap_setfilter(pcap_handler, &prog) < 0)
        {
            std::cout << (boost::format("pcap_setfilter failed, error: %1%") 
                                        % pcap_geterr(pcap_handler)).str() 
                      << std::endl;
            return 0;
        }

        pcap_freecode(&prog);

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
            pcap_dispatch(pcap_handler, -1, OnRawPacket, (u_char*)&tcp_decoder);
        } while (true);
    } while (false);

    switch (status)
    {
    case PCAP_ERROR:
        std::cout << (boost::format("pcap_set_XXX <%1%> failed, error: %2%") 
                                    % device_name 
                                    % pcap_geterr(pcap_handler)).str() 
                  << std::endl;
        break;
    case PCAP_ERROR_NO_SUCH_DEVICE:
    case PCAP_ERROR_PERM_DENIED:
    case PCAP_ERROR_PROMISC_PERM_DENIED:
        std::cout << (boost::format("pcap_set_XXX <%1%> failed, pcap_status <%2%> , error: %3%") 
                                    % device_name 
                                    % pcap_statustostr(status)
                                    % pcap_geterr(pcap_handler)).str() 
                  << std::endl;
        break;
    default:
        std::cout << (boost::format("net interface <%1%> invalid status <%2%>")
                                    % device_name
                                    % pcap_statustostr(status)).str() 
                  << std::endl;
    }

    pcap_close(pcap_handler);
    return 0;
}