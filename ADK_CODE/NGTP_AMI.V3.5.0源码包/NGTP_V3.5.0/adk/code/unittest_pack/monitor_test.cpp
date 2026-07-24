#define BOOST_TEST_MODULE monitor
#include <boost/test/included/unit_test.hpp>

#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>

#include <boost/property_tree/json_parser.hpp>

#include <adk_pack/error_code.h>
#include <adk_pack/monitor/indicator_writer.h>
#include <adk_pack/monitor/monitor.h>
#include <adk_pack/property.h>

class AppSinkerV2 : public adk::IMonitorSinker
{
public:
    virtual ~AppSinkerV2() {}
    AppSinkerV2()
    {
    }

    void Receive(IMonitorSinker::Type type, uint64_t query_key, const boost::property_tree::ptree& sink_ptree)
    {
        std::lock_guard<std::mutex> lck(ind_mut_);
        oss_.str("");
        boost::property_tree::json_parser::write_json(oss_, sink_ptree, true);

        // std::cout << "type = " << type << ", query_key = " << query_key << ", sink_ptree : " << std::endl;
        // std::cout << oss_.str() << std::endl;
    }

    adk::Property GetProperty()
    {
        // 防止恰好清空oss_之后调用该接口
        std::lock_guard<std::mutex> lck(ind_mut_);
        return adk::Property(oss_.str());
    }

private:
    std::ostringstream oss_;
    std::mutex ind_mut_;
};

class TestMonitor
{
public:
    bool OnCollection(boost::property_tree::ptree& indicator)
    {
        // std::cout << "OnCollection invoked" << std::endl;
        indicator.put("test", 1);
        // 通过参数控制抛异常
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

BOOST_AUTO_TEST_CASE(sinker_plugin_without_start_monitor)
{
    AppSinkerV2 sinker;
    auto ret = adk::Monitor::PluginSinker(&sinker);
    // 未启动 Monitor 实例，期望返回非法调用
    BOOST_CHECK_EQUAL(ret, adk::ErrorCode::kInvalidInvoke);
}

BOOST_AUTO_TEST_CASE(sinker_plugin_plugout)
{
    AppSinkerV2 sinker;
    // 未启动时调用 期望会返回失败
    auto ret = adk::Monitor::PlugoutSinker(&sinker);
    BOOST_CHECK_EQUAL(ret, adk::ErrorCode::kFailure);

    adk::Monitor::Start();
    ret = adk::Monitor::PluginSinker(&sinker);
    BOOST_CHECK_EQUAL(ret, adk::ErrorCode::kSuccess);

    TestMonitor test_object;
    adk::MonitorOps monitor_ops;
    monitor_ops.on_collection_indicator = boost::bind(&TestMonitor::OnCollection, &test_object, _1);
    monitor_ops.on_query = boost::bind(&TestMonitor::OnQuery, &test_object, _1, _2, _3);
    monitor_ops.is_collection_indicator = true;
    // 加快测试过程， 将指标收集时间缩短为 100ms
    monitor_ops.collection_interval_milli = 100;
    REGISTER_OBJECT(TestMonitor, "test_object", &monitor_ops);

    sleep(1);

    ret = adk::Monitor::PlugoutSinker(&sinker);
    BOOST_CHECK_EQUAL(ret, adk::ErrorCode::kSuccess);

    // 重复 Plugout 返回失败
    ret = adk::Monitor::PlugoutSinker(&sinker);
    BOOST_CHECK_EQUAL(ret, adk::ErrorCode::kFailure);

    adk::Monitor::Stop();
}

/**
 * @brief 测试 Monitor 调用产生异常后能否正常运行
 * 
 */
BOOST_AUTO_TEST_CASE(sinker_plugin_after_monitor_exception_quit)
{
    adk::Monitor::Start();
    AppSinkerV2 sinker;
    adk::Property ind_prop;

    TestMonitor test_object;
    adk::MonitorOps monitor_ops;
    // 注册指标采集句柄和主动查询句柄
    monitor_ops.on_collection_indicator = boost::bind(&TestMonitor::OnCollection, &test_object, _1);
    monitor_ops.on_query = boost::bind(&TestMonitor::OnQuery, &test_object, _1, _2, _3);
    monitor_ops.is_collection_indicator = true;
    monitor_ops.collection_interval_milli = 100;
    REGISTER_OBJECT(TestMonitor, "test_object", &monitor_ops);

    sleep(1);
    // 正常调用
    auto ret = adk::Monitor::PluginSinker(&sinker);
    BOOST_CHECK_EQUAL(ret, adk::ErrorCode::kSuccess);
    ind_prop = sinker.GetProperty();
    std::cout << "ind: " << ind_prop.Dump(true) << std::endl;
    std::string status = ind_prop.GetValue("class_objects..object_collection_status", "");
    BOOST_REQUIRE(status == "");

    test_object.is_throw_exception = true;

    sleep(1);
    // 抛异常
    ret = adk::Monitor::PluginSinker(&sinker);
    BOOST_CHECK_EQUAL(ret, adk::ErrorCode::kSuccess);
    ind_prop = sinker.GetProperty();
    std::cout << "ind: " << ind_prop.Dump(true) << std::endl;
    status = ind_prop.GetValue("class_objects..object_collection_status", "");
    BOOST_REQUIRE(status == "failure");
    adk::Monitor::Stop();
}

BOOST_AUTO_TEST_CASE(sinker_plugin_after_monitor_normal_quit)
{
    adk::Monitor::Start();

    TestMonitor test_object;
    adk::MonitorOps monitor_ops;
    monitor_ops.on_collection_indicator = boost::bind(&TestMonitor::OnCollection, &test_object, _1);
    monitor_ops.on_query = boost::bind(&TestMonitor::OnQuery, &test_object, _1, _2, _3);
    monitor_ops.is_collection_indicator = true;
    monitor_ops.collection_interval_milli = 100;
    REGISTER_OBJECT(TestMonitor, "test_object", &monitor_ops);

    sleep(1);

    adk::Monitor::Stop();

    AppSinkerV2 sinker;
    // 退出后获取指标  返回失败
    auto ret = adk::Monitor::PluginSinker(&sinker);
    BOOST_CHECK_EQUAL(ret, adk::ErrorCode::kFailure);
}

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
    monitor_ops.collection_interval_milli = 100;
    REGISTER_OBJECT(TestMonitor, "test_object", &monitor_ops);

    sleep(1);

    adk::Monitor::Stop();
    // 停止后 Plugout
    ret = adk::Monitor::PlugoutSinker(&sinker);
    BOOST_CHECK_EQUAL(ret, adk::ErrorCode::kSuccess);
    ret = adk::Monitor::PlugoutSinker(&sinker);
    BOOST_CHECK_EQUAL(ret, adk::ErrorCode::kSuccess);
}
