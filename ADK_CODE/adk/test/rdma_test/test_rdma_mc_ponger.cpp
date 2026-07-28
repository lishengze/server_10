#include "rdma_test_case.h"

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("host-ip", boost::program_options::value<std::string>(), "set host ip address")
        ("pong-multicast", boost::program_options::value<std::string>()->default_value("239.0.219.219"), "set pong multicast address")
        ("ping-multicast", boost::program_options::value<std::string>()->default_value("239.0.218.218"), "set ping multicast address")
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
    RdmaContext* rdma_context = RdmaContext::NewContext(host_ip);
    if (nullptr == rdma_context)
    {
        std::cout << "RdmaContext::NewContext failed" << std::endl;
        return -1;
    }

    RdmaMcEndpoint* rdma_endpoint = rdma_context->CreateMcEndpoint();
    if (nullptr == rdma_endpoint)
    {
        std::cout << "create udp endpoint failed" << std::endl;
        return -1;
    }

    const std::string pong_multicast = vm["pong-multicast"].as<std::string>();
    RdmaDH* pong_dh = rdma_endpoint->JoinMcGroup(pong_multicast);
    if (nullptr == pong_dh)
    {
        std::cout << "JoinMcGroup pong-multicast<" << pong_multicast << "> failed" << std::endl;
        return -1;
    }

    std::cout << "JoinMcGroup pong-multicast<" << pong_multicast << "> success" << std::endl;

    while (RdmaDH::Status::kIniting == pong_dh->status)
    {
        usleep(1000);
    }

    if (!pong_dh->is_ready())
    {
        std::cout << "status of <pong-multicast destination handler> changed to invalid" << std::endl;
        return -1;
    }

    std::cout << "pong-multicast destination handler is ready" << std::endl;

    if (adk::ErrorCode::kSuccess != rdma_endpoint->LeaveMcGroup(pong_multicast))
    {
        std::cout << "LeaveMcGroup pong-multicast<" << pong_multicast << "> failed" << std::endl;
        return -1;
    }

    std::cout << "LeaveMcGroup pong-multicast<" << pong_multicast << "> success" << std::endl;

    if (adk::ErrorCode::kSuccess == rdma_endpoint->LeaveMcGroup(pong_multicast))
    {
        std::cout << "double LeaveMcGroup pong-multicast<" << pong_multicast << "> expected success" << std::endl;
        return -1;
    }

    const std::string ping_multicast = vm["ping-multicast"].as<std::string>();
    RdmaDH* ping_dh = rdma_endpoint->JoinMcGroup(ping_multicast);
    if (nullptr == ping_dh)
    {
        std::cout << "JoinMcGroup ping-multicast<" << ping_multicast << "> failed" << std::endl;
        return -1;
    }

    std::cout << "JoinMcGroup ping-multicast<" << ping_multicast << "> success" << std::endl;

    while (RdmaDH::Status::kIniting == ping_dh->status)
    {
        usleep(1000);
    }

    if (!ping_dh->is_ready())
    {
        std::cout << "status of <ping-multicast destination handler> changed to invalid" << std::endl;
        return -1;
    }

    std::cout << "ping-multicast destination handler is ready" << std::endl;

    uint64_t recv_count = 0;
    do
    {
        recv_count += rdma_endpoint->RecvMMsg([&](char* msg_buf, uint32_t msg_size) {

        retry:
            struct TxNodeEntry* node_entry = rdma_endpoint->NewTxMessage();
            if (ADK_UNLIKELY(nullptr == node_entry))
            {
                rdma_endpoint->RecycleTxEntries();
                goto retry;
            }

            node_entry->set_buffer_size(msg_size);
            memcpy(node_entry->buffer(), msg_buf, msg_size);
            rdma_endpoint->SendMsg(node_entry, pong_dh);
        });

        if (recv_count > 1000)
        {
            rdma_endpoint->RecycleTxEntries();
            recv_count = 0;
        }

    } while (true);
    return 0;
}