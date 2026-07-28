#ifndef ADK_IMPL_EXCEPTION_H_
#define ADK_IMPL_EXCEPTION_H_

#include <stdexcept>

#include <boost/format.hpp>

#define ADK_STRINGIZE_(a) #a
#define ADK_STRINGIZE(a) ADK_STRINGIZE_(a)

#define ADK_THROW(desc) throw std::runtime_error((boost::format(ADK_STRINGIZE(__FILE__) ", %1%, line " ADK_STRINGIZE(__LINE__) ", %2%") % __FUNCTION__ % (desc)).str());

#endif // ADK_EXCEPTION_H_
