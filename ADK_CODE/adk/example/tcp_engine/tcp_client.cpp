#include <iostream>
#include <unistd.h>
#include <adk/io_engine.h>
#include <adk/error_code.h>

//定义简单实例消息格式
struct MessageFormat
{
    uint32_t msg_size;
    char     msg_body[];
};

//实现事件相关句柄
class ClientHandler final : public adk::io_engine::EventHandler,
                            public adk::io_engine::ConnectHandler
{
	//当连接出现如下网络错误，断开，连接失败，心跳超时，异步关闭时会出现该接口的调用
    void OnEvent(adk::io_engine::Endpoint* endpoint, adk::io_engine::Event* event) override
    {
        if (event->level() > adk::io_engine::EventLevel::kWarn)
        {
            endpoint->Close();
        }
        std::cout << "OnEvent: level <" << event->level()
                  << "> | what <" << event->what() << ">" << std::endl;
    }

	//异步连接成功后会进行该接口的调用
    void OnConnect(adk::io_engine::Endpoint* endpoint, adk::io_engine::Property& ep_props) override
    {
        std::cout << "OnConnect: <" << endpoint->remote_ip()
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
		//打印来自服务器的消息内容
        std::cout << ((MessageFormat*)message->const_data())->msg_body << std::endl;
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
    ClientHandler client_handler;
    auto tcp_engine = adk::io_engine::TcpEngine::Create(
        adk::io_engine::Property()
        (adk::io_engine::config::kEventHandler, static_cast<adk::io_engine::EventHandler*>(&client_handler))
        (adk::io_engine::config::kConnectHandler, static_cast<adk::io_engine::ConnectHandler*>(&client_handler))
    );
    if (nullptr == tcp_engine)
    {
        std::cout << "Create tcp engine failed" << std::endl;
        return -1;
    }

	//建立一个客户端并发起连接(127.0.0.1:20000)
    MessageHandler message_handler;
    auto client_endpoint = tcp_engine->Connect(
        adk::io_engine::Property()
        (adk::io_engine::config::endpoint::kRemoteIp, "127.0.0.1")
        (adk::io_engine::config::endpoint::kRemotePort, 20000)
        (adk::io_engine::config::endpoint::kMessageHandler, static_cast<adk::io_engine::MessageHandler*>(&message_handler))
        (adk::io_engine::config::endpoint::kDecodeTemplate, static_cast<adk::io_engine::DecodeTemplate*>(&message_handler))
    );
    if (nullptr == client_endpoint)
    {
        std::cout << "Asynconnect failed, error info = " << tcp_engine->GetLastError() << std::endl;
        return -1;
    }

	//构建发给服务器的消息"Hello Server"
    char response[128];
    char* const temp_buffer = response;
    strcpy(((MessageFormat*)temp_buffer)->msg_body, "Hello Server");
    ((MessageFormat*)temp_buffer)->msg_size = sizeof(MessageFormat) + 13;
    
	//循环给服务器发送消息并收集打印指标信息
	std::string indicator;
    for (uint32_t index = 0; index < 100; ++index)
    {
        //发送消息失败可能是由于对端关闭，触发OnEvent回调并在回调中调用了endpoint->Close()
        if (adk::ErrorCode::kSuccess != client_endpoint->SendMsg(temp_buffer, ((MessageFormat*)temp_buffer)->msg_size))
        {
            std::cout << "SendMsg failed" << std::endl;
            break;
        }

        sleep(6);
        tcp_engine->CollectIndicator(indicator);
        std::cout << indicator << std::endl;
    }

	//测试完成 销毁tcp engine对象
    adk::io_engine::TcpEngine::Destroy(tcp_engine);
    return 0;
}