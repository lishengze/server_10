#include <time.h>

#include <adk/http_server.h>
#include <adk/response_builder.h>
#include <adk/http_util.h>

#include <boost/thread.hpp>
#include <boost/program_options.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/thread/thread.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/format.hpp>
#include <deque>
#include <functional>

typedef adk::http::Server<adk::http::HTTP> HttpServer;

typedef std::shared_ptr<HttpServer::Request> RequestPtr;

typedef std::shared_ptr<HttpServer::Response> ResponsePtr;

typedef adk::http::ResponseBuilder<adk::http::HTTP> ResponseBuilder;

boost::mutex* g_lock;
struct QueueState
{
    std::deque<std::string> queue;
    uint32_t is_closed = 0;
    uint32_t padding = 0;

    QueueState()
    {}
};
typedef std::map<std::string, QueueState> QueueMap;
QueueMap* g_http_queue_map;

std::map<uint64_t, std::string>* g_expire_timer_map;

std::deque<std::string>* GetQueue(RequestPtr request)
{
    std::string q_name = adk::http::UrlDecode(request->path_match[1]);
    auto it = g_http_queue_map->find(q_name);
    if (it == g_http_queue_map->end())
    {
        auto it_res = g_http_queue_map->insert(std::make_pair(q_name, QueueState()));
        struct timespec ts_now;
        clock_gettime(CLOCK_REALTIME, &ts_now);
        // 两天后
        uint64_t expire_time = ts_now.tv_sec * 1000000000ul + ts_now.tv_nsec + 83600ul*2000000000ul; 
        g_expire_timer_map->emplace(expire_time, q_name);

        return &it_res.first->second.queue;
    }
    return &it->second.queue;
}

void QueueLength(ResponsePtr response, RequestPtr request)
{
    ResponseBuilder rb(response.get());

    boost::mutex::scoped_lock lock_guard(*g_lock);
    auto* g_http_queue = GetQueue(request);

    rb.set_status_code(adk::http::status_code::kOK);
    rb << (boost::format("queue_length:%1%") % g_http_queue->size()).str();
}

void Push(ResponsePtr response, RequestPtr request)
{
    ResponseBuilder rb(response.get());

    boost::mutex::scoped_lock lock_guard(*g_lock);
    auto* g_http_queue = GetQueue(request);

    g_http_queue->push_back(request->content.string());
    rb.set_status_code(adk::http::status_code::kOK);
}

void Pop(ResponsePtr response, RequestPtr request)
{
    ResponseBuilder rb(response.get());

    boost::mutex::scoped_lock lock_guard(*g_lock);
    auto* g_http_queue = GetQueue(request);

    if (g_http_queue->size() > 0)
    {
        auto& content = g_http_queue->front();
        rb << content;
        g_http_queue->pop_front();
    }
    rb.set_status_code(adk::http::status_code::kOK);
}

void Clear(ResponsePtr response, RequestPtr request)
{
    ResponseBuilder rb(response.get());
    boost::mutex::scoped_lock lock_guard(*g_lock);
    auto* g_http_queue = GetQueue(request);
    
    g_http_queue->clear();
    rb.set_status_code(adk::http::status_code::kOK);
}

void Close(ResponsePtr response, RequestPtr request)
{
    ResponseBuilder rb(response.get());
    boost::mutex::scoped_lock lock_guard(*g_lock);
    QueueState* q_s = (QueueState*)GetQueue(request);
    q_s->is_closed = 1;

    rb.set_status_code(adk::http::status_code::kOK);
}

void IsClose(ResponsePtr response, RequestPtr request)
{
    ResponseBuilder rb(response.get());

    boost::mutex::scoped_lock lock_guard(*g_lock);
    auto* g_http_queue = GetQueue(request);
    QueueState* q_s = (QueueState*)g_http_queue;
    if (q_s->is_closed == 1
        && g_http_queue->empty())
    {
        rb << R"({"closed":"true"})";
    }
    else
    {
        rb << R"({"closed":"false"})";
    }

    rb.set_status_code(adk::http::status_code::kOK);
}

void TimerRun()
{
    while (true)
    {
        struct timespec ts_now;
        clock_gettime(CLOCK_REALTIME, &ts_now);
        uint64_t expire_time = ts_now.tv_sec * 1000000000ul + ts_now.tv_nsec; 

        {
            boost::mutex::scoped_lock lock_guard(*g_lock);
            auto it = g_expire_timer_map->begin();
            for (; it != g_expire_timer_map->end(); ++it)
            {
                if (it->first < expire_time) // 已经过期
                {
                    auto it_2 = g_http_queue_map->find(it->second);
                    if (it_2 != g_http_queue_map->end()
                        && it_2->second.is_closed == 1)
                    {
                        g_http_queue_map->erase(it_2);
                    }
                }
                else
                {
                    break;
                }

                it = g_expire_timer_map->erase(it);
            }
        }
        
        // 每1小时秒检查一次
        sleep(3600);
    }
}

namespace po = boost::program_options;
namespace ph = std::placeholders;
int main(int argc, char* argv[])
{
    g_lock = new boost::mutex();
    g_http_queue_map = new QueueMap();
    g_expire_timer_map = new std::map<uint64_t, std::string>();

    po::options_description desc("Allowed options", 120);   // parse command line
    desc.add_options()
    ("help,h", "show this information")
    ("listen-ip",  po::value<std::string>()->default_value("0.0.0.0"), "the http ip address of queuing service")
    ("listen-port",  po::value<uint32_t>()->default_value(12556), "the http port of queuing service")
    ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm); 

    if (vm.count("help"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    HttpServer http_server;
    http_server.config.address = vm["listen-ip"].as<std::string>();
    http_server.config.port = vm["listen-port"].as<uint32_t>();

    http_server.resource["^/(.+)/queue_length/$"]["GET"]
            = std::bind(&QueueLength, ph::_1, ph::_2);

    http_server.resource["^/(.+)/push/$"]["PUT"]
            = std::bind(&Push, ph::_1, ph::_2);

    http_server.resource["^/(.+)/pop/$"]["PUT"]
            = std::bind(&Pop, ph::_1, ph::_2);

    http_server.resource["^/(.+)/clear/$"]["PUT"]
            = std::bind(&Clear, ph::_1, ph::_2);

    http_server.resource["^/(.+)/close/$"]["PUT"]
            = std::bind(&Close, ph::_1, ph::_2);

    http_server.resource["^/(.+)/is_close/$"]["GET"]
            = std::bind(&IsClose, ph::_1, ph::_2);

    boost::thread timer_thread = boost::thread(boost::bind(TimerRun));
    try
    {
        http_server.start();
    }
    catch(const std::exception &err)
    {
        std::cout << err.what() << std::endl;
        return 1;
    }
    return 0;
}
