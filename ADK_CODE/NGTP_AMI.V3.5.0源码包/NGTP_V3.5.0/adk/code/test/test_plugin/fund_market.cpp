#include <string>    
#include <iostream>
#include "entrypoint.h"

using namespace adk::plugin ;

const char *mode_name = "fund_market" ;
const char *model_version = "1.0" ;
    
ADK_PLUGIN_IMPL_ENTRYPOINT_INTERFACE(Fund,Check)
{
public:
    void operator()(int integer_arg,double double_arg) override
    {
       ADK_PLUGIN_LOG(PluginLogLevel::kInfo,"Hello this is fund_market:fund's operator");
        return ;
    }

    virtual std::string interface_name() { return "Fund_Check"; }
    virtual std::string desc() { return "this is a implementation Fund_Check  of Check"; }
} ;


ADK_PLUGIN_IMPL_ENTRYPOINT_INTERFACE(Market,Filter2)
{
public:
    ABC operator()(int integer_arg,double double_arg) override
    {
        ADK_PLUGIN_LOG(PluginLogLevel::kInfo,"Hello this is fund_market:market's operator") ;
        ABC ret ;
        return ret ;
    }
    virtual std::string interface_name() { return "market_Filter"; }
    virtual std::string desc() { return "this is a implementation fund_market of Filter"; }
} ;


bool Verify(const std::string& app_version)
{
    if(app_version.compare(model_version) != 0)
    {
        return false;
    }

    return true ;
}

void ModuleFinish(PluginModule* obj)
{
    // release the resources which you have applied.
}

bool ModuleInit(PluginModule* obj) 
{
    if(!obj->VerifyVersion(std::bind(&Verify,std::placeholders::_1)))
    {
        std::cout<<"version is failed."<<std::endl;
        return false ;
    }

    return obj->AddEntryPointInterface<Fund, Market>();
}   

ABF_INIT void OnDlopen()
{
    PluginModule* module = new PluginModule;
    module->SetModuleName(mode_name);
    module->SetModuleVersion(model_version) ;
    module->SetModuleInitFunc(ModuleInit) ;
    module->SetModuleFinishFunc(ModuleFinish) ;

    ADK_PLUGIN_REGISTER_MODULE(module) ;

    return ;
}
