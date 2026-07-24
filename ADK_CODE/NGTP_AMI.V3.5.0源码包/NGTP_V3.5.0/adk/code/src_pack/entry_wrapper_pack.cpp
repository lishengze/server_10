#include <adk/entry_wrapper.h>
#include <adk_pack/entry_wrapper.h>
#include <sys/prctl.h>

namespace adk {

std::thread std_thread(const char* short_name, const char* full_name, std::function<void()> f)
{
    return adk_impl::std_thread(short_name, full_name, f);
}

boost::thread boost_thread(const char* short_name, const char* full_name, std::function<void()> f)
{
    return adk_impl::boost_thread(short_name, full_name, f);
}

}