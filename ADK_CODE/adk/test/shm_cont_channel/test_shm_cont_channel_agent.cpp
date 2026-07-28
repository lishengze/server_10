#include "test_case.h"

#include <iostream>

#include <adk/util.h>
#include <adk/shm_cont_channel.h>

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
        ;

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    if (vm.count("help"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    adk::EnableShareMemoryDump(nullptr);
    const std::string sccl_name = vm["name"].as<std::string>();
    auto* const agent = adk::sccl::Agent::Create(sccl_name);
    if (nullptr == agent)
    {
        std::cout << "Create Agent " << sccl_name << " failed" << std::endl;
        return -1;
    }

    volatile uint64_t* volatile consumer_nr = nullptr;
    volatile uint64_t* volatile consumer_failed_nr = nullptr;
    std::thread consumer = std::thread([&]() {
        uint64_t counter = 0;
        uint64_t counter_failed = 0;
        consumer_nr = &counter;
        consumer_failed_nr = &counter_failed;

        uint64_t expecteds[65536] = { 0 };
        char* buffer = new char[adk::sccl::kReserveSize];

        do
        {
            auto* const entry = agent->TryWaitEntry();
            if (nullptr == entry)
            {
                ++counter_failed;
                usleep(0);
                continue;
            }

            const auto index = entry->Index();
            const auto buffer_size = entry->BufferSize();
            shm_memory(buffer, entry->Buffer(), buffer_size);
            agent->FreeEntry(entry);

            if (buffer_size != reinterpret_cast<Protocol*>(buffer)->message_size)
            {
                std::cout << "### BUG ###" << __LINE__ << std::endl;
            }

            auto& expected = expecteds[index];
            if (expected != reinterpret_cast<Protocol*>(buffer)->message_sqn)
            {
                std::cout << "### BUG ### value = " << reinterpret_cast<Protocol*>(buffer)->message_sqn
                          << ", expected value = " << expected << std::endl;
            }

            ++(*consumer_nr);
            expected = reinterpret_cast<Protocol*>(buffer)->message_sqn + 1;
        } while (true);
    });

    while (nullptr == consumer_nr);
    while (nullptr == consumer_failed_nr);

    uint64_t consumer1 = 0;
    uint64_t consumer2 = 0;

    while (true)
    {
        sleep(1);

        const auto temp_nr1 = *consumer_nr;
        const auto temp_nr2 = *consumer_failed_nr;
        std::cout << "consumer_diff = " << temp_nr1 - consumer1
                  << ", consumer_failed_diff = " << temp_nr2 - consumer2 << std::endl;

        consumer1 = temp_nr1;
        consumer2 = temp_nr2;
    }
    return 0;
}