#include <adk/rdma/rdma_raw_packet.h>

#include <string>
#include <boost/program_options.hpp>

#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <linux/types.h>

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("host-ip", boost::program_options::value<std::string>(), "set host ip address")
        ("host-port", boost::program_options::value<uint16_t>()->default_value(50000), "set host port")
        ("remote-ip", boost::program_options::value<std::string>(), "set dest ip address")
        ("remote-port", boost::program_options::value<uint16_t>()->default_value(50001), "set dest port")
        ;

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    if (vm.count("help"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    if (!vm.count("host-ip"))
    {
        std::cout << "please specify host ip" << std::endl;
        std::cout << desc << std::endl;
        return 0;
    }

    if (!vm.count("remote-ip"))
    {
        std::cout << "please specify remote ip" << std::endl;
        std::cout << desc << std::endl;
        return 0;
    }

    const std::string host_ip = vm["host-ip"].as<std::string>();
    const uint16_t host_port = vm["host-port"].as<uint16_t>();
    const std::string remote_ip = vm["remote-ip"].as<std::string>();
    const uint16_t remote_port = vm["remote-port"].as<uint16_t>();

    const std::string transmit_str = "Hello world!";
    const uint32_t transmit_len = transmit_str.size() + 1;

    auto sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sa_local;
    sa_local.sin_family = AF_INET;
    sa_local.sin_addr.s_addr = inet_addr(host_ip.c_str());
    sa_local.sin_port = htons(host_port);

    bind(sock_fd, (struct sockaddr*)&sa_local, sizeof(struct sockaddr_in));

    struct sockaddr_in sa_remote;
    sa_remote.sin_family = AF_INET;
    sa_remote.sin_addr.s_addr = inet_addr(remote_ip.c_str());
    sa_remote.sin_port = htons(remote_port);

    /*
    do
    {
        sendto(sock_fd, transmit_str.c_str(), transmit_len, 0, (struct sockaddr*)&sa_remote, sizeof(struct sockaddr_in));
        sleep(1);
    } while (true);
    */

    auto* const endpoint = adk::rdma::RawPktEndpoint::Create(host_ip, host_port, adk::rdma::RawPktEndpoint::kTxOnly);
    if (nullptr == endpoint)
    {
        std::cout << "create raw packet endpoint failed, error <" << endpoint->last_error() << ">" << std::endl;
        return 0;
    }

    auto* const dest_handler = endpoint->CreateDestHandler(remote_ip, remote_port);
    if (nullptr == dest_handler)
    {
        std::cout << "create dest handler failed <" << remote_ip 
                  << ":" << remote_port << ">" << std::endl;
        return 0;
    }

    do 
    {
        auto* const tx_entry = endpoint->NewTxMessage();
        tx_entry->set_app_buffer_size(transmit_len);
        strcpy(tx_entry->app_buffer(), transmit_str.c_str());

        if (ADK_UNLIKELY(adk::ErrorCode::kSuccess != endpoint->SendMsg(tx_entry, dest_handler)))
        {
            std::cout << "Send message failed" << std::endl;
        }
        else
        {
            printf("0-1--2-3--4-5--6-7--8-9--A-B--C-D--E-F-\n");
            for (uint32_t index = 0; index < tx_entry->buffer_size(); ++index)
            {
                printf("%02x", (uint32_t)(*((const uint8_t*)(tx_entry->const_buffer()) + index)));
                if (!((index + 1) & 1))
                {
                    printf(" ");
                }

                if (0 == (index + 1) % 16)
                {
                    printf("\n");
                }
            }
            printf("\n");
        }

        endpoint->RecycleTxEntries();

        sleep(1);
    } while (true);

    return 0;
}