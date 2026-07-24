#include <iostream>
#include <aaf.h>

using namespace aaf;
	
class AAFDemo : public GenericAmiApplication 
{
public:
	AAFDemo()
	{}

	~AAFDemo()
	{}

	// 注意： 以下接口的定义顺序，即为框架的初始化顺序和停止顺序
	
	virtual void SetAmiAppOption()
	{
		AAF_ADDOPT_ACCEPTOR_NARG("enable-ha-context", "enable ha-context", enable_ha_);
		AAF_ADDOPT_ACCEPTOR_NARG("enable-sg-context", "enable singleton-context", enable_sg_);
		AAF_ADDOPT_ACCEPTOR("total-messages", "the total messages to send", total_messages_, total_messages_);
	}

	virtual void OnConfigureFramework(ami::Property& fw_props)
	{
		// 请求框架创建单例（非高可用）的AMI Context，这里作为演示从命令行读取
		// 实际开发应用时，也可以始终配置为true，或者从配置读取
		// 单例Context为 "应用名_Sinelgon"，应用名通过-n选项指定
		fw_props.SetValue(config::kEnableSingletonContext, enable_sg_);
		
		// 请求框架创建高可用的AMI Context，这里作为演示从命令行读取
		// 实际开发应用时，也可以始终配置为true，或者从配置读取
		// 高可用Context为 "应用名"，应用名通过-n选项指定
		fw_props.SetValue(config::kEnableHighAvailableContext, enable_ha_);

		// 请求框架不检查应用名规范 “[A-Za-z]+_[0-9]+_[0-9]+_[0-9]”
		fw_props.SetValue(config::kEnableAppNameCheck, false);
	}
	
	virtual int32_t OnAmiInitBegin()
	{
		// TODO: AMI初始化之前，框架会回调该接口，
		//       该接口返回后框架会开始初始化AMI
		return aaf::ErrorCode::kSuccess;
	}
	
	virtual int32_t OnTxEndpointCreationBegin()
	{
		//TODO: 在创建TxEndpoint之前，框架会回调该接口
		//      该接口返回后框架会开始创建TxEndpoint
		return aaf::ErrorCode::kSuccess;
	}
	
	virtual int32_t OnTxEndpointCreation(EndpointHandler* ep_hdl, const std::string& ep_name)
	{
		//TODO: 框架每创建一个TxEndpoint便会回调该接口
		//      ep_hdl 为框架创建的TxEndpoint句柄，可用于发送消息
		//      ep_name 为对应的TxEndpoint名称
		//      应用需要在此接口返回之前，将TxEndpoint和应用相关的数据结构或者应用线程进行关联
		ep_hdl_ = ep_hdl;
		return aaf::ErrorCode::kSuccess;
	}

	virtual int32_t OnRxEndpointCreationBegin()
	{
		//TODO: 在创建RxEndpoint之前，框架会回调该接口
		//      该接口返回后框架会开始创建RxEndpoint
		//      应用需要在此接口返回之前，准备好处理AMI消息所需要的数据结构和资源
		return aaf::ErrorCode::kSuccess;
	}

	virtual int32_t OnRxEndpointCreation(const std::string& ep_name, ami::MessageHandler** msg_hdl, bool is_ha_ctx)
	{
		//TODO: 在创建RxEndpoint之前，框架会回调该接口
		//      ep_name 为对应的RxEndpoint名称
		//      msg_hdl 为输出参数可以为RxEndpoint指定特有的消息处理句柄
		//      		消息处理句柄需要派生自ami::MessageHandler
		//      该接口返回后，框架会开始创建名称为ep_name的RxEndpoint
		return aaf::ErrorCode::kSuccess;
	}

	virtual int32_t OnAmiInitEnd()
	{
		//TODO: 框架完成AMI的初始化之后会回调该接口
		//		该接口返回后，框架会在OnRun和OnIdle之间循环
		return aaf::ErrorCode::kSuccess;
	}

	virtual int32_t OnRun()
	{
		// 从 ep_hdl_ 上申请一个新的消息
		ami::Message* msg = aaf::NewMessage(ep_hdl_, 128);

		// 填充应用数据
		msg->append("welcome to using AAF framework", 
					strlen("welcome to using AAF framework")); 

		// 发送消息
		ep_hdl_->SendMsg(msg);

		if (++counter_ == total_messages_)
		{
			// 发送完所有消息，通知框架退出
			GenericAmiApplication::StopAmiApp();
		}
		
		// 该接口返回后，框架会调用OnIdle
		// 若框架不需要退出，则会持续调用OnRun和OnIdle
		return ErrorCode::kPassed;
	}

	virtual void OnIdle() 
	{
		// 每发送一条消息，睡眠1秒
		::usleep(1000000);
	}

	virtual void OnAmiExitBegin()
	{
		//TODO: 框架在开始销毁AMI对象之前，会回调该接口
		//      在此处停止使用TxEndpoint向AMI上发送消息
	}

	virtual void OnAmiRxExitEnd()
	{
		//TODO: 会回调该接口时，框架已不再从AMI接收消息
	}

	virtual void OnAmiExitEnd()
	{
		//TODO: AMI彻底被停止之后，框架会回调该接口
		//      在此处销毁处理AMI消息所需要的应用数据结构
	}

	virtual void OnMessageSingleton(ami::Message* msg)
	{
		// 从单例（非高可用）的Context上收到了一条消息
		std::cout << "singleton context: " << msg->const_data() << std::endl;
	}

	virtual void OnMessage(ami::Message* msg)
	{
		// 从高可用的Context上收到了一条消息
		std::cout << "high available context: " << msg->const_data() << std::endl;
	}
	
private:
	EndpointHandler* ep_hdl_ = nullptr;
	uint32_t		 counter_ = 0;
	uint32_t 		 total_messages_ = 10;
	bool 			 enable_ha_ = false;		//TODO  将enable_ha_初始化为true,
	     			                    		//	    始终创建和应用同名的高可用Context
	bool 			 enable_sg_ = false;
} g_ami_app;	// 注意需要将派生类实例化，并且仅实例化一次，应用程序才可以正常运行。
