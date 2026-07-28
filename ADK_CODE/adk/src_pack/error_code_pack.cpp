#include <adk/error_code.h>
#include <adk_pack/error_code.h>

namespace adk
{

const std::string& GetErrorDesc(ErrorCode ec)
{
    return adk_impl::GetErrorDesc((adk_impl::ErrorCode)ec);
}

}