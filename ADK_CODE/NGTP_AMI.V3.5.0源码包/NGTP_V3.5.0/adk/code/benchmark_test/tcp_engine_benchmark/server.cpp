#include <adk/property.h>
#include <adk/error_code.h>
#include <adk/io_engine.h>
#include <adk/arch/generic.h>

#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>

#include <iostream>
#include <map>
#include <string>
#include <atomic>

std::map<uint64_t, adk::io_engine::Endpoint*> g_endpoint_map;
std::atomic<uint64_t> g_receive_count(0);
uint32_t msg_size = 256;
volatile bool g_is_running = true;

class MyAcceptHandler : public adk::io_engine::AcceptHandler
{
public:
	void OnAccept(adk::io_engine::Endpoint* endpoint, adk::io_engine::Property& ep_props)
	{
		std::cout << "Endpoint "<< endpoint->endpoint_id() << " connect success" << std::endl;
		g_endpoint_map.insert(std::make_pair(endpoint->endpoint_id(), endpoint));
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
	}
};

class MyMessageHandler : public adk::io_engine::MessageHandler
{
public:
	MyMessageHandler()
	{
		buff = new char[msg_size];
	}

	int32_t OnMessage(adk::io_engine::Message* message)
	{
		// std::cout << "receive len: " << len  << " data: " << data << std::endl;
		char* data = message->data();
		uint32_t data_len = message->data_len();
		uint32_t recv_len = 0u;
		uint32_t left_len = data_len;
		adk::io_engine::Endpoint* endpoint = message->endpoint();
		while(g_is_running)
		{
			left_len = data_len - recv_len;
			// 当前buffer接收完成，退出等待下一条消息
			if (0 == left_len)
			{
				return adk::ErrorCode::kSuccess;
			}  

			// 当前buffer不足一个完整消息
			if (left_len < msg_size)  
			{
				message->set_follow_up(recv_len, msg_size - left_len);
				return kFollowUp;
			}

            clock_gettime(CLOCK_REALTIME, (struct timespec*)(data + recv_len + 8 + 2 * sizeof(struct timespec)));

			++g_receive_count;
			memcpy(buff, data + recv_len, msg_size);

            do
            {
                clock_gettime(CLOCK_REALTIME, (struct timespec*)(buff + 8 + 3 * sizeof(struct timespec)));
                auto ret = endpoint->SendMsg<false, true>(buff, msg_size);
                if (ADK_LIKELY(ret == adk::ErrorCode::kSuccess))
                {
                    break;
                }
                else if (ADK_UNLIKELY(ret == adk::ErrorCode::kWouldblock))
                {
                    continue;
                }
                else
                {
                    std::cout << "send msg failed, errno: " << ret << std::endl;
                    g_is_running = false;
                    return adk::ErrorCode::kSuccess;
                }
            }while(g_is_running);
			

			recv_len += msg_size;
			left_len -= msg_size;
		}
	}
private:
	char* buff = nullptr;		
};

class MyPreTxHandler : public adk::io_engine::PreSendHandler
{
    int32_t OnTxMessageBefore(void* ep_share_ctx, adk::io_engine::Message* message) override
    {
        clock_gettime(CLOCK_REALTIME, (struct timespec*)(message->data() + 8 + 4 * sizeof(struct timespec)));
        return adk::ErrorCode::kSuccess;
    }

    void OnTxMessageAfter(void* ep_share_ctx, int32_t result) override
    {

    }
};

int main(int argc, char const *argv[])
{
	//解析参数
	namespace po = boost::program_options;
    po::options_description option_desc;
    po::variables_map option_vm;

    option_desc.add_options()
    ("help,h", "show this information")
    ("local-addr,s",po::value<std::string>(), "local data address, example: 1.1.1.40:60001")
    ("msg-size", po::value<uint32_t>(), "send message size, default 256")
    ("low-latency", "是否使用低时延模式");

    po::store(po::parse_command_line(argc, argv, option_desc), option_vm);
    po::notify(option_vm);

    if (option_vm.count("help"))
    {
        std::cout << option_desc << std::endl;
        return 0;
    }

    if (option_vm.count("msg-size") != 0)
    {
    	msg_size = option_vm["msg-size"].as<uint32_t>();
    }

    bool low_latency = false;
    if (option_vm.count("low-latency") != 0)
    {
        low_latency = true;    
    }

    if (option_vm.count("local-addr") == 0)
    {
    	std::cout << "please input option 'local_addr' "<< std::endl;
    	return -1;
    }
    std::string local_addr = option_vm["local-addr"].as<std::string>();

    std::vector<std::string> splits;
    boost::split(splits, local_addr, boost::is_any_of(":"), boost::token_compress_on);
    std::string local_ip = splits[0];
    uint16_t local_port = std::atoi(splits[1].c_str());

	MyAcceptHandler accept_handler;
	MyEventHandler event_handler;
	MyMessageHandler message_handler;
    MyPreTxHandler pre_tx_handler;

	adk::Property props;
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

	adk::io_engine::Property accept_props;
    accept_props(adk::io_engine::config::endpoint::kListenIp, local_ip)
            	(adk::io_engine::config::endpoint::kListenPort, local_port)
            	(adk::io_engine::config::endpoint::kReuseAddr, true)
            	(adk::io_engine::config::endpoint::kEventHandler, &event_handler)
            	(adk::io_engine::config::endpoint::kAcceptHandler, &accept_handler)
           		(adk::io_engine::config::endpoint::kMessageHandler, &message_handler)
   				(adk::io_engine::config::endpoint::kTcpNoDelay, true)
                ;

	adk::io_engine::Acceptor* acceptor = tcp_engine->Accept(accept_props);
	if (acceptor == nullptr)
	{
		std::cout << "create acceptor failed" << std::endl;
		return -1;
	}

	uint64_t last_receive_count = 0;
	while (1)
	{
		uint64_t temp_count = g_receive_count;
		std::cout << "total receive " << g_receive_count 
				  << " msgs, and receive rate " 
				  << temp_count - last_receive_count << std::endl;
		last_receive_count = temp_count;
    	
		sleep(1);
	}

	adk::io_engine::TcpEngine::Destroy(tcp_engine);
	return 0;
}

