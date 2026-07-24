#include "test_case.h"

#include <iostream>

#include <adk/util.h>
#include <adk_pack/shm_cont_channel.h>

int main(int argc, char** argv)
{
    boost::program_options::options_description desc("Allowed options", 120);
    desc.add_options()
        ("help,h", "show this information")
        (
            "name",
            boost::program_options::value<std::string>()->default_value("shm_test_x"),
            "set host ip address"
        )
        (
            "message-size",
            boost::program_options::value<uint32_t>()->default_value(128),
            "set message size"
        )
        (
            "message-count",
            boost::program_options::value<uint64_t>()->default_value(std::numeric_limits<uint64_t>::max()),
            "set message count"
        )
        ;

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    if (vm.count("help"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    adk_impl::EnableShareMemoryDump(nullptr);
    const std::string sccl_name = vm["name"].as<std::string>();
    auto* const proxy = adk::sccl::Proxy::Create(sccl_name);
    if (nullptr == proxy)
    {
        std::cout << "connect to Agent " << sccl_name << " failed" << std::endl;
        return -1;
    }

    const auto message_size = vm["message-size"].as<uint32_t>();

    volatile uint64_t* volatile producer_nr = nullptr;
    volatile uint64_t* volatile producer_failed_nr = nullptr;
    std::thread producer = std::thread([&]() {
        const auto message_count = vm["message-count"].as<uint64_t>();

        auto* const message = reinterpret_cast<Protocol*>(new char[message_size]);
        message->message_size = message_size;
        message->message_sqn = 0;

        producer_nr = &(message->message_sqn);

        uint64_t counter = 0;
        producer_failed_nr = &counter;

        do 
        {
            auto* const buffer = proxy->AllocBuffer(message_size);
            if (nullptr != buffer)
            {
                shm_memory(buffer, message, message_size);
                proxy->PostBuffer(buffer, message_size);

                ++(*producer_nr);
            }
            else
            {
                ++counter;
                ADK_PAUSE();
            }
        } while (*producer_nr < message_count);

        delete[] reinterpret_cast<char*>(message);
    });

    while (nullptr == producer_nr);
    while (nullptr == producer_failed_nr);

    uint64_t producer1 = 0;
    uint64_t producer2 = 0;
    while (true)
    {
        sleep(1);

        const auto temp_nr3 = *producer_nr;
        const auto temp_nr4 = *producer_failed_nr;
        std::cout << "producer_diff = " << temp_nr3 - producer1 
                  << ", producer_failed_diff = " << temp_nr4 - producer2 << std::endl;

        producer1 = temp_nr3;
        producer2 = temp_nr4;
    }
    return 0;
}
