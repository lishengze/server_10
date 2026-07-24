#include <adk/error_code.h>
#include <adk/simple_rate_controller.h>
#include <adk/io_engine.h>
#include <adk/arch/generic.h>
#include <adk/arch/generic.h>

#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>

#include <iostream>
#include <string>
#include <thread>
#include <atomic>

// using namespace adk::rudp;
#define BUFF_SIZE 512
#define RUDP_DATA_HEAD_LEN 28

struct ThreadInfo
{
	uint64_t	 	connection_id;
	std::thread::id thread_id;
	std::thread* 	thd;
};
volatile bool g_is_connected = false;
volatile bool g_is_running = true;
uint32_t msg_size = 256;
uint32_t total_send_count = 10000000;
uint32_t rate = 1;
std::vector<adk::io_engine::Endpoint*> g_ep_vec;
std::vector<ThreadInfo> g_ep_thread_vec;

std::atomic<uint64_t> g_recv_count(0);
std::atomic<uint64_t> g_send_count(0);
std::atomic<uint64_t> g_send_error_count(0);

class MyConnectHandler : public adk::io_engine::ConnectHandler
{
public:
	void OnConnect(adk::io_engine::Endpoint* endpoint, adk::io_engine::Property& ep_props)
	{
		std::cout << "Endpoint "<< endpoint->endpoint_id() << " connect success" << std::endl;
		g_is_connected = true;
		g_ep_vec.push_back(endpoint);
	}
};

class MyEventHandler : public adk::io_engine::EventHandler
{
public:
	void OnEvent(adk::io_engine::Endpoint* endpoint, adk::io_engine::Event* event)
	{
		std::cout << "Endpoint " << endpoint->endpoint_id() 
				  << " receive event: " << event->what() 
				  << std::endl;
		if (event->level() == adk::io_engine::EventLevel::kError)
		{
			endpoint->Close();
		}

		g_is_running = false;
		g_is_connected = false;
	}
};

class MyMessageHandler : public adk::io_engine::MessageHandler
{
public:
	int32_t OnMessage(adk::io_engine::Message* message)
	{
		char* data = message->data();
		uint32_t data_len = message->data_len();
		uint32_t recv_len = 0u;
		uint32_t left_len = data_len;

		while(true)
		{
			// 当前消息接收完成，退出等待下一条消息
			if (0 == left_len)
			{
				break;
			}  

			if (left_len < msg_size)  
			{
				message->set_follow_up(recv_len, msg_size - left_len);
				return kFollowUp;
			}

            uint64_t sqn = *(uint64_t*)(data + recv_len);

			recv_len += msg_size;
			left_len -= msg_size;

            ++g_recv_count;
		}

		return adk::ErrorCode::kSuccess;
	}
};


class MyPreTxHandler : public adk::io_engine::PreSendHandler
{
    int32_t OnTxMessageBefore(void* ep_share_ctx, adk::io_engine::Message* message) override
    {
        clock_gettime(CLOCK_REALTIME, (struct timespec*)(message->data() + 8 + sizeof(struct timespec)));
        return adk::ErrorCode::kSuccess;
    }

    void OnTxMessageAfter(void* ep_share_ctx, int32_t result) override
    {

    }
};

void ThroughputPrint()
{
	while (g_is_running)
	{
		std::cout << "total send " << g_send_count << " messsage, and send " << g_send_error_count << " failed" << std::endl;
		sleep(1);
	}
}

void SendMsg(adk::io_engine::Endpoint* endpoint, uint64_t begin_sqn)
{
	char* send_buff = new char[msg_size];
	memset(send_buff, 0, msg_size);
	adk::SimpleRateCtrl rate_ctl(rate);
	
    uint64_t send_count = 0;
    uint64_t sqn = begin_sqn;
	while (g_is_running && ++send_count <= total_send_count)
	{
        *(uint64_t*)(send_buff) = sqn;
		rate_ctl.Wait();
        do
        {
            clock_gettime(CLOCK_REALTIME, (struct timespec*)(send_buff + 8));
            auto ret = endpoint->SendMsg<false, true>(send_buff, msg_size);
            if (ADK_LIKELY(ret == adk::ErrorCode::kSuccess))
            {
                break;
            }
            else if (ADK_UNLIKELY(ret == adk::ErrorCode::kWouldblock))
            {
                ++g_send_error_count;
                continue;
                
            }
            else
            {
                std::cout << "send msg to [" << endpoint->remote_ip() << ":" << endpoint->remote_port() << "] failed, errno: " << errno << std::endl;
                g_is_running = false;
                return;
            }
        }while(g_is_running);

        ++sqn;
        ++g_send_count;
	}
}

int main(int argc, char const *argv[])
{
	//解析参数
	namespace po = boost::program_options;
    po::options_description option_desc;
    po::variables_map option_vm;

    option_desc.add_options()
    ("help,h", "show this information")
    ("local-addr,c",po::value<std::string>(), "本端地址端口, example: 1.1.1.40:50001")
    ("remote-addr,r",po::value<std::string>(), "服务端地址端口, example: 1.1.1.42:40001")
    ("send-count", po::value<uint32_t>(), "发送消息总数, default 10000000")
    ("send-rate", po::value<uint32_t>(), "发送速率, default 1")
    ("msg-size", po::value<uint32_t>(), "发送消息大小, default 256")
    ("connections,n", po::value<uint16_t>(), "建立连接数目, 默认建立1条连接")
    ("low-latency", "是否使用低时延模式");

    po::store(po::parse_command_line(argc, argv, option_desc), option_vm);
    po::notify(option_vm);

    if (option_vm.count("help"))
    {
        std::cout << option_desc << std::endl;
        return 0;
    }

    //注册TxEndpoint
    if (option_vm.count("local-addr") == 0)
    {
    	std::cout << "please input option 'local-addr' "<< std::endl;
    	return -1;
    }
    std::string local_addr = option_vm["local-addr"].as<std::string>();

    if (option_vm.count("remote-addr") == 0)
    {
    	std::cout << "please input option 'remote-addr' "<< std::endl;
    	return -1;
    }
    std::string remote_addr = option_vm["remote-addr"].as<std::string>();
    
    if (option_vm.count("send-count") != 0)
    {
    	total_send_count = option_vm["send-count"].as<uint32_t>();
    }

    if (option_vm.count("send-rate") != 0)
    {
    	rate = option_vm["send-rate"].as<uint32_t>();
    }

    if (option_vm.count("msg-size") != 0)
    {
    	msg_size = option_vm["msg-size"].as<uint32_t>();
    }

    uint16_t conn_num = 1;
    if (option_vm.count("connections") != 0)
    {
    	conn_num = option_vm["connections"].as<uint16_t>();
    }

    bool low_latency = false;
    if (option_vm.count("low-latency") != 0)
    {
        low_latency = true;    
    }

    std::vector<std::string> splits;
    boost::split(splits, local_addr, boost::is_any_of(":"), boost::token_compress_on);
    std::string local_ip = splits[0];
    uint16_t local_port = std::atoi(splits[1].c_str());

    boost::split(splits, remote_addr, boost::is_any_of(":"), boost::token_compress_on);
    std::string remote_ip = splits[0];
    uint16_t remote_port = std::atoi(splits[1].c_str());

	MyConnectHandler connect_handler;
	MyEventHandler event_handler;
	MyMessageHandler message_handler;
    MyPreTxHandler pre_tx_handler;

	adk::io_engine::Property props;
	props(adk::io_engine::config::kMessageIp, local_ip)
	     (adk::io_engine::config::kIsTxLowLatency, low_latency)
         (adk::io_engine::config::kIsRxLowLatency, low_latency)
         (adk::io_engine::config::kPreSendHandler, &pre_tx_handler)
	     ;

	adk::io_engine::TcpEngine* tcp_engine = adk::io_engine::TcpEngine::Create(props);
	if (tcp_engine == nullptr)
	{
		std::cout << "create tcp_engine failed" << std::endl;
		return -1;
	}

	adk::io_engine::Property prop;
	prop(adk::io_engine::config::endpoint::kRemoteIp, remote_ip)
	     (adk::io_engine::config::endpoint::kRemotePort, remote_port)
	     (adk::io_engine::config::endpoint::kReuseAddr, true)
	     (adk::io_engine::config::endpoint::kTcpNoDelay, true)
	     (adk::io_engine::config::endpoint::kConnectHandler, &connect_handler)
	     (adk::io_engine::config::endpoint::kEventHandler, &event_handler)
	     (adk::io_engine::config::endpoint::kMessageHandler, &message_handler)
	     ;

	for (int i = 0; i < conn_num; ++i)
	{
		adk::io_engine::Endpoint* endpoint = tcp_engine->Connect(prop);
		if (endpoint == nullptr)
		{
			std::cout << "start " << i+1 << " connect failed," << std::endl;
			break;
		}
	}

	uint32_t i = 0;
	while (g_ep_vec.size() != conn_num)
	{
		sleep(1);
		if (++i == 60)	//最大等待60秒，保证所有连接建立成功
		{
			std::cout << "all conneciton did not connected" << std::endl;
			return -1;
		}
	}

	//收发消息
    i = 0;
	for (auto endpoint : g_ep_vec)
    {
        ThreadInfo thread_info;
        std::thread* thd = new std::thread(&SendMsg, endpoint, i * total_send_count);
        thread_info.connection_id = endpoint->endpoint_id();
        thread_info.thread_id = thd->get_id();
        thread_info.thd = thd;

        std::cout << "Connection " << endpoint->endpoint_id() << " start send_msg thread "<< thread_info.thread_id  << " ..." << std::endl;
        g_ep_thread_vec.push_back(thread_info);

        ++i;     
    }

    while (g_is_running)
    {
        std::cout << "total send " << g_send_count 
                  << " messsage, and send " << g_send_error_count 
                  << " failed, receive " << g_recv_count << std::endl;

        // for (auto th : g_ep_thread_vec)
        // {
        //     std::cout << "Connection " << th.connection_id << " send msg " << th.send_count
        //               << ", send failed msg " << th.send_error_count << std::endl;
        // }
        if (g_recv_count == g_ep_vec.size() * total_send_count)
        {
            break;
        }
        sleep(1);
    }

	sleep(1);
    g_is_running == false;
	for (auto th : g_ep_thread_vec)
	{
		if (th.thd->joinable())
		{
			th.thd->join();
		}
	}
	
	std::cout << "total send " << total_send_count
			  << " msgs, and send " 
			  << g_send_error_count << " failed, receive " 
			  << g_recv_count << " msgs" << std::endl;

	adk::io_engine::TcpEngine::Destroy(tcp_engine);

	return 0;
}
