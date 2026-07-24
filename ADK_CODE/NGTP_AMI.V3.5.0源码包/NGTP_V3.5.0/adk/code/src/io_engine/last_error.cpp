#include "last_error.h"

#include <string.h>

namespace adk_impl
{

namespace io_engine
{

constexpr size_t kMaxErrorInfoSize = 256;
thread_local static char s_error_info[kMaxErrorInfoSize] = { 0 };

const char* GetErrorInfo()
{
    return s_error_info;
}

void SetErrorInfo(int error_number)
{
    if (0 != error_number)
    {
        strncpy(s_error_info, strerror(error_number), kMaxErrorInfoSize - 1);
    }
}

void SetErrorInfo(const std::string& str_error)
{
    strncpy(s_error_info, str_error.c_str(), kMaxErrorInfoSize - 1);
}

}

}