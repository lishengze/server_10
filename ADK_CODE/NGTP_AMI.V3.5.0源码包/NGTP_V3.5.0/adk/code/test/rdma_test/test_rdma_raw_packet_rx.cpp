#include <adk/rdma/rdma_raw_packet.h>

#include <string>
#include <boost/program_options.hpp>

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("host-ip", boost::program_options::value<std::string>(), "set host ip address")
        ("spec-to-host", boost::program_options::value<bool>()->default_value(true), "specified host network interface")
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

    adk::rdma::RawPktEndpoint::RawPktFlow flow = vm["spec-to-host"].as<bool>() 
        ? adk::rdma::RawPktEndpoint::RawPktFlow::kTxOnly
        : adk::rdma::RawPktEndpoint::RawPktFlow::kSniffer;

    auto* const endpoint = adk::rdma::RawPktEndpoint::Create(vm["host-ip"].as<std::string>(), 0, flow);
    if (nullptr == endpoint)
    {
        std::cout << "create raw packet endpoint failed, error <" << endpoint->last_error() << ">" << std::endl;
        return 0;
    }

    std::cout << "HW information = " << endpoint->GetHWInfo() << std::endl;

    do 
    {
        endpoint->RecvMsg([](const char* buffer, uint32_t buffer_len) {
            printf("0-1--2-3--4-5--6-7--8-9--A-B--C-D--E-F-\n");
            for (uint32_t index = 0; index < buffer_len; ++index)
            {
                printf("%02x", (uint32_t)(*((uint8_t*)buffer + index)));
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
        });
    } while (true);

    return 0;
}