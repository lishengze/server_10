

#include <adk/plugin.h>

#define version "1.1"

using namespace adk::plugin ;

DEFINE_PLUGIN_ENTRYPOINT(version,Check,int,double) ;
DEFINE_PLUGIN_ENTRYPOINT(version,Filter,int,double) ;

