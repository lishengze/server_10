// http_agent new API test
// RegisterHttpURL() for register Get/Put HTTP URL
#define BOOST_TEST_MODULE http_agent
#include <arpa/inet.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <assert.h>
#include <atomic>
#include <string>
#include <thread>
#include <random>
#include <functional>
#include <map>
#include <iostream>
#include <adk/http_client.h>
#include <adk/http_server.h>
#include <adk/response_builder.h>
#include <boost/test/included/unit_test.hpp>

using namespace std;

#ifndef adk
#define adk adk_impl
#endif

using HttpClient = adk::http::Client<adk::http::HTTP>;
using HttpServer = adk::http::Server<adk::http::HTTP>;
using RequestPtr = std::shared_ptr<HttpServer::Request>;
using ResponsePtr = std::shared_ptr<HttpServer::Response>;
using ResponseBuilder = adk::http::ResponseBuilder<adk::http::HTTP>;

static const std::string read_pwd = "111111";
static const std::string write_pwd = "222222";

class DoOnExit
{
public:
    DoOnExit(const std::function<void()> &func) : do_on_exit_func_(func)
    {
    }

    ~DoOnExit()
    {
        if (do_on_exit_func_)
            do_on_exit_func_();
    }

    DoOnExit(const DoOnExit&) = delete;

    DoOnExit& operator=(const DoOnExit&) = delete;
private:
    std::function<void()> do_on_exit_func_;
};

static std::string IstreamToStr(std::basic_istream<char> &istream) 
{   
    return std::string((std::istream_iterator<char>(istream)), 
                        std::istream_iterator<char>());
}

static uint16_t GetRandPort()
{
    static std::default_random_engine random_engine(time(nullptr));
    static std::uniform_int_distribution<unsigned> uniform(4000, 65535);
    static std::string addr = "0.0.0.0";
    static char addr_arry[sizeof(struct in_addr)];

    inet_pton(AF_INET, addr.c_str(), addr_arry);

    while (true)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
        {
            continue;
        }

        auto port = uniform(random_engine);
        struct sockaddr_in sock_addr;
        bzero(&sock_addr, sizeof(sock_addr));
        sock_addr.sin_family = AF_INET;
        sock_addr.sin_port   = htons(port);
        memcpy(addr_arry, &sock_addr.sin_addr, sizeof(struct in_addr));
        if (bind(sock, (struct sockaddr*)&sock_addr, sizeof(sock_addr)) != 0)
        {
            close(sock);
            continue;
        }

        close(sock);
        return port;
    }
}

BOOST_AUTO_TEST_CASE(http_authorized)
{
    uint16_t port = GetRandPort(); // get rand port
    HttpServer http_server;
    //set http server config
    http_server.config.address = "127.0.0.1"; //ip
    http_server.config.port = port; // port
    http_server.config.read_password = read_pwd; // read password
    http_server.config.write_password = write_pwd; // write password
    
    // http get handler
    http_server.resource["^/get$"]["GET"] = [](ResponsePtr response, RequestPtr request)
    {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // http post handler
    http_server.resource["^/post$"]["POST"] = [](ResponsePtr response, RequestPtr request)
    {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // http put handler
    http_server.resource["^/put$"]["PUT"] = [](ResponsePtr response, RequestPtr request)
    {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // http delete handler
    http_server.resource["^/delete$"]["DELETE"] = [](ResponsePtr response, RequestPtr request)
    {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // start http server
    
    std::thread http_server_thr = std::thread(std::bind(&HttpServer::start, &http_server));

    DoOnExit do_on_exit([&http_server_thr, &http_server]()
    {
        http_server.stop();
        http_server_thr.join();
    });
    
    std::string addr = "127.0.0.1:";
    addr += std::to_string(port);
    HttpClient http_client(addr);

    std::shared_ptr<HttpClient::Response> resp;
    std::string req_str = "get_test";

    // no provide password
    //test get request not ok
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("GET", "/get", req_str));
    BOOST_REQUIRE(resp->status_code == "401 Unauthorized");

    //test post request not ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("POST", "/post", req_str));
    BOOST_REQUIRE(resp->status_code == "401 Unauthorized");

    //test put request not ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("PUT", "/put", req_str));
    BOOST_REQUIRE(resp->status_code == "401 Unauthorized");

    //test delete request not ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("DELETE", "/delete", req_str));
    BOOST_REQUIRE(resp->status_code == "401 Unauthorized");

    //set read password 
    std::map<std::string, std::string> header;
    header["AMIDSAuthority"] = read_pwd;

    //test get request ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("GET", "/get", req_str, header));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);

    //test post request not ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("POST", "/post", req_str, header));
    BOOST_REQUIRE(resp->status_code == "401 Unauthorized");

    //test put request not ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("PUT", "/put", req_str, header));
    BOOST_REQUIRE(resp->status_code == "401 Unauthorized");

    //test delete request not ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("DELETE", "/delete", req_str, header));
    BOOST_REQUIRE(resp->status_code == "401 Unauthorized");

    //set writ password
    header.clear();
    header.emplace("AMIDSAuthority", write_pwd);

    //test get request ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("GET", "/get", req_str, header));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);

    //test post request ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("POST", "/post", req_str, header));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);

    //test put request ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("PUT", "/put", req_str, header));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);

    //test delete request ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("DELETE", "/delete", req_str, header));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);

    header.clear();
    header["AMIDomainServerClient"] = "__AMIDomainServerClient__";

    //test get request ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("GET", "/get", req_str, header));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);

    //test put request ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("PUT", "/put", req_str, header));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);
}

BOOST_AUTO_TEST_CASE(http_no_authorized)
{
    uint16_t port = GetRandPort(); // get rand port
    HttpServer http_server;
    //set http server config
    http_server.config.address = "127.0.0.1"; //ip
    http_server.config.port = port; // port
    
    // http get handler
    http_server.resource["^/get$"]["GET"] = [](ResponsePtr response, RequestPtr request)
    {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // http post handler
    http_server.resource["^/post$"]["POST"] = [](ResponsePtr response, RequestPtr request)
    {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // http put handler
    http_server.resource["^/put$"]["PUT"] = [](ResponsePtr response, RequestPtr request)
    {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // http delete handler
    http_server.resource["^/delete$"]["DELETE"] = [](ResponsePtr response, RequestPtr request)
    {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // start http server
    std::thread http_server_thr = std::thread(std::bind(&HttpServer::start, &http_server));

    DoOnExit do_on_exit([&http_server_thr, &http_server]()
    {
        http_server.stop();
        http_server_thr.join();
    });

    std::string addr = "127.0.0.1:";
    addr += std::to_string(port);
    HttpClient http_client(addr);

    std::shared_ptr<HttpClient::Response> resp;
    std::string req_str = "get_test";

    // no provide password
    //test get request not ok
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("GET", "/get", req_str));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);

    //test post request not ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("POST", "/post", req_str));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);

    //test put request not ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("PUT", "/put", req_str));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);

    //test delete request not ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("DELETE", "/delete", req_str));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);
}

BOOST_AUTO_TEST_CASE(http_filter_authorized)
{
    uint16_t port = GetRandPort(); // get rand port
    HttpServer http_server;
    //set http server config
    http_server.config.address = "127.0.0.1";      //ip
    http_server.config.port = port;                // port
    http_server.config.read_password = read_pwd;   // read password
    http_server.config.write_password = write_pwd; // write password

    // verification filter resource
    http_server.config.verification_filter_resource["^/filter_test$"].insert("GET");
    http_server.config.verification_filter_resource["/filter_test$"].insert("PUT");
    http_server.config.verification_filter_resource["/filter_test$"].insert("POST");

    // http get handler
    http_server.resource["^/$"]["GET"] = [](ResponsePtr response, RequestPtr request) {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // http get handler
    http_server.resource["^/test.jpg$"]["GET"] = [](ResponsePtr response, RequestPtr request) {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // http get handler
    http_server.resource["^/test.png$"]["GET"] = [](ResponsePtr response, RequestPtr request) {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // http get handler
    http_server.resource["^/test.js$"]["GET"] = [](ResponsePtr response, RequestPtr request) {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // http get handler
    http_server.resource["^/test.css$"]["GET"] = [](ResponsePtr response, RequestPtr request) {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // filter test
    http_server.resource["^/filter_test$"]["GET"] = [](ResponsePtr response, RequestPtr request) {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // filter test
    http_server.resource["^/filter_test$"]["POST"] = [](ResponsePtr response, RequestPtr request) {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // filter test
    http_server.resource["^/filter_test$"]["PUT"] = [](ResponsePtr response, RequestPtr request) {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // http get handler
    http_server.resource["^/get$"]["GET"] = [](ResponsePtr response, RequestPtr request) {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // http post handler
    http_server.resource["^/post$"]["POST"] = [](ResponsePtr response, RequestPtr request) {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // http put handler
    http_server.resource["^/put$"]["PUT"] = [](ResponsePtr response, RequestPtr request) {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // http delete handler
    http_server.resource["^/delete$"]["DELETE"] = [](ResponsePtr response, RequestPtr request) {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // start http server
    std::thread http_server_thr = std::thread(std::bind(&HttpServer::start, &http_server));

    DoOnExit do_on_exit([&http_server_thr, &http_server]() {
        http_server.stop();
        http_server_thr.join();
    });

    std::string addr = "127.0.0.1:";
    addr += std::to_string(port);
    HttpClient http_client(addr);

    std::shared_ptr<HttpClient::Response> resp;
    std::string req_str = "get_test";

    // no provide password
    //test get request not ok
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("GET", "/get", req_str));
    BOOST_REQUIRE(resp->status_code == "401 Unauthorized");

    //test post request not ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("POST", "/post", req_str));
    BOOST_REQUIRE(resp->status_code == "401 Unauthorized");

    //test put request not ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("PUT", "/put", req_str));
    BOOST_REQUIRE(resp->status_code == "401 Unauthorized");

    //test delete request not ok
    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("DELETE", "/delete", req_str));
    BOOST_REQUIRE(resp->status_code == "401 Unauthorized");

    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("GET", "/", req_str));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);

    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("GET", "/test.jpg", req_str));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);

    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("GET", "/test.png", req_str));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);

    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("GET", "/test.js", req_str));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);

    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("GET", "/test.css", req_str));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);

    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("GET", "/filter_test", req_str));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);

    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("POST", "/filter_test", req_str));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);

    http_client.close();
    BOOST_REQUIRE_NO_THROW(resp = http_client.request("PUT", "/filter_test", req_str));
    BOOST_REQUIRE(resp->status_code == "200 OK");
    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);
}


BOOST_AUTO_TEST_CASE(multi_http_authorized)
{
    uint16_t port = GetRandPort(); // get rand port
    HttpServer http_server;
    //set http server config
    http_server.config.address = "127.0.0.1"; //ip
    http_server.config.port = port; // port
    http_server.config.read_password = read_pwd; // read password
    http_server.config.write_password = write_pwd; // write password
    
    // http get handler
    http_server.resource["^/get$"]["GET"] = [](ResponsePtr response, RequestPtr request)
    {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // http post handler
    http_server.resource["^/post$"]["POST"] = [](ResponsePtr response, RequestPtr request)
    {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // http put handler
    http_server.resource["^/put$"]["PUT"] = [](ResponsePtr response, RequestPtr request)
    {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // http delete handler
    http_server.resource["^/delete$"]["DELETE"] = [](ResponsePtr response, RequestPtr request)
    {
        ResponseBuilder rb(response.get());
        rb << request->content.rdbuf();
    };

    // start http server
    std::thread http_server_thr = std::thread(std::bind(&HttpServer::start, &http_server));

    DoOnExit do_on_exit([&http_server_thr, &http_server]()
    {
        http_server.stop();
        http_server_thr.join();
    });
    
    volatile bool is_running = true;
    std::thread set_thr([&]()
    {
        uint32_t i = 0;
        while (is_running)
        {
            std::lock_guard<std::mutex> lck(http_server.config.mtx);
            if (i % 2 == 0)
            {
                http_server.config.read_password = read_pwd;
            }
            else 
            {
                http_server.config.read_password = write_pwd;
            }

            ++i;
        }
    });
    

    std::vector<std::thread> req_thr_vec;
    req_thr_vec.reserve(20);

    for (int i = 0; i < 20; i++)
    {
        req_thr_vec.emplace_back([&]()
        {
            std::string addr = "127.0.0.1:";
            addr += std::to_string(port);
            HttpClient http_client(addr);

            //set read password 
            std::map<std::string, std::string> header;
            header["AMIDSAuthority"] = read_pwd;
            std::shared_ptr<HttpClient::Response> resp;
            std::string req_str = "get_test";

            int i = 0;
            while (i < 3)
            {
                //test get request ok
                http_client.close();
                BOOST_REQUIRE_NO_THROW(resp = http_client.request("GET", "/get", req_str, header));
                if(resp->status_code == "200 OK")
                {
                    BOOST_REQUIRE(IstreamToStr(resp->content) == req_str);
                }
                else 
                {
                    BOOST_REQUIRE(resp->status_code == "401 Unauthorized");
                }
                ++i;
            }
        });
    }

    for (auto &thr : req_thr_vec)
    {
        thr.join();
    }

    is_running = false;
    set_thr.join();
}