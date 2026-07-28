#include <unistd.h>
#include <iostream>
#include <adk/web/adk_http.h>

int main(int argc, char const *argv[])
{
    using namespace adk::web;

    HttpServer http_server;
    http_server.config.address = "0.0.0.0";
    http_server.config.port = 8000;
    http_server.config.web_root = "./web";

    http_server.RegisterHandler("/hello_world", "GET", 
                                [](HttpServer::RequestPtr req, HttpServer::ResponsePtr resp)
                                {
                                    std::cout << req->method << " " << req->path << std::endl;
                                    resp->http_body = "hello world\n";
                                });

    http_server.RegisterHandler("^/data/(.+)$", "GET", 
                                [](HttpServer::RequestPtr req, HttpServer::ResponsePtr resp)
                                {
                                    std::cout << req->method << " " << req->path<< std::endl;

                                    //请求 url 中匹配的 (.+) 字符
                                    std::cout << req->path_match[1] << std::endl;
                                    resp->http_body = req->path_match[1];
                                    resp->http_body.append("\n");

                                    // 如果客户端请求不符合预期
                                    // 则可以设置 http 响应状态码，默认状态码为 http_status_code::ok
                                    // resp-> status_code = http_status_code::bad_request;
                                });

    http_server.RegisterHandlerStaticFileUrl("^/static/(.+)$"); // 处理获取其他静态文件的 http 请求

    http_server.Start();

    sleep(3600);

    http_server.Stop();

    return 0;
}