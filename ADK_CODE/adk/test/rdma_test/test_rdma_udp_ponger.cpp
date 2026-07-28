#include "rdma_test_case.h"

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("host-ip", boost::program_options::value<std::string>(), "set host ip address")
        ("host-port", boost::program_options::value<uint16_t>()->default_value(50010), "set host port")
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
        std::cout << "host ip is not set" << std::endl;
        return -1;
    }

    const std::string host_ip = vm["host-ip"].as<std::string>();
    const uint16_t    host_port = vm["host-port"].as<uint16_t>();
    RdmaContext* rdma_context = RdmaContext::NewContext(host_ip);
    if (nullptr == rdma_context)
    {
        std::cout << "RdmaContext::NewContext failed" << std::endl;
        return -1;
    }

    RdmaUcEndpoint* udp_endpoint = rdma_context->CreateUcEndpoint(host_port);
    if (nullptr == udp_endpoint)
    {
        std::cout << "create udp endpoint failed" << std::endl;
        return -1;
    }

    uint64_t recv_count = 0;
    do 
    {
        recv_count += udp_endpoint->RecvMMsgDh([&](char* msg_buf, uint32_t msg_size, RdmaDH* dh) {
        retry:
            struct TxNodeEntry* node_entry = udp_endpoint->NewTxMessage();
            if (ADK_UNLIKELY(nullptr == node_entry))
            {
                udp_endpoint->RecycleTxEntries();
                goto retry;
            }

            node_entry->set_buffer_size(msg_size);
            memcpy(node_entry->buffer(), msg_buf, msg_size);
            udp_endpoint->SendMsg(node_entry, dh);
        });

        if (recv_count > 10)
        {
            udp_endpoint->RecycleTxEntries();
            recv_count = 0;
        }

    } while (true);
    return 0;
}