#include <iostream>
#include <unistd.h>
#include <adk/io_engine.h>

//定义简单实例消息格式
struct MessageFormat
{
    uint32_t msg_size;
    char     msg_body[];
};

//实现事件相关回调
class ServerHandler final : public adk::io_engine::EventHandler,
                            public adk::io_engine::AcceptHandler
{
public:
	//当连接出现如下网络错误，断开，心跳超时，异步关闭时会出现该接口的调用
    void OnEvent(adk::io_engine::Endpoint* endpoint, adk::io_engine::Event* event) override
    {
        if (event->level() > adk::io_engine::EventLevel::kWarn)
        {
            endpoint->Close();
        }
        std::cout << "OnEvent: level <" << event->level() 
                  << "> | what <" << event->what() << ">" << std::endl;
    }

	//当服务器接受到新连接时会进行该接口的调用
    void OnAccept(adk::io_engine::Endpoint* endpoint, adk::io_engine::Property& ep_props) override
    {
        std::cout << "OnAccept: new connection <" << endpoint->remote_ip() 
                  << ":" << endpoint->remote_port() << ">" << std::endl;
    }
};

//实现消息解析模板和递交相关回调
class MessageHandler final : public adk::io_engine::MessageHandler,
                             public adk::io_engine::DecodeTemplate
{
public:
	// 网络受到消息递交回调
    int32_t OnMessage(adk::io_engine::Message* message) override
    {
		//打印来自客户端的消息内容
        std::cout << ((MessageFormat*)message->const_data())->msg_body << std::endl;

		//构建回复消息 "Hello Client"
        char response[128];
        char* const temp_buffer = response;
        strcpy(((MessageFormat*)temp_buffer)->msg_body, "Hello Client");
        ((MessageFormat*)temp_buffer)->msg_size = sizeof(MessageFormat) + 13;
		
		// 调用message->Reply函数进行消息回复
        message->Reply(temp_buffer, ((MessageFormat*)temp_buffer)->msg_size);
        return adk::io_engine::MessageHandler::Result::kSuccess;
    }

	// 实现自定义消息模板，用于框架自动进行消息分界
    int32_t MessageLength(const void* msg_data, uint32_t len) override
    {
        if (len >= sizeof(MessageFormat))
        {
            return ((MessageFormat*)msg_data)->msg_size;
        }
        return -1;
    }
};

int main()
{
	//实例化事件相关句柄，设置相关属性并创建tcp engine对象
    ServerHandler  server_handler;
    auto tcp_engine = adk::io_engine::TcpEngine::Create(
        adk::io_engine::Property()
        (adk::io_engine::config::kEventHandler, static_cast<adk::io_engine::EventHandler*>(&server_handler))
        (adk::io_engine::config::kAcceptHandler, static_cast<adk::io_engine::AcceptHandler*>(&server_handler))
    );
    if (nullptr == tcp_engine)
    {
        std::cout << "Create tcp engine failed" << std::endl;
        return -1;
    }

	//建立一个服务器监听对象 监听的端口为ANY_ADDRESS:20000
    MessageHandler message_handler;
    auto tcp_acceptor = tcp_engine->Accept(
        adk::io_engine::Property()
        (adk::io_engine::config::acceptor::kListenPort, 20000)
        (adk::io_engine::config::acceptor::kMessageHandler, static_cast<adk::io_engine::MessageHandler*>(&message_handler))
        (adk::io_engine::config::acceptor::kDecodeTemplate, static_cast<adk::io_engine::DecodeTemplate*>(&message_handler))
    );
    if (nullptr == tcp_acceptor)
    {
        std::cout << "Create tcp acceptor failed, error info = " << tcp_engine->GetLastError() << std::endl;
        return -1;
    }

	// 收集并打印指标详细信息
    std::string indicator;
    for (uint32_t index = 0; index < 100; ++index)
    {
        sleep(6);
        tcp_engine->CollectIndicator(indicator);
        std::cout << indicator << std::endl;
    }

	//测试完成 销毁tcp engine对象
    adk::io_engine::TcpEngine::Destroy(tcp_engine);
    return 0;
}