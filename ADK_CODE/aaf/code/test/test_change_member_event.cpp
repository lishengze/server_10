#include <assert.h>

#include <iostream>
#include <stdexcept>

#include <aaf.h>
#include <adk/util.h>

using namespace aaf;

class AmiApp : public GenericAmiApplication 
{
    ADK_LOG_DECLARE_AC(400000);

public:
    AmiApp()
    {}

    ~AmiApp()
    {}

    // configure program options
    virtual void SetAmiAppOption()
    {
        AAF_ADDOPT_ACCEPTOR_NARG("enable-ha-context", "enable ha-context", enable_ha_);
        AAF_ADDOPT_ACCEPTOR_NARG("enable-sg-context", "enable singleton-context", enable_sg_);
        // AddOptionWithAcceptor("context-name", "", std::string(), context_name_);
    }
    // =======================================================================================
    
    // configure aaf
    void OnConfigureFramework(ami::Property& fw_props)
    {
        fw_props.SetValue(config::kEnableHighAvailableContext, enable_ha_);
        fw_props.SetValue(config::kEnableSingletonContext, enable_sg_);
        fw_props.SetValue(config::kEnableAppNameCheck, false);
    }

    virtual void OnMessageSingleton(ami::Message*)
    {
        std::cout << "OnMessageSingleton Bug On!" << std::endl;
    }

    virtual void OnMessage(ami::Message*)
    {
        std::cout << "OnMessage Bug On!" << std::endl;
        return;
    }

    // =======================================================================================
    virtual void OnRoleChangeToLeader()
    {
        std::cout << "############ role change to leader" << std::endl;
    }

    virtual void OnRoleChangeToMember()
    {
        std::cout << "############ role change to member" << std::endl;
    }

    // =======================================================================================
    virtual int32_t OnRun()
    {
        return ErrorCode::kPassed;
    }

private:
    // std::string      context_name_;
    bool             enable_ha_ = false;    
    bool             enable_sg_ = false;
} g_ami_app;

ADK_LOG_DEFINE(AmiApp);
