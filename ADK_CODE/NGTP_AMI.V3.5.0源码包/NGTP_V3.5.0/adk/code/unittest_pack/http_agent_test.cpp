// http_agent new API test
// RegisterHttpURL() for register Get/Put HTTP URL
#define BOOST_TEST_MODULE http_agent
#include <adk_pack/http_client.h>
#include <adk_pack/http_server.h>
#include <adk_pack/monitor/http_agent.h>
#include <assert.h>
#include <atomic>
#include <boost/test/included/unit_test.hpp>
#include <chrono>
#include <iostream>

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