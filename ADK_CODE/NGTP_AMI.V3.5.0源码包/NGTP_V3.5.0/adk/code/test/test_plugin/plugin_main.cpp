#include <iostream>
#include <entrypoint.h>

const std::string  app_version 	= "1.0";
const std::string  app_name 	= "DXS_1" ;
const std::string  so_path 		= "/home/luojian/program/adk/code/test/plugin/bin/gcc-4.8.5/debug/threading-multi/"; 
const std::string  config_file 	= "/home/luojian/program/adk/code/test/plugin/plugin.json"; // 

using namespace adk::plugin ;

void Print(int32_t level,const std::string& message)
{
	std::cout<<"level:"<<level<<" msg:"<<message<<std::endl;
}

int main(int argc,char** argv)
{	
	if(argc < 4)
	{
		std::cout<<"please input: ./exe app_name plugins_path plugins_config_file"<<std::endl;
		return 0 ;
	}

	// set the LogHandler
	ADK_PLUGIN_REGISTER_LOG_HANDLER(Print) ;
	
	int32_t init_ret = ADK_PLUGIN_FW_INIT(argv[1],app_version,argv[2],argv[3]) ;
	if(init_ret)
	{
		std::cout<<"Fw_Init is failed."<<GetPluginError(init_ret)<< std::endl;
		return 0 ;
	}

	std::cout<<"Fw_Init is ok..."<<std::endl;

	std::cout<<"exe Check...."<<std::endl;
	ADK_PLUGIN_FOREACH(Check,1,2.0) ;

	std::cout<<"exe Filter2 UNTIL"<<std::endl;
	ADK_PLUGIN_FOREACH_UNTIL(1,Filter2,1,2.0) ;
	std::cout<<"exe Filter UNTIL_FALSE"<<std::endl;
	ADK_PLUGIN_FOREACH_UNTIL_FALSE(Filter,1,2.0) ;
	std::cout<<"exe Filter UNTIL WITH RET"<<std::endl;
	ADK_PLUGIN_FOREACH_UNTIL_WITH_RET(false,ret,Filter,2,4.0) ;

	std::cout<<"ret:"<<ret<<std::endl;

	boost::property_tree::ptree pt ;
	ADK_PLUGIN_STATISTIC(pt) ;
	boost::property_tree::write_json("statistic_info.json",pt) ;

	ADK_PLUGIN_FW_FINISH();

	std::cout<<"exe is finished..."<<std::endl;
	return 0 ;
}