#define BOOST_TEST_MODULE adh_web_test
#include <arpa/inet.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <assert.h>
#include <random>
#include <functional>
#include <fstream>
#include <mutex>

#include <boost/test/included/unit_test.hpp>
#include <boost/filesystem.hpp>

#include <adk/web/adk_http.h>

#ifndef adk
#define adk adk_impl
#endif

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

BOOST_AUTO_TEST_CASE(adk_http_test1)
{
    auto port = GetRandPort();
    std::string http_addr = "127.0.0.1:";
    http_addr.append(std::to_string(port));

    adk::web::HttpServer http_server;
    http_server.config.address = "127.0.0.1";
    http_server.config.port = port;

    http_server.RegisterHandler("^/$", "GET",[](adk::web::HttpServer::RequestPtr req, adk::web::HttpServer::ResponsePtr resp)
    {
        resp->http_body = req->http_body;
        for (auto &item : req->header)
        {
            resp->header.emplace(item.first, item.second);
        }
    });

    http_server.RegisterHandler("^/data/(.+)$", "GET",[](adk::web::HttpServer::RequestPtr req, adk::web::HttpServer::ResponsePtr resp)
    {
        resp->http_body = req->path_match[1];
    });

    http_server.RegisterHandler("^/header_test$", "GET",[](adk::web::HttpServer::RequestPtr req, adk::web::HttpServer::ResponsePtr resp)
    {
        resp->header["test_header"] = "test_header";
    });

    BOOST_REQUIRE_NO_THROW(http_server.Start());

    adk::web::HttpClient http_client(http_addr);
    adk::web::HttpClient::ResponsePtr resp_ptr;

    resp_ptr = http_client.Request("GET", "/", "test");
    BOOST_REQUIRE(resp_ptr != nullptr);
    BOOST_REQUIRE(resp_ptr->http_body == "test");

    std::map<std::string, std::string> test_header;
    test_header["test"] = "test";
    http_client.Close();
    resp_ptr = http_client.Request("GET", "/", "test", test_header);
    BOOST_REQUIRE(resp_ptr != nullptr);
    BOOST_REQUIRE(resp_ptr->http_body == "test");
    auto header_it = resp_ptr->header.find("test");
    BOOST_REQUIRE(header_it != resp_ptr->header.end());
    BOOST_REQUIRE(header_it->second == "test");

    http_client.Close();
    resp_ptr = http_client.Request("GET", "/data/test");
    BOOST_REQUIRE(resp_ptr != nullptr);
    BOOST_REQUIRE(resp_ptr->http_body == "test");

    http_client.Close();
    resp_ptr = http_client.Request("GET", "/header_test");
    BOOST_REQUIRE(resp_ptr != nullptr);
    header_it = resp_ptr->header.find("test_header");
    BOOST_REQUIRE(header_it != resp_ptr->header.end());
    BOOST_REQUIRE(header_it->second == "test_header");

    http_server.SetDefaultResponeHeader("default_key1", "default_key1");
    http_server.SetDefaultResponeHeader("default_key2", "default_key2");
    http_client.Close();
    resp_ptr = http_client.Request("GET", "/");
    BOOST_REQUIRE(resp_ptr != nullptr);
    header_it = resp_ptr->header.find("default_key1");
    BOOST_REQUIRE(header_it != resp_ptr->header.end());
    BOOST_REQUIRE(header_it->second == "default_key1");
    header_it = resp_ptr->header.find("default_key2");
    BOOST_REQUIRE(header_it != resp_ptr->header.end());
    BOOST_REQUIRE(header_it->second == "default_key2");

    http_server.Stop();
}

BOOST_AUTO_TEST_CASE(adk_http_test2)
{
    auto port = GetRandPort();
    std::string http_addr = "127.0.0.1:";
    http_addr.append(std::to_string(port));

    adk::web::HttpServer http_server;
    http_server.config.address = "127.0.0.1";
    http_server.config.port = port;
    http_server.config.thread_pool_size = 10;

    http_server.RegisterHandler("^/$", "GET",[](adk::web::HttpServer::RequestPtr req, adk::web::HttpServer::ResponsePtr resp)
    {
        resp->http_body = req->http_body;
        for (auto &item : req->header)
        {
            resp->header.emplace(item.first, item.second);
        }
    });

    BOOST_REQUIRE_NO_THROW(http_server.Start());

    std::vector<std::thread> thr_vec;
    thr_vec.reserve(10);

    for (int i = 0; i < 10; i++)
    {
        thr_vec.emplace_back(std::thread([&]()
        {
            adk::web::HttpClient http_client(http_addr);
            adk::web::HttpClient::ResponsePtr resp_ptr;
            for (int j = 0; j < 10; ++j)
            {
                http_client.Close();
                resp_ptr = http_client.Request("GET", "/", "test");
                if (resp_ptr == nullptr
                    || resp_ptr->http_body != "test")
                {
                    throw std::runtime_error("test failed");    
                }
            }
        }));
    }

    for (auto &thr : thr_vec)
    {
        thr.join();
    }
    
    http_server.Stop();
}

BOOST_AUTO_TEST_CASE(adk_http_test3)
{
    std::string test_web_root = "./.test_web_root";
    boost::system::error_code ec;
    boost::filesystem::remove_all(test_web_root, ec);
    boost::filesystem::create_directories(test_web_root, ec);
    std::ofstream ofs1(test_web_root + "/index.html", std::ios::trunc);
    std::ofstream ofs2(test_web_root + "/test.file", std::ios::trunc);
    BOOST_REQUIRE(ofs1.is_open() && ofs2.is_open());
    ofs1 << "index_file";
    ofs2 << "test_file";
    ofs1.flush();
    ofs2.flush();

    auto port = GetRandPort();
    std::string http_addr = "127.0.0.1:";
    http_addr.append(std::to_string(port));

    adk::web::HttpServer http_server;
    http_server.config.address = "127.0.0.1";
    http_server.config.port = port;
    http_server.config.web_root = test_web_root;
    http_server.RegisterHandlerStaticFileUrl("/test.file");

    BOOST_REQUIRE_NO_THROW(http_server.Start());

    adk::web::HttpClient http_client(http_addr);
    adk::web::HttpClient::ResponsePtr resp_ptr;

    resp_ptr = http_client.Request("GET", "/", "test");
    BOOST_REQUIRE(resp_ptr != nullptr);
    BOOST_REQUIRE(resp_ptr->http_body == "index_file");

    http_client.Close();
    resp_ptr = http_client.Request("GET", "/test.file");
    BOOST_REQUIRE(resp_ptr != nullptr);
    BOOST_REQUIRE(resp_ptr->http_body == "test_file");

    http_server.Stop();
}

BOOST_AUTO_TEST_CASE(adk_http_test4)
{
    auto port = GetRandPort();
    std::string http_addr = "127.0.0.1:";
    http_addr.append(std::to_string(port));

    adk::web::HttpServer http_server;
    http_server.config.address = "127.0.0.1";
    http_server.config.port = port;

    http_server.RegisterHandler("^/$", "GET",[](adk::web::HttpServer::RequestPtr req, adk::web::HttpServer::ResponsePtr resp)
    {
        adk::web::HttpServer::WriteResponseFile(resp, "test_file","test.txt");
    });

    BOOST_REQUIRE_NO_THROW(http_server.Start());

    adk::web::HttpClient http_client(http_addr);
    adk::web::HttpClient::ResponsePtr resp_ptr;

    resp_ptr = http_client.Request("GET", "/");
    BOOST_REQUIRE(resp_ptr != nullptr);
    BOOST_REQUIRE(resp_ptr->http_body == "test_file");
    auto it = resp_ptr->header.find("Content-Disposition");
    BOOST_REQUIRE(it != resp_ptr->header.end());
    BOOST_REQUIRE(it->second == "attachment; filename=test.txt");

    http_server.Stop();
}


BOOST_AUTO_TEST_CASE(adk_http_test5)
{
    auto port = GetRandPort();
    std::string http_addr = "127.0.0.1:";
    http_addr.append(std::to_string(port));

    adk::web::HttpServer http_server;
    http_server.config.address = "127.0.0.1";
    http_server.config.port = port;

    http_server.RegisterHandler("^/$", "GET",[](adk::web::HttpServer::RequestPtr req, adk::web::HttpServer::ResponsePtr resp)
    {

    });

    http_server.RegisterHandler("^/404_not_found/$", "GET",[](adk::web::HttpServer::RequestPtr req, adk::web::HttpServer::ResponsePtr resp)
    {
        resp->status_code = adk::web::http_status_code::not_found;
    });

    BOOST_REQUIRE_NO_THROW(http_server.Start());

    adk::web::HttpClient http_client(http_addr);
    adk::web::HttpClient::ResponsePtr resp_ptr;
    resp_ptr = http_client.Request("GET", "/");
    BOOST_REQUIRE(resp_ptr != nullptr);
    BOOST_REQUIRE(resp_ptr->status_code == "200 OK");
    BOOST_REQUIRE(resp_ptr->http_version == "1.1");

    http_client.Close();
    resp_ptr = http_client.Request("GET", "/404_not_found/");
    BOOST_REQUIRE(resp_ptr != nullptr);
    BOOST_REQUIRE(resp_ptr->status_code == "404 Not Found");
    BOOST_REQUIRE(resp_ptr->http_version == "1.1");
}