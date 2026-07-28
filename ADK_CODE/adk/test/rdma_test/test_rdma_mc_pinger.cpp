#include "rdma_test_case.h"

#include <adk/token_buckets.h>

int main(int argc, char* argv[])
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        ("host-ip", boost::program_options::value<std::string>(), "set host ip address")
        ("ping-multicast", boost::program_options::value<std::string>()->default_value("239.0.218.218"), "set ping multicast address")
        ("pong-multicast", boost::program_options::value<std::string>()->default_value("239.0.219.219"), "set pong multicast address")
        ("message-size", boost::program_options::value<uint32_t>()->default_value(256), "set message size")
        ("transmit-rate", boost::program_options::value<uint32_t>()->default_value(300000), "set transmit rate")
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
        std::cout << "Create multicast endpoint failed" << std::endl;
        return -1;
    }

    const std::string ping_multicast = vm["ping-multicast"].as<std::string>();
    RdmaDH* const ping_dh = rdma_endpoint->JoinMcGroup(ping_multicast);
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

    if (adk::ErrorCode::kSuccess != rdma_endpoint->LeaveMcGroup(ping_multicast))
    {
        std::cout << "LeaveMcGroup ping-multicast<" << ping_multicast << "> failed" << std::endl;
        return -1;
    }

    std::cout << "LeaveMcGroup ping-multicast<" << ping_multicast << "> success" << std::endl;

    if (adk::ErrorCode::kSuccess == rdma_endpoint->LeaveMcGroup(ping_multicast))
    {
        std::cout << "double LeaveMcGroup ping-multicast<" << ping_multicast << "> expected success" << std::endl;
        return -1;
    }

    const std::string pong_multicast = vm["pong-multicast"].as<std::string>();
    RdmaDH* const pong_dh = rdma_endpoint->JoinMcGroup(pong_multicast);
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

    bool reset = false;
    uint64_t send_nr = 0;
    uint64_t recv_nr = 0;
    uint64_t latency_min = -1;
    uint64_t latency_max = 0;
    uint64_t latency_total = 0;

    std::thread observe_thd = std::thread([&]() {
        uint64_t local_send_nr = 0;

        do
        {
            sleep(1);

            const auto temp_send_nr = send_nr;
            if (0 != recv_nr)
            {
                std::cout << "send_nr = " << temp_send_nr - local_send_nr << " | "
                    << "recv_nr = " << recv_nr << " | "
                    << "latency_min = " << latency_min << " | "
                    << "latency_max = " << latency_max << " | "
                    << "latency_avg = " << latency_total / recv_nr << std::endl;
            }
            else
            {
                std::cout << "send_nr = " << temp_send_nr - local_send_nr << " | "
                    << "recv_nr = NA" << " | "
                    << "latency_min = NA" << " | "
                    << "latency_max = NA" << " | "
                    << "latency_avg = NA" << std::endl;
            }

            local_send_nr = temp_send_nr;
            reset = true;
        } while (true);
    });

    const auto message_size = std::max<uint32_t>(vm["message-size"].as<uint32_t>(), sizeof(struct timespec));

    std::thread recv_thd = std::thread([&]() {
        struct timespec time_point;

        do
        {
            if (reset)
            {
                recv_nr = 0;
                latency_min = -1;
                latency_max = 0;
                latency_total = 0;
                reset = false;
            }

            rdma_endpoint->RecvMMsg([&](char* msg_buf, uint32_t msg_size) {
                assert(message_size == msg_size);
                assert(msg_size >= sizeof(struct timespec));

                clock_gettime(CLOCK_REALTIME, &time_point);

                const auto time_diff = time_point.tv_sec * 1000000000 + time_point.tv_nsec
                    - ((struct timespec*)msg_buf)->tv_sec * 1000000000 - ((struct timespec*)msg_buf)->tv_nsec;

                latency_min = std::min<uint64_t>(latency_min, time_diff);
                latency_max = std::max<uint64_t>(latency_max, time_diff);
                latency_total += time_diff;
                ++recv_nr;
            });
        } while (true);
    });

    const auto transmit_rate = vm["transmit-rate"].as<uint32_t>();
    adk::TokenBucket* token_bucket = adk::RateControl::GetInstance<adk::rate_unit::Second>((double)transmit_rate, true);
    assert(token_bucket);

    std::cout << "ping dest handler: status = " << ping_dh->status
              << ", remote_key = " << ping_dh->remote_qkey
              << ", remote_qpn = " << ping_dh->remote_qpn << std::endl;

    do
    {
        if (adk::ErrorCode::kSuccess != token_bucket->TryAcquire(1))
        {
            rdma_endpoint->RecycleTxEntries();
            continue;
        }

        struct TxNodeEntry* node_entry = rdma_endpoint->NewTxMessage();
        if (nullptr == node_entry)
        {
            rdma_endpoint->RecycleTxEntries();
            continue;
        }

        assert(node_entry->buffer_size() >= message_size);
        clock_gettime(CLOCK_REALTIME, (struct timespec*)(node_entry->buffer()));
        node_entry->set_buffer_size(message_size);
        rdma_endpoint->SendMsg(node_entry, ping_dh);
        ++send_nr;
    } while (true);

    return 0;
}