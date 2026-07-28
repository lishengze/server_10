#ifndef ADK_IMPL_IO_ENGINE_LAST_ERROR_H_
#define ADK_IMPL_IO_ENGINE_LAST_ERROR_H_

#include <string>

namespace adk_impl
{

namespace io_engine
{

const char* GetErrorInfo();

void SetErrorInfo(int error_number);

void SetErrorInfo(const std::string& str_error);

}

}

#endif
