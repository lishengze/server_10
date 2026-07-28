#include "recorder_data_reader.h"
#include <iostream>

#include <boost/program_options.hpp>

int main(int argc, char* argv[])
{
    namespace po = boost::program_options;
    po::options_description option_desc;
    po::variables_map option_vm;

    option_desc.add_options()
    ("help,h", "show this information")
    ("context,c", po::value<std::string>(), "the context name")
    ("data_path,d", po::value<std::string>()->default_value("./recorder_data"), "the recorder root data path")
    ("type,t", po::value<std::string>()->default_value("rx"), "the endpoint type: rx or tx")
    ("transport_name,T", po::value<std::string>()->default_value(std::string()), "the transport name")
    ("begin_sqn", po::value<uint64_t>()->default_value(1), "the begin message sqn")
    ("end_sqn", po::value<uint64_t>()->default_value(0), "the end message sqn");

    try
    {
        po::store(po::parse_command_line(argc, argv, option_desc), option_vm);
        po::notify(option_vm);

        if (option_vm.count("help"))
        {
            std::cerr << option_desc << std::endl;
            return 0;
        }

        auto context_name = option_vm["context"].as<std::string>();
        auto data_path = option_vm["data_path"].as<std::string>();
        auto type = option_vm["type"].as<std::string>();
        auto begin_sqn = option_vm["begin_sqn"].as<uint64_t>();
        auto end_sqn = option_vm["end_sqn"].as<uint64_t>();
        auto transport_name = option_vm["transport_name"].as<std::string>();

        recorder_data::RecorderDataReader recorder_data_reader(data_path);

        if (type == "rx")
        {
            if (!recorder_data_reader.ReadRxMessage(context_name, begin_sqn, end_sqn))
            {
                std::cerr << "read rx message failed, context_name: " << context_name << " begin sqn: " <<  begin_sqn << " end sqn: " << end_sqn << std::endl;
                return -1;
            }
        }
        else if (type == "tx")
        {
            if (!recorder_data_reader.ReadTxMessage(context_name, transport_name, begin_sqn, end_sqn))
            {
                std::cerr << "read tx message failed, context_name: " << context_name << " begin sqn: " <<  begin_sqn << " end sqn: " << end_sqn << std::endl;
                return -1;
            }
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << "catch exception: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
