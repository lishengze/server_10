// user interface ============================================================================================
#include <string>    
#include <iostream>
#include <functional>
#include "entrypoint.h"

const char * model_version = "1.0" ;   // 尽量使用 const char*;使用string 出现OnDlopen和全局变量构造
const char * mode_name = "share_account" ;
using namespace adk::plugin ;

ADK_PLUGIN_IMPL_ENTRYPOINT_INTERFACE(Share,Check)
{
public:
    void operator()(int integer_arg,double double_arg) override
    {
        ADK_PLUGIN_LOG(PluginLogLevel::kInfo,"Hello this is share_account :share's operator");
        return  ;
    }
} ;


ADK_PLUGIN_IMPL_ENTRYPOINT_INTERFACE(Account,Filter)
{
public:
    ABC operator()(int integer_arg,double double_arg) override
    {
        ADK_PLUGIN_LOG(PluginLogLevel::kInfo,"Hello this is share_account : account's operator");
        ABC ret ;
        return ret;
    }
} ;

bool Verify(const std::string& app_version)
{
    if(app_version.compare(model_version) != 0)
    {
        return false;
    }

    return true ;
}


bool ModuleInit(PluginModule* obj) 
{
    if(!obj->VerifyVersion(Verify))
    {
        std::cout<<"version is failed."<<std::endl;
        return false ;
    }
    obj->AddEntryPointInterface<Share,Account>();

    return true ;
}   

void ModuleFinish(PluginModule* obj)
{
    // release resource you have apply in the plugins
}

ABF_INIT void OnDlopen() // so加载将立即执行此函数
{
    PluginModule* module = new PluginModule;
    module->SetModuleName(mode_name);
    module->SetModuleVersion(model_version) ;
    module->SetModuleInitFunc(ModuleInit) ;
    module->SetModuleFinishFunc(ModuleFinish) ;

    ADK_PLUGIN_REGISTER_MODULE(module) ;
    return ;
}

   
