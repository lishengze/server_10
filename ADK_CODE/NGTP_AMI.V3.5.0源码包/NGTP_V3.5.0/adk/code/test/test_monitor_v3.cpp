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

    static bool QueryRequestHandler(void* user, uint64_t& query_key, std::string& url, boost::property_tree::ptree& query_condition, int32_t& query_type);
    static bool ConfigHandler(void* user, uint64_t& query_key, std::string& url, boost::property_tree::ptree& query_condition, int32_t& query_type);
    static bool ConfigHandler2(void* user, uint64_t& query_key, std::string& url, boost::property_tree::ptree& query_condition, int32_t& query_type);
    static bool ConfigHandler3(void* user, uint64_t& query_key, std::string& url, boost::property_tree::ptree& query_condition, int32_t& query_type);
    bool ConfigHandler4(uint64_t& query_key, std::string& url, boost::property_tree::ptree& query_condition, int32_t& query_type)
    {
        assert(query_type == 0);
        assert(query_condition.empty());
        std::cout << "ConfigHandler4" << std::endl;
        url = "cfg:TestMonitor@test_object/is_collection_indicator=0";
        return false;
    }
};

bool TestMonitor::QueryRequestHandler(void* user, uint64_t& query_key, std::string& url, boost::property_tree::ptree& query_condition, int32_t& query_type)
{
    assert(user == NULL);
    assert(query_type == 0);
    assert(query_condition.empty());
    url = "heir:TestMonitor@test_object";
    ++g_query_key;
    query_key = g_query_key;
    return true;
}

bool TestMonitor::ConfigHandler(void* user, uint64_t& query_key, std::string& url, boost::property_tree::ptree& query_condition, int32_t& query_type)
{
    assert(user == NULL);
    assert(query_type == 0);
    assert(query_condition.empty());
    url = "cfg:TestMonitor@test_object/is_collection_indicator=0";
    ++g_query_key;
    query_key = g_query_key;
    return true;
}

bool TestMonitor::ConfigHandler2(void* user, uint64_t& query_key, std::string& url, boost::property_tree::ptree& query_condition, int32_t& query_type)
{
    assert(user == NULL);
    assert(query_type == 0);
    assert(query_condition.empty());
    url = "cfg:TestMonitor@test_object/is_collection_indicator=1";
    ++g_query_key;
    query_key = g_query_key;
    return true;
}

bool TestMonitor::ConfigHandler3(void* user, uint64_t& query_key, std::string& url, boost::property_tree::ptree& query_condition, int32_t& query_type)
{
    assert(user == (void*)0x1234);
    assert(query_type == 0);
    assert(query_condition.empty());
    url = "cfg:TestMonitor@test_object/collection_interval_milli=1000";
    ++g_query_key;
    query_key = g_query_key;
    return true;
}

TestMonitor test_object;

class AppSinker : public adk::IMonitorSinker
{
public:
    virtual ~AppSinker() {}
    AppSinker() {}

    void Receive(IMonitorSinker::Type type, uint64_t query_key, const boost::property_tree::ptree& sink_ptree)
    {
        std::cout << "type = " << type << std::endl;
        oss_.clear();
        oss_.str("");

        boost::property_tree::json_parser::write_json(oss_, sink_ptree, false);
        std::cout << oss_.str();
    }
private:
    std::ostringstream oss_;
};

bool EventHandler(void* user, boost::property_tree::ptree& ptree)
{
    assert(user == NULL);
    ptree.put("EventLevel", 0);
    ptree.put("EventType", 1);
    ptree.put("EventDesc", "member got out of sync");
    return true;
}

int main(int argc, char const *argv[])
{
    adk::Monitor::Start();

    adk::MonitorOps monitor_ops;
    monitor_ops.on_collection_indicator = boost::bind(&TestMonitor::OnCollection, &test_object, _1);
    monitor_ops.on_query = boost::bind(&TestMonitor::OnQuery, &test_object, _1, _2, _3);
    monitor_ops.is_collection_indicator = false; //true;

    adk::EventChannel*  ev_channel = REGISTER_OBJECT(TestMonitor, "test_object", &monitor_ops);

    // test monitor operation functions are not registered
    adk::MonitorOps monitor_ops2;
    monitor_ops2.is_collection_indicator = true;
    REGISTER_OBJECT(TestMonitor, "test_object2", &monitor_ops2);

    AppSinker sinker;
    adk::Monitor::PluginSinker(&sinker);

    //sleep(10);
    sleep(2);

    adk::Monitor::Start();

    ev_channel->PushEvent(&EventHandler, NULL);
    ev_channel->PushEvent(boost::bind(EventHandler, (void*)0x00, _1));

    boost::property_tree::ptree ptree;
    ptree.put("EventPtree", "test");
    ev_channel->PushEvent(ptree);

    adk::Monitor::SubmitRequest(1, std::string("heir:TestMonitor@test_object"));

    adk::Monitor::SubmitRequest(2, std::string("flag:test_object"));
    
    adk::Monitor::SubmitRequest(3, std::string("flat:test_object"));

    adk::Monitor::SubmitRequest(4, std::string("flat:test_object2"));

    adk::Monitor::Stop();

    sleep(1);

    adk::Monitor::SubmitRequest(5, std::string("flat:test_object"));
    adk::Monitor::SubmitRequest(6, std::string("flat:test_object"));

    sleep(1);

    adk::Monitor::Stop();

    adk::Monitor::SubmitRequest(7, std::string("flat:test_object"));

    adk::Monitor::Stop();
    adk::Monitor::Stop();
    adk::Monitor::Stop();
    sleep(4);

    return 0;
}
