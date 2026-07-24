#define BOOST_TEST_MODULE monitor
#include <boost/test/included/unit_test.hpp>

#include <iostream>
#include <sstream>
#include <stdexcept>

#include <boost/property_tree/json_parser.hpp>

#include <adk/monitor/monitor.h>
#include <adk/monitor/indicator_writer.h>

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
    }

private:
    std::ostringstream oss_;
};

class TestMonitor
{
public:
    bool OnCollection(boost::property_tree::ptree& indicator)
    {
        std::cout << "OnCollection invoked" << std::endl;
        indicator.put("test", 1);
        if (is_throw_exception)
            throw std::runtime_error("monitor test");
        return true;
    }


    bool OnQuery(const int32_t query_type, const boost::property_tree::ptree& query_condition, boost::property_tree::ptree& reply)
    {
        std::cout << "OnQuery invoked" << std::endl;
        return true;
    }

    TestMonitor()
    {
        is_throw_exception = false;
    }

    bool is_throw_exception;
};

BOOST_AUTO_TEST_CASE(sinker_plugout_after_normal_quit)
{
    AppSinkerV2 sinker;
    adk::Monitor::Start();
    auto ret = adk::Monitor::PluginSinker(&sinker);
    BOOST_CHECK_EQUAL(ret, adk::ErrorCode::kSuccess);

    TestMonitor test_object;
    adk::MonitorOps monitor_ops;
    monitor_ops.on_collection_indicator = boost::bind(&TestMonitor::OnCollection, &test_object, _1);
    monitor_ops.on_query = boost::bind(&TestMonitor::OnQuery, &test_object, _1, _2, _3);
    monitor_ops.is_collection_indicator = true;
    REGISTER_OBJECT(TestMonitor, "test_object", &monitor_ops);

    sleep(6);

    adk::Monitor::Stop();

    ret = adk::Monitor::PlugoutSinker(&sinker);
    BOOST_CHECK_EQUAL(ret, adk::ErrorCode::kSuccess);
    ret = adk::Monitor::PlugoutSinker(&sinker);
    BOOST_CHECK_EQUAL(ret, adk::ErrorCode::kSuccess);
}
