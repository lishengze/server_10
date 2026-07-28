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
		indicator.put("test", 1);
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
};

bool TestMonitor::QueryRequestHandler(void* user, uint64_t& query_key, std::string& url, boost::property_tree::ptree& query_condition, int32_t& query_type)
{
	assert(user == NULL);
	assert(query_type == 0);
	assert(query_condition.empty());
	++g_query_key;
	query_key = g_query_key;
	url = "heir:TestMonitor@test_object";
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
	AppSinker() 
	{
		indicator_writer_.Init("./", "test_monitor_v2");
	}

	void Receive(IMonitorSinker::Type type, uint64_t query_key, const boost::property_tree::ptree& sink_ptree)
	{
		indicator_writer_.Write(boost::lexical_cast<std::string>(query_key), IMonitorSinker::GetTypeDesc(type), sink_ptree);
	}

private:
	std::ostringstream oss_;
	adk::IndicatorWriter indicator_writer_;
};

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
	adk::MonitorOps monitor_ops;
	monitor_ops.on_collection_indicator = boost::bind(&TestMonitor::OnCollection, &test_object, _1);
	monitor_ops.on_query = boost::bind(&TestMonitor::OnQuery, &test_object, _1, _2, _3);
	monitor_ops.is_collection_indicator = true;

	REGISTER_OBJECT(TestMonitor, "test_object", &monitor_ops);

	adk::EventChannel*  ev_channel = REGISTER_OBJECT(TestMonitor, "test_object", &monitor_ops);

	AppSinker sinker;
	AppSinkerV2 sinker_v2;
	adk::Monitor::PluginSinker(&sinker);
	adk::Monitor::PluginSinker(&sinker_v2);

	ev_channel->PushEvent(boost::bind(EventHandler, (void*)0x00, _1));
	sleep(10);

	adk::Monitor::SubmitRequest(TestMonitor::QueryRequestHandler, NULL);
	adk::Monitor::SubmitRequest(TestMonitor::ConfigHandler, NULL);
	sleep(5);
	ev_channel->PushEvent(boost::bind(EventHandler, (void*)0x00, _1));
	adk::Monitor::SubmitRequest(TestMonitor::ConfigHandler3, (void*)0x1234);
	adk::Monitor::SubmitRequest(TestMonitor::ConfigHandler2, NULL);

	sleep(5);
	adk::Monitor::PlugoutSinker(&sinker_v2);
	std::cout << "plugout sinker v2" << std::endl;
	sleep(5);
	adk::Monitor::PlugoutSinker(&sinker);
	std::cout << "plugout sinker" << std::endl;

	sleep(10000);

	return 0;
}
