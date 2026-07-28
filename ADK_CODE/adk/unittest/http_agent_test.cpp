// http_agent new API test
// RegisterHttpURL() for register Get/Put HTTP URL
#define BOOST_TEST_MODULE http_agent
#include <adk/http_client.h>
#include <adk/http_server.h>
#include <adk/monitor/http_agent.h>
#include <adk/monitor/monitor.h>
#include <adk/monitor/indicator_writer.h>
#include <assert.h>
#include <atomic>
#include <boost/test/included/unit_test.hpp>
#include <chrono>
#include <iostream>
#include <adk/json/json.hpp>

using namespace std;

typedef adk::http::Client<adk::http::HTTP> HttpClient;

BOOST_AUTO_TEST_CASE(RegisterHTTPURL)
{
    adk::monitor::HttpAgent server;

    server.Start(8080, "127.0.0.1", [](const boost::system::system_error& ec) {
        std::cout << ec.code() << ": " << ec.what() << std::endl;
    });

    atomic_int counter {0};
    server.RegisterGetHttpURL("echo\\?([[:print:]]*)", [&](std::string const& s) -> std::string {
        counter++;
        return s;
    });

    // Wait for server to start so that the client can connect
    this_thread::sleep_for(chrono::seconds(1));

    HttpClient client("127.0.0.1:8080");

    // NOTE: the actual url will be /app/url
    // register GET URL
    std::string req1("hello"), req2("world"), req3("cpp");

    auto r1 = client.request("GET", "/app/echo?" + req1);
    std::string resp1(std::istream_iterator<char>(r1->content), {});
    BOOST_CHECK_EQUAL(req1, resp1);
    BOOST_CHECK_EQUAL(counter, 1);

    auto r2 = client.request("GET", "/app/echo?" + req2);
    std::string resp2(std::istream_iterator<char>(r2->content), {});
    BOOST_CHECK_EQUAL(req2, resp2);
    BOOST_CHECK_EQUAL(counter, 2);

    // register PUT URL
    server.RegisterPutHttpURL("clear", [&](std::string const& s) -> bool {
        counter = 0;
        return true;
    });

    // register new url should not affect old ones
    auto r3 = client.request("GET", "/app/echo?" + req3);
    std::string resp3(std::istreambuf_iterator<char>(r3->content), {});
    BOOST_CHECK_EQUAL(req3, resp3);
    BOOST_CHECK_EQUAL(counter, 3);

    client.request("PUT", "/app/clear");
    BOOST_CHECK_EQUAL(counter, 0);
}

bool OnCollectIndicator(boost::property_tree::ptree& indicator)
{
    return true;
}

BOOST_AUTO_TEST_CASE(RegisterHTTPURLEventPaged)
{
    std::string context = "Test";
    adk::monitor::HttpAgent server;
    std::shared_ptr<HttpClient::Response> resp;

    adk::MonitorOps ctx_mon_ops;
    ctx_mon_ops.is_collection_indicator = true;
    ctx_mon_ops.on_collection_indicator = &OnCollectIndicator;
    adk::EventChannel *ec = adk::Monitor::RegisterObject("Context", context, &ctx_mon_ops);
    BOOST_REQUIRE(ec != nullptr);

    if (adk::Monitor::Start() != adk::ErrorCode::kSuccess)
    {
        std::cout << "Start monitor failed." << std::endl;
    }

    server.Start(9648, "127.0.0.1", [](const boost::system::system_error& ec) {
        std::cout << ec.code() << ": " << ec.what() << std::endl;
    });

    boost::property_tree::ptree content;
    auto settime = [&](uint64_t shift) {
        content.put("Context", context);
        boost::property_tree::ptree ptree;
        time_t time_value;
        time(&time_value);
        ptree.put("TimeStamp", time_value + shift);
        content.put_child("property", ptree);
        ec->PushEvent(content);
    };

    settime(0);
    settime(100);
    settime(-200);
    settime(200);
    settime(-100);

    std::string url;
    HttpClient client("127.0.0.1:9648");
    url.assign("/event_paged/?page_no=1&page_size=3");
    BOOST_REQUIRE_NO_THROW(resp = client.request("GET", url));
    BOOST_REQUIRE(resp->status_code == "200 OK");

    //std::string data(std::istream_iterator<char>(resp->content), {});

    nlohmann::json stream_js;
    resp->content >> stream_js;
    for (uint64_t i = 0; i < stream_js.size(); i++)
    {
        if (i > 0)
        {
            BOOST_REQUIRE(stream_js[i-1].value("TimeStamp", 0) >= stream_js[i].value("TimeStamp", 0));
        }
        BOOST_REQUIRE(stream_js[i].value("Context", std::string()) == context);
        std::cout << stream_js[i].dump(0) << std::endl;
    }
    BOOST_REQUIRE(stream_js.size() == 3);
    stream_js.clear();
    client.close();

    url.assign("/event_paged/?page_no=2&page_size=3");
    BOOST_REQUIRE_NO_THROW(resp = client.request("GET", url));
    BOOST_REQUIRE(resp->status_code == "200 OK");

    //std::string data(std::istream_iterator<char>(resp->content), {});

    resp->content >> stream_js;
    for (uint64_t i = 0; i < stream_js.size(); i++)
    {
        if (i > 0)
        {
            // 判断时间。从小到大顺序
            BOOST_REQUIRE(stream_js[i-1].value("TimeStamp", 0) >= stream_js[i].value("TimeStamp", 0));
        }
        // 判断内容
        BOOST_REQUIRE(stream_js[i].value("Context", std::string()) == context);
        std::cout << stream_js[i].dump(0) << std::endl;
    }
    BOOST_REQUIRE(stream_js.size() == 2);
    stream_js.clear();
    client.close();

    // 请求超过event事件左边界，无数据返回
    url.assign("/event_paged/?page_no=3&page_size=3");
    BOOST_REQUIRE_NO_THROW(resp = client.request("GET", url));
    BOOST_REQUIRE(resp->status_code == "200 OK");

    resp->content >> stream_js;
    BOOST_REQUIRE(stream_js.size() == 0);
    stream_js.clear();
    client.close();

    // 异常检查

    // 非法参数
    url.assign("/event_paged/?page_no=1000&page_size=1000");
    BOOST_REQUIRE_NO_THROW(resp = client.request("GET", url));
    BOOST_REQUIRE(resp->status_code == "200 OK");

    resp->content >> stream_js;
    BOOST_REQUIRE(stream_js.size() == 0);
    stream_js.clear();
    client.close();

    // 非法参数
    url.assign("/event_paged/?page_no=a&page_size=b");
    BOOST_REQUIRE_NO_THROW(resp = client.request("GET", url));
    BOOST_REQUIRE(resp->status_code == "200 OK");

    resp->content >> stream_js;
    BOOST_REQUIRE(stream_js.size() == 0);
    stream_js.clear();
    client.close();

    // 参数不完整
    url.assign("/event_paged/?page_no=1");
    BOOST_REQUIRE_NO_THROW(resp = client.request("GET", url));
    BOOST_REQUIRE(resp->status_code == "200 OK");

    resp->content >> stream_js;
    BOOST_REQUIRE(stream_js.size() == 0);
    stream_js.clear();
    client.close();

    // 参数负值
    url.assign("/event_paged/?page_no=-1");
    BOOST_REQUIRE_NO_THROW(resp = client.request("GET", url));
    BOOST_REQUIRE(resp->status_code == "200 OK");

    resp->content >> stream_js;
    BOOST_REQUIRE(stream_js.size() == 0);
    stream_js.clear();
    client.close();

    // 参数过大
    url.assign("/event_paged/?page_no=10000");
    BOOST_REQUIRE_NO_THROW(resp = client.request("GET", url));
    BOOST_REQUIRE(resp->status_code == "200 OK");

    resp->content >> stream_js;
    BOOST_REQUIRE(stream_js.size() == 0);
    stream_js.clear();
    client.close();

    // 参数不完整
    url.assign("/event_paged/?page_size=1");
    BOOST_REQUIRE_NO_THROW(resp = client.request("GET", url));
    BOOST_REQUIRE(resp->status_code == "200 OK");

    resp->content >> stream_js;
    BOOST_REQUIRE(stream_js.size() == 0);
    stream_js.clear();
    client.close();

    // 参数负值
    url.assign("/event_paged/?page_size=-1");
    BOOST_REQUIRE_NO_THROW(resp = client.request("GET", url));
    BOOST_REQUIRE(resp->status_code == "200 OK");

    resp->content >> stream_js;
    BOOST_REQUIRE(stream_js.size() == 0);
    stream_js.clear();
    client.close();

    // 参数过大
    url.assign("/event_paged/?page_size=1000000");
    BOOST_REQUIRE_NO_THROW(resp = client.request("GET", url));
    BOOST_REQUIRE(resp->status_code == "200 OK");

    resp->content >> stream_js;
    BOOST_REQUIRE(stream_js.size() == 0);
    stream_js.clear();
    client.close();

    // 无参数
    url.assign("/event_paged/?");
    BOOST_REQUIRE_NO_THROW(resp = client.request("GET", url));
    BOOST_REQUIRE(resp->status_code == "200 OK");

    resp->content >> stream_js;
    BOOST_REQUIRE(stream_js.size() == 0);
    stream_js.clear();
    client.close();
}


BOOST_AUTO_TEST_CASE(StopServer)
{
    adk::monitor::HttpAgent server;

    server.Start(8080, "127.0.0.1", [](const boost::system::system_error& ec) {
        std::cout << ec.code() << ": " << ec.what() << std::endl;
    });

    atomic_int counter {0};
    server.RegisterGetHttpURL("echo\\?([[:print:]]*)", [&](std::string const& s) -> std::string {
        counter++;
        return s;
    });

    // Wait for server to start so that the client can connect
    this_thread::sleep_for(chrono::seconds(1));

    HttpClient client("127.0.0.1:8080");
    // set request timeout deadline
    client.config.timeout = 5;

    // NOTE: the actual url will be /app/url
    // register GET URL
    std::string req1("hello"), req2("world"), req3("cpp");

    auto r1 = client.request("GET", "/app/echo?" + req1);
    std::string resp1(std::istream_iterator<char>(r1->content), {});
    BOOST_CHECK_EQUAL(req1, resp1);
    BOOST_CHECK_EQUAL(counter, 1);

    auto r2 = client.request("GET", "/app/echo?" + req2);
    std::string resp2(std::istream_iterator<char>(r2->content), {});
    BOOST_CHECK_EQUAL(req2, resp2);
    BOOST_CHECK_EQUAL(counter, 2);

    server.Stop();

    // server has stopped, cannot request
    try
    {
        auto r3 = client.request("GET", "/app/echo?" + req3);
    }
    catch(const std::exception& e)
    {
        std::string error = e.what();
        BOOST_REQUIRE(error == "Operation canceled");
    }
    BOOST_CHECK_EQUAL(counter, 2);

    // distruct server, exit normal
}