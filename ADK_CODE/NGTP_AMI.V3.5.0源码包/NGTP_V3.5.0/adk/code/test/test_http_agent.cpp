// http_agent new API test
// RegisterHttpURL() for register Get HTTP URL
// RegisterHttpURL() for register Put HTTP URl

#include <adk/http_client.h>
#include <adk/http_server.h>
#include <adk/monitor/http_agent.h>
#include <assert.h>
#include <atomic>
#include <chrono>
#include <iostream>

using namespace std;

typedef adk::http::Client<adk::http::HTTP> HttpClient;

int main(int argc, char* argv[])
{
    adk::monitor::HttpAgent server;

    server.Start(8080, "127.0.0.1", [](const boost::system::system_error& ec) {
        std::cout << ec.code() << ": " << ec.what() << std::endl;
    });

    atomic_int counter { 0 };
    server.RegisterGetHttpURL("echo", [&](std::string const& s) -> std::string {
        counter++;
        return s;
    });

    // Wait for server to start so that the client can connect
    this_thread::sleep_for(chrono::seconds(1));

    std::cout << "initial: " << counter << std::endl;
    HttpClient client("127.0.0.1:8080");

    // NOTE: the actual url will be /app/url
    auto r1 = client.request("GET", "/app/echo", "hello");
    // in case r1->content is end
    std::string resp1(std::istream_iterator<char>(r1->content), {});
    cout << resp1;

    auto r2 = client.request("GET", "/app/echo", " world");
    std::string resp2(std::istream_iterator<char>(r2->content), {});
    cout << resp2;

    std::cout << "after two Get request: " << counter << std::endl;

    // register
    server.RegisterPutHttpURL("clear", [&](std::string const& s) -> bool {
        counter = 0;
        return true;
    });

    client.request("PUT", "/app/clear");
    std::cout << "after one Put request: " << counter << std::endl;
    return 0;
}