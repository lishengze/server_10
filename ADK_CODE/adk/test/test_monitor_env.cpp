#include <iostream>
#include <sstream>

#include <boost/property_tree/json_parser.hpp>

#include <adk/monitor/monitor.h>
#include <adk/monitor/indicator_writer.h>

uint64_t g_query_key;

class TestMonitor
{
public:
    bool OnCollection(boost::property_tree::ptree& indicator)
    {
        std::cout << "OnCollection invoked" << std::endl;
        return true;
    }


    bool OnQuery(const int32_t query_type, const boost::property_tree::ptree& query_condition, boost::property_tree::ptree& reply)
    {
        std::cout << "OnQuery invoked" << std::endl;
        return true;
    }
};

TestMonitor test_object;

class AppSinkerV2 : public adk::IMonitorSinker
{
public:
    virtual ~AppSinkerV2() {}
    AppSinkerV2() 
    {
    }

    void Receive(IMonitorSinker::Type type, uint64_t query_key, const boost::property_tree::ptree& sink_ptree)
    {
        oss_.str("");
        boost::property_tree::json_parser::write_json(oss_, sink_ptree, true);

        std::cout << "type = " << type << ", query_key = " << query_key << ", sink_ptree : " << std::endl;
        std::cout << oss_.str() << std::endl;
        std::cout << boost::posix_time::ptime(boost::posix_time::microsec_clock::local_time()) << std::endl << std::endl;
    }

private:
    std::ostringstream oss_;
};

int main(int argc, char const *argv[])
{
    adk::MonitorOps monitor_ops;
    monitor_ops.on_collection_indicator = boost::bind(&TestMonitor::OnCollection, &test_object, _1);
    monitor_ops.on_query = boost::bind(&TestMonitor::OnQuery, &test_object, _1, _2, _3);
    monitor_ops.is_collection_indicator = true;

    REGISTER_OBJECT(TestMonitor, "test_object", &monitor_ops);

    AppSinkerV2 sinker_v2;
    adk::Monitor::PluginSinker(&sinker_v2);  

    sleep(100);

    adk::Monitor::PlugoutSinker(&sinker_v2);
    std::cout << "plugout sinker v2" << std::endl;

    return 0;
}
