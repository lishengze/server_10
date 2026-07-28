#include <assert.h>

#include <iostream>
#include <stdexcept>

#include <aaf.h>
#include <adk/util.h>

using namespace aaf;

class AppMsgHdl : public ami::MessageHandler
{
public:
	virtual ~AppMsgHdl() {}

	virtual void OnMessage(ami::Message*)
	{
		std::cout << "app message handler" << std::endl;
	}
};

#define CONDITION_THROW(point_name) do {	\
	if (point_name == except_point_)	\
		throw std::runtime_error("test exception");	\
} while (false)

class AmiApp : public GenericAmiApplication 
{
	ADK_LOG_DECLARE_AC(300000);

public:
	AmiApp()
	{
		ep_hdl_ = NULL;
		counter_ = 0;
		
		is_throw_ = false;
		enable_sg_ = false;
		override_make_ctx_ = false;
		rate_ = 12;
	}

	~AmiApp()
	{}

	// configure program options
	virtual void SetAmiAppOption()
	{
		AddOption("throw-exception", "throw exception inside OnInit");

		AddOptionWithArgument<uint32_t>("argument-v1", "to add option with no value ");

		AddOptionWithArgument("argument-v2", "to add option with default value 1", int32_t(1));

		AddOptionWithAcceptor("enable-sg-context", "enable singleton-context", enable_sg_);
		
		AddOptionWithAcceptor("enable-ha-context", "enable ha-context", enable_ha_);

		AddOptionWithCallback("callback-v1",
							  "to add option with no value, using callback to process",
							  &AmiApp::OnOptionCallbackNoArg,this);

		AddOptionWithCallback("except-point",
							  "test app throw exception point",
							  std::string("no_exception"),
							  &AmiApp::OnOptionCallback, this);		
		
		AAF_ADDOPT_ACCEPTOR_NARG("override-make-context",
							  "override the MakeSingletonContextName method", 
							  override_make_ctx_);
		
		AAF_ADDOPT_ACCEPTOR("total-messages", 
							"the number of messages to send", 
							int64_t(24),
							total_messages_);
		
		AAF_ADDOPT_CALLBACK_NARG("callback-v2",
							  	 "to add option with no value, using callback to process",
							     &AmiApp::OnOptionCallbackNoArg);
		
		AAF_ADDOPT_CALLBACK("tx-endpoint", 
							"the endpoint to SendMsg",
							std::string("ForTest"),
							&AmiApp::OnOptionCallback);

		if (std::getenv("AAF_TEST_TRY_CATCH") != NULL)
			throw std::runtime_error("test exception");
	}

	// parse program options
	virtual void OnAmiAppOption(const std::string& option_name)
	{
		CONDITION_THROW("OnAmiAppOption");
		if (option_name == "throw-exception")
		{
			is_throw_ = true;
			std::cout<<"throw-exception = "<< is_throw_ << std::endl;
		}
		if (option_name == "example-v1")
		{
			std::cout << "example-v1 without arg" << std::endl;
		}	
		if (option_name == "example-v2")
		{
			// option --Example is set
			int32_t opt_val = GetOptionArgument<int32_t>(option_name);
			std::cout << "example-v2 = "<< opt_val << std::endl;
		}		
	}

	int32_t OnOptionCallback(const std::string& option_name, const std::string& arg)
	{
		if(option_name == "except-point")
		{
			except_point_ = arg;
			std::cout << "except_point = " << except_point_ << std::endl;
		}
		if(option_name == "tx-endpoint")
		{
			txep_name_ = arg;
			std::cout << "tx-endpoint = " << txep_name_ << std::endl;
		}
		
		return aaf::ErrorCode::kSuccess;
	}

	int32_t OnOptionCallbackNoArg(const std::string& option_name)
	{
		assert(option_name == "callback-v1");
		std::cout << "callback-v1 without arg " << std::endl;
		return aaf::ErrorCode::kSuccess;
	}
	// =======================================================================================
	
	// configure aaf
	void OnConfigureFramework(ami::Property& fw_props)
	{
		CONDITION_THROW("OnConfigureFramework");
		fw_props.SetValue(config::kEnableSingletonContext, enable_sg_);
		fw_props.SetValue(config::kEnableHighAvailableContext, enable_ha_);
		fw_props.SetValue(config::kEnableAppNameCheck, false);
		// fw_props.SetValue(config::kEnableMonitorRequest, true);
	}

	void OnRoleChangeToLeader()
	{
		std::cout << "role change to leader" << std::endl;
		CONDITION_THROW("OnRoleChangeToLeader");
	}

	void OnMemberLost(const std::vector<std::string>& lost_members)
	{
		CONDITION_THROW("OnMemberLost");
	}

	// =======================================================================================

	// Init application data	
	virtual int32_t OnAmiInitBegin()
	{
		CONDITION_THROW("OnAmiInitBegin");
		std::cout<< "=================== ami init begin ===================" << std::endl;
		if (is_throw_)
		{
			throw std::exception();
		}

		std::cout << "enable-sg-context = " << enable_sg_ << std::endl;
		std::cout << "enable-ha-context = " << enable_ha_ << std::endl;
		std::cout << "override_make_ctx = " << override_make_ctx_ << std::endl;
		std::cout << "total_messages = " << total_messages_ << std::endl;

		rate_ctrl_ = new adk::SimpleRateController<>(rate_);

		return aaf::ErrorCode::kSuccess;
	}
	
	virtual std::string MakeHighAvailableContextName()
	{
		CONDITION_THROW("MakeHighAvailableContextName");
		return GetApplicationName();
	}
	
	virtual std::string MakeSingletonContextName()
	{
		CONDITION_THROW("MakeSingletonContextName");
		if (override_make_ctx_)
			return GetApplicationName() + "_S";
		else
			return GenericAmiApplication::MakeSingletonContextName();
	}
	// =======================================================================================

	// before create TxEndpoint
	virtual int32_t OnTxEndpointCreationBegin()
	{

		std::cout << "show tx transports info >>>>>" << std::endl;
		auto& transport_id_set =  GetTxStreamIDs();
		for (auto tp_id : transport_id_set)
		{
			auto* tp_info = GetTransportInfo(tp_id);
			assert(tp_info != NULL);
			auto& transport_info = *tp_info;
			std::cout << "	transport_info.transport_id = " << transport_info.transport_id << ", " << std::endl;
			std::cout << "	transport_info.tier_name = " << transport_info.tier_name << ", " << std::endl;
			std::cout << "	transport_info.endpoint_name = " << transport_info.endpoint_name << ", " << std::endl;
			std::cout << "	transport_info.endpoint_id = " << transport_info.endpoint_id << ", " << std::endl;
			std::cout << "	transport_info.transport_partition = " << transport_info.transport_partition << ", " << std::endl;
			std::cout << "	transport_info.transport_name = " << transport_info.transport_name << ", " << std::endl;
			std::cout << "	transport_info.transport_direction = " << transport_info.transport_direction << std::endl;
		}

		std::cout << "show rx transports info >>>>>" << std::endl;
		auto& rx_transport_id_set = GetRxStreamIDs();	
		for (auto tp_id : rx_transport_id_set)
		{
			auto* tp_info = GetTransportInfo(tp_id);
			assert(tp_info != NULL);
			auto& transport_info = *tp_info;
			std::cout << "	transport_info.transport_id = " << transport_info.transport_id << ", " << std::endl;
			std::cout << "	transport_info.tier_name = " << transport_info.tier_name << ", " << std::endl;
			std::cout << "	transport_info.endpoint_name = " << transport_info.endpoint_name << ", " << std::endl;
			std::cout << "	transport_info.endpoint_id = " << transport_info.endpoint_id << ", " << std::endl;
			std::cout << "	transport_info.transport_partition = " << transport_info.transport_partition << ", " << std::endl;
			std::cout << "	transport_info.transport_name = " << transport_info.transport_name << ", " << std::endl;
			std::cout << "	transport_info.transport_direction = " << transport_info.transport_direction << std::endl;
		}


		CONDITION_THROW("OnTxEndpointCreationBegin");
		std::cout << "================= before tx endpoints ===============" << std::endl;
		auto& ep_set = GetTxEndpointSet();
		for (auto& ep : ep_set)
		{
			std::cout << "ep = " << ep << std::endl;
		}

		auto& stream_ids = GetTxStreamIDs();
		for (auto id : stream_ids)
		{
			std::cout << "stream_id = " << id << std::endl;
		}
		return ErrorCode::kSuccess;
	}

	// before create RxEndpoint
	virtual int32_t OnRxEndpointCreationBegin()
	{
		CONDITION_THROW("OnRxEndpointCreationBegin");
		std::cout << "================= before rx endpoints ===============" << std::endl;
		auto& ep_set = GetRxEndpointSet();
		for (auto& ep : ep_set)
		{
			std::cout << "ep = " << ep << std::endl;
		}

		auto& stream_ids = GetRxStreamIDs();
		for (auto id : stream_ids)
		{
			std::cout << "id = " << id << std::endl;
		}
		return ErrorCode::kSuccess;
	}

	// create TxEndpoints
	virtual int32_t OnTxEndpointCreation(EndpointHandler* ep_hdl, const std::string& ep_name)
	{
		CONDITION_THROW("OnTxEndpointCreation");
		std::cout << "OnTxEndpointCreation " << ep_name << " >>>>> ";
		if (ep_name == txep_name_)
		{
			ep_hdl_ = ep_hdl;
		}

		std::cout << "tx partitions = ";
		std::vector<int32_t> partitions;
		GetTxEndpointPartitions(ep_name, partitions);
		for (auto partition : partitions)
		{
			std::cout << partition << " , ";
		}
		std::cout << std::endl;
		return ErrorCode::kSuccess;
	}

	// create RxEndpoints
	virtual int32_t OnRxEndpointCreation(const std::string& ep_name, ami::MessageHandler** msg_hdl, bool is_ha_ctx)
	{
		CONDITION_THROW("OnRxEndpointCreation");
		std::cout << "OnTxEndpointCreation " << ep_name << " >>>>> ";
		if (ep_name == "ForTest")
		{
			*msg_hdl = new AppMsgHdl();
		}

		std::cout << "rx partitions = ";
		std::vector<int32_t> partitions;
		GetRxEndpointPartitions(ep_name, partitions);
		for (auto partition : partitions)
		{
			std::cout << partition << " , ";
		}
		std::cout << std::endl;
		return ErrorCode::kSuccess;
	}

	// =======================================================================================
	virtual int32_t OnAmiInitEnd()
	{
		CONDITION_THROW("OnAmiInitEnd");
		std::cout << "=================== ami init end =====================" << std::endl;
		std::cout << "show context info >>>>>" << std::endl;
		auto ha_ctx_id = GetContextId();
		if (ha_ctx_id != aaf::constant::kInvalidContextId)
		{
			std::cout << "	ha context id = " << ha_ctx_id << std::endl;	
		}

		auto singleton_ctx_id = GetSingletonContextId();
		if (singleton_ctx_id != aaf::constant::kInvalidContextId)
		{
			std::cout << "	singleton context id = " << singleton_ctx_id << std::endl;	
		}

		if (enable_ha_)
		{
			EndpointHandler* ep_hdl_test = CreateTxEndpoint(txep_name_);
			assert(ep_hdl_test != NULL);
			assert(ep_hdl_test == ep_hdl_);	
		}

		std::cout << "show tx transports info >>>>>" << std::endl;
		auto& transport_id_set =  GetTxStreamIDs();
		for (auto tp_id : transport_id_set)
		{
			auto* tp_info = GetTransportInfo(tp_id);
			assert(tp_info != NULL);
			auto& transport_info = *tp_info;
			std::cout << "	transport_info.transport_id = " << transport_info.transport_id << ", " << std::endl;
			std::cout << "	transport_info.tier_name = " << transport_info.tier_name << ", " << std::endl;
			std::cout << "	transport_info.endpoint_name = " << transport_info.endpoint_name << ", " << std::endl;
			std::cout << "	transport_info.transport_partition = " << transport_info.transport_partition << ", " << std::endl;
			std::cout << "	transport_info.transport_name = " << transport_info.transport_name << ", " << std::endl;
			std::cout << "	transport_info.transport_direction = " << transport_info.transport_direction << std::endl;
		}

		std::cout << "show rx transports info >>>>>" << std::endl;
		auto& rx_transport_id_set = GetRxStreamIDs();	
		for (auto tp_id : rx_transport_id_set)
		{
			auto* tp_info = GetTransportInfo(tp_id);
			assert(tp_info != NULL);
			auto& transport_info = *tp_info;
			std::cout << "	transport_info.transport_id = " << transport_info.transport_id << ", " << std::endl;
			std::cout << "	transport_info.tier_name = " << transport_info.tier_name << ", " << std::endl;
			std::cout << "	transport_info.endpoint_name = " << transport_info.endpoint_name << ", " << std::endl;
			std::cout << "	transport_info.transport_partition = " << transport_info.transport_partition << ", " << std::endl;
			std::cout << "	transport_info.transport_name = " << transport_info.transport_name << ", " << std::endl;
			std::cout << "	transport_info.transport_direction = " << transport_info.transport_direction << std::endl;
		}

		std::cout << "=================== start on run =====================" << std::endl;
		return aaf::ErrorCode::kSuccess;
	}

	// implement message handler
	virtual void OnMessageSingleton(ami::Message* msg)
	{
		std::cout << "OnMessageSingleton:" << msg->const_data() << std::endl;
	}

	virtual void OnMessage(ami::Message* msg)
	{
		std::cout << "HA OnMessage Bug On!" << msg->const_data() << std::endl;
	}
	// =======================================================================================

	virtual int32_t OnRun()
	{
		CONDITION_THROW("OnRun");
		if (ep_hdl_ == NULL)
			return ErrorCode::kPassed;

		ami::Message* msg = aaf::NewMessage(ep_hdl_, 128);
		static int a = 0;
		if (a == 0)
		{msg->append("hello", strlen("hello")); a = 1;}
		else if (a == 1)
		{msg->append("welcome to Archforce", strlen("welcome to Archforce")); a = 2; }
		else if (a == 2)
		{msg->append("thanks to using AAF framework", strlen("thanks to using AAF framework")); a = 0; }
		ep_hdl_->SendMsg(msg);
		if (++counter_ == total_messages_)
		{
			GenericAmiApplication::StopAmiApp();
		}
		
		rate_ctrl_->Wait();
		
		if (counter_ % rate_ == 0)
		{
			ADK_LOG_INFO_AC_TF("OnRun", "counter = {1}", counter_);
		}
		
		return ErrorCode::kPassed;
	}

	virtual void OnIdle() 
	{
		CONDITION_THROW("OnIdle");
		::usleep(1);
	};

	void OnAmiExitBegin()
	{
		CONDITION_THROW("OnAmiExitBegin");
		std::cout << "=================== ami exit begin ====================" << std::endl;
	}

	virtual void OnAmiRxExitEnd()
	{
		CONDITION_THROW("OnAmiRxExitEnd");
		std::cout << "================= rx endpoints exit end ==============" << std::endl;
		if (is_throw_)
		{
			std::cerr << "OnAmiRxExitEnd is called after throwing exception" << std::endl;
			throw std::exception();
		}
	}

	void OnAmiExitEnd()
	{
		CONDITION_THROW("OnAmiExitEnd");
		std::cout << "=================== ami exit end =====================" << std::endl;
	}

private:
	EndpointHandler* ep_hdl_;
	uint32_t		 counter_;
	bool			 is_throw_;				///< AddOption
	bool 			 enable_ha_;			///< AddOptionWithAcceptor
	bool 			 enable_sg_;			///< AddOptionWithAcceptor
	std::string 	 except_point_;			///< AddOptionWithCallback
	std::string 	 txep_name_;			///< AAF_ADDOPT_CALLBACK
	bool 			 override_make_ctx_;	///< AAF_ADDOPT_ACCEPTOR_NARG
	int64_t 		 total_messages_;		///< AAF_ADDOPT_ACCEPTOR
	int32_t 		 rate_;
	adk::SimpleRateController<>* rate_ctrl_;
} g_ami_app;

ADK_LOG_DEFINE(AmiApp);