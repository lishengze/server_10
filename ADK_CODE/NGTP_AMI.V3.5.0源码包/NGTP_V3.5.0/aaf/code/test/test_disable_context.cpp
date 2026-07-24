#include <iostream>
#include <aaf.h>

using namespace aaf;
	
class AppTest : public GenericAmiApplication 
{
public:
	AppTest()
	{}

	~AppTest()
	{}

	// 注意： 以下接口的定义顺序，即为框架的初始化顺序和停止顺序
	
	virtual void SetAmiAppOption()
	{
		AAF_ADDOPT_ACCEPTOR_NARG("enable-ha-context", "enable ha-context", enable_ha_);
		AAF_ADDOPT_ACCEPTOR_NARG("enable-sg-context", "enable singleton-context", enable_sg_);
		AAF_ADDOPT_ACCEPTOR_NARG("disable-context", "disable all context", disable_ctx_);
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

		// 请求框架不创建Context
		fw_props.SetValue(config::kIsDisableContext, disable_ctx_);
	}
	
	virtual int32_t OnAmiInitBegin()
	{
		std::cout << "OnAmiInitBegin" << std::endl;
		return aaf::ErrorCode::kSuccess;
	}
	
	virtual int32_t OnTxEndpointCreationBegin()
	{
		std::cout << "OnTxEndpointCreationBegin" << std::endl;
		return aaf::ErrorCode::kSuccess;
	}
	
	virtual int32_t OnTxEndpointCreation(EndpointHandler* ep_hdl, const std::string& ep_name)
	{
		// 框架不会创建Context，因此该回调不会被调用
		std::cout << "OnTxEndpointCreation" << std::endl;
		ep_hdl_ = ep_hdl;
		return aaf::ErrorCode::kSuccess;
	}

	virtual int32_t OnRxEndpointCreationBegin()
	{
		std::cout << "OnRxEndpointCreationBegin" << std::endl;
		return aaf::ErrorCode::kSuccess;
	}

	virtual int32_t OnRxEndpointCreation(const std::string& ep_name, ami::MessageHandler** msg_hdl, bool is_ha_ctx)
	{
		// 框架不会创建Context，因此该回调不会被调用
		std::cout << "OnRxEndpointCreation" << std::endl;
		return aaf::ErrorCode::kSuccess;
	}

	virtual int32_t OnAmiInitEnd()
	{
		std::cout << "OnAmiInitEnd" << std::endl;
		return aaf::ErrorCode::kSuccess;
	}

	virtual int32_t OnRun()
	{

		std::cout << "OnRun" << std::endl;
		sleep(1);		
		return ErrorCode::kPassed;
	}

	virtual void OnIdle() 
	{
		// 每发送一条消息，睡眠1秒
		std::cout << "OnIdle" << std::endl;
		::usleep(1000000);
	}

	virtual void OnAmiExitBegin()
	{

		std::cout << "OnAmiExitBegin" << std::endl;
	}

	virtual void OnAmiRxExitEnd()
	{
		std::cout << "OnAmiRxExitEnd" << std::endl;
	}

	virtual void OnAmiExitEnd()
	{

		std::cout << "OnAmiExitEnd" << std::endl;
	}

	virtual void OnMessageSingleton(ami::Message* msg)
	{
		// 框架不会创建Context，因此该回调不会被调用
		std::cout << "singleton context: " << msg->const_data() << std::endl;
	}

	virtual void OnMessage(ami::Message* msg)
	{
		// 框架不会创建Context，因此该回调不会被调用
		std::cout << "high available context: " << msg->const_data() << std::endl;
	}
	
private:
	EndpointHandler* ep_hdl_ = nullptr;
	uint32_t		 counter_ = 0;
	uint32_t 		 total_messages_ = 10;
	bool 			 enable_ha_ = true;		//TODO  将enable_ha_初始化为true,
	bool 			 enable_sg_ = false;
	bool 			 disable_ctx_ = false;
} g_ami_app;	// 注意需要将派生类实例化，并且仅实例化一次，应用程序才可以正常运行。
