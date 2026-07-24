#ifndef ADK_IMPL_IO_ENGINE_EP_REGISTER_H_
#define ADK_IMPL_IO_ENGINE_EP_REGISTER_H_

#include <set>
#include <mutex>

namespace adk_impl
{

namespace io_engine
{

using std::set;
using std::mutex;
using std::lock_guard;

class TcpEndpoint;

struct RegisterInfo
{
    mutex reg_mutex;
    set<const TcpEndpoint*> reg_endpoints;
};

class EndpointRegister
{
public:

    static void RegisterEndpoint(TcpEndpoint* const endpoint_impl);

    static void UnregisterEndpoint(TcpEndpoint* const endpoint_impl);

    static int32_t VerifyEndpoint(const TcpEndpoint* endpoint_impl);

private:
    static RegisterInfo* GetInstance();
};


}

}

#endif