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
        AddOptionWithAcceptor("context-name",
                              "",
                              std::string(),
                              context_name_);
    }
    // =======================================================================================
    
    // configure aaf
    void OnConfigureFramework(ami::Property& fw_props)
    {
        fw_props.SetValue(config::kEnableHighAvailableContext, true);
        fw_props.SetValue(config::kEnableAppNameCheck, false);
    }

    virtual void OnMessageSingleton(ami::Message*)
    {
        std::cout << "OnMessageSingleton Bug On!" << std::endl;
    }

    virtual void OnMessage(ami::Message*)
    {
        return;
    }

    // =======================================================================================
    virtual void OnRoleChangeToLeader()
    {
        std::cout << "role change to leader" << std::endl;
    }

    virtual void OnMemberLost(const std::vector<std::string>& lost_members)
    {
        std::cout << "lost members : " << adk::GetElementList(lost_members) << std::endl;
    }

    virtual std::string MakeHighAvailableContextName()
    {
        return context_name_;
    }

    // =======================================================================================
    virtual int32_t OnRun()
    {
        return ErrorCode::kPassed;
    }

private:
    EndpointHandler* ep_hdl_;
    uint32_t         counter_;
    bool             is_throw_;
    std::string      context_name_;
} g_ami_app;

ADK_LOG_DEFINE(AmiApp);
