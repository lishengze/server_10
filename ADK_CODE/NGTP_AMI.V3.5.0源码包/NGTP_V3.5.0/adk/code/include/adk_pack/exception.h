/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_EXCEPTION_H_
#define ADK_EXCEPTION_H_

#include <stdexcept>

#include <boost/format.hpp>

#define ADK_STRINGIZE_(a) #a
#define ADK_STRINGIZE(a) ADK_STRINGIZE_(a)

#define ADK_THROW(desc) \
 throw std::runtime_error((boost::format(ADK_STRINGIZE(__FILE__) ", %1%, line " ADK_STRINGIZE(__LINE__) ", %2%") % __FUNCTION__ % (desc)).str());

#endif // ADK_LIB_EXCEPTION_H_