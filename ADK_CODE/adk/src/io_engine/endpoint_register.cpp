#include "endpoint_impl.h"
#include "endpoint_register.h"

namespace adk_impl
{

namespace io_engine
{

void EndpointRegister::RegisterEndpoint(TcpEndpoint* const endpoint_impl)
{
    RegisterInfo* const register_info = EndpointRegister::GetInstance();
    assert(register_info);

    lock_guard<mutex> lock(register_info->reg_mutex);
    register_info->reg_endpoints.insert(endpoint_impl);
}

void EndpointRegister::UnregisterEndpoint(TcpEndpoint* const endpoint_impl)
{
    RegisterInfo* const register_info = EndpointRegister::GetInstance();
    assert(register_info);

    lock_guard<mutex> lock(register_info->reg_mutex);
    register_info->reg_endpoints.erase(endpoint_impl);
}

int32_t EndpointRegister::VerifyEndpoint(const TcpEndpoint* endpoint_impl)
{
    RegisterInfo* const register_info = EndpointRegister::GetInstance();
    assert(register_info);

    lock_guard<mutex> lock(register_info->reg_mutex);
    const auto iter = register_info->reg_endpoints.find(endpoint_impl);
    if (ADK_UNLIKELY(register_info->reg_endpoints.end() == iter))
    {
        return ErrorCode::kFailure;
    }

    return ErrorCode::kSuccess;
}

RegisterInfo* EndpointRegister::GetInstance()
{
    static RegisterInfo*  register_info = new RegisterInfo();
    return register_info;
}

}

}