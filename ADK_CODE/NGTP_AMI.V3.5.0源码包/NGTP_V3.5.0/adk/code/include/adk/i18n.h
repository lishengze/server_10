#ifndef ADK_IMPL_I18N_H_
#define ADK_IMPL_I18N_H_

#include "libadk.h"
#include <locale>
#include <boost/locale.hpp>

namespace adk_impl
{

ADK_API std::locale* get_locale();

#define ADK_TRANSLATE(x) (boost::locale::translate(x).str(*adk_impl::get_locale()))

} // namespace adk_impl

#endif /* AGW_I18N_H_ */
