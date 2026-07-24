#include <adk/i18n.h>
#include <adk_pack/i18n.h>

namespace adk
{

namespace impl
{

std::locale& local_impl()
{
    return *adk_impl::get_locale();
}

}


}