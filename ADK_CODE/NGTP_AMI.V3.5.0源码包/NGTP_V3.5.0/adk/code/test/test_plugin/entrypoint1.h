#include <adk/plugin.h>

#define version "2.0"

using namespace adk::plugin ;
struct ABC
{
	operator bool()
	{
		return true ;
	}
};

//ADK_PLUGIN_DEFINE_ENTRYPOINT(version,Check,void,int,double) ;
ADK_PLUGIN_DEFINE_ENTRYPOINT_RETURN_VOID(version,Check,int,double) ;
ADK_PLUGIN_DEFINE_ENTRYPOINT(version,Filter2,ABC,int,double) ;
ADK_PLUGIN_DEFINE_ENTRYPOINT(version,Filter1,int,int,double) ;