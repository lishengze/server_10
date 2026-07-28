#include <assert.h>

#include <iostream>
#include <stdexcept>

#include <aaf.h>

using namespace aaf;

class AmiApp : public GenericAmiApplication 
{
public:
    AmiApp()
    {
        ep_hdl_ = NULL;
        counter_ = 0;
        is_throw_ = false;
    }

    ~AmiApp()
    {}

    // configure program options
    virtual void SetAmiAppOption()
    {
        AddOption("throw-exception", "throw exception inside OnInit");
        AddOptionWithArgument("example", "to add option with default value 1", int32_t(1));

        AAF_ADDOPT_CALLBACK("callback", "to add option with default value 1, using callback to process args", int32_t(1), &AmiApp::OnOptionCallback);
        AAF_ADDOPT_CALLBACK_NARG("callback2", "to add option with default value 1, using callback to process", &AmiApp::OnOptionCallbackNoArg);
    }

    // parse program options
    virtual void OnAmiAppOption(const std::string& option_name)
    {
        if (option_name == "example")
        {
            // option --Example is set
            int32_t opt_val = GetOptionArgument<uint32_t>(option_name);
            (void) opt_val;
        }
        else if (option_name == "throw-exception")
        {
            is_throw_ = true;
        }
    }

    int32_t OnOptionCallback(const std::string& option_name, const int32_t& arg)
    {
        assert(option_name == "callback");
        std::cout << "--callback arg = " << arg << std::endl;
        return aaf::ErrorCode::kSuccess;
    }

    int32_t OnOptionCallbackNoArg(const std::string& option_name)
    {
        assert(option_name == "callback2");
        std::cout << "--callback2 no arg " << std::endl;
        return aaf::ErrorCode::kSuccess;
    }
    // =======================================================================================
    
    // configure aaf
    void OnConfigureFramework(ami::Property& fw_props)
    {
        fw_props.SetValue(config::kEnableSingletonContext, true);
    }

    virtual void OnMessageSingleton(ami::Message*)
    {
        std::cout << "OnMessageSingleton" << std::endl;
    }

    virtual void OnMessage(ami::Message*)
    {
        std::cout << "HA OnMessage Bug On!" << std::endl;
    }
    // =======================================================================================

    // Init application data    
    virtual int32_t OnAmiInitBegin()
    {
        if (is_throw_)
        {
            throw std::exception();
        }

        return aaf::ErrorCode::kSuccess;
    }
    // =======================================================================================

    // create TxEndpoints
    virtual int32_t OnTxEndpointCreation(EndpointHandler* ep_hdl, const std::string& ep_name)
    {
        if (ep_name == "ForTest")
        {
            ep_hdl_ = ep_hdl;
        }
        return ErrorCode::kSuccess;
    }
    // =======================================================================================

    virtual int32_t OnRun()
    {
        ami::Message* msg = aaf::NewMessage(ep_hdl_, 128);
        msg->append("hello world", strlen("hello world"));
        ep_hdl_->SendMsg(msg);
        if (++counter_ == 6)
        {
            GenericAmiApplication::StopAmiApp();
        }
        return ErrorCode::kPassed;
    }

    virtual void OnAmiRxExitEnd()
    {
        if (is_throw_)
        {
            std::cerr << "OnAmiRxExitEnd is called after throwing exception" << std::endl;
            throw std::exception();
        }
    }

private:
    EndpointHandler* ep_hdl_;
    uint32_t         counter_;
    bool             is_throw_;
} g_ami_app;
