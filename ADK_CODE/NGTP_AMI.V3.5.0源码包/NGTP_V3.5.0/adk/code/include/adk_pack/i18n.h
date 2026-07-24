/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_I18N_H_
#define ADK_I18N_H_

#include <locale>
#include <boost/locale.hpp>

namespace adk
{

namespace impl
{
std::locale& local_impl();
}

#ifndef ADK_TRANSLATE
#define ADK_TRANSLATE(x) (boost::locale::translate(x).str(adk::impl::local_impl()))
#endif

} // namespace adk

#endif /* AGW_I18N_H_ */
