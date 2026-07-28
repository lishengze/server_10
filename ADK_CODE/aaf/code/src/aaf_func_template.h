#include <boost/function.hpp>
#include <list>
namespace aaf
{
template <typename T, int tag = 1>
struct FunctionRegistry
{
    static void RegisterFunction(T func) { s_func_list_.push_back(func); }

    template <typename HandlerType>  //
    static int32_t ForeachFunc(const HandlerType& handler)
    {
        for (auto func : s_func_list_)
        {
            if (handler(func) != aaf::ErrorCode::kSuccess)
            {
                return aaf::ErrorCode::kFailure;
            }
        }
        return aaf::ErrorCode::kSuccess;
    }
    static std::list<T> s_func_list_;
};

template <typename T, int tag>
std::list<T> FunctionRegistry<T, tag>::s_func_list_;

typedef boost::function<int32_t()> OnAmiInitBeginFunc;
typedef boost::function<int32_t()> OnAmiExitEndFunc;
typedef boost::function<int32_t(aaf::EndpointHandler* ep_hdl,
                                const std::string& ep_name)>
    OnTxEndpointCreationFunc;
typedef boost::function<int32_t(const std::string& ep_name,
                                ami::MessageHandler** msg_hdl,
                                bool is_ha_ctx)>
    OnRxEndpointCreationFunc;
typedef boost::function<int32_t(const std::string& context_name, bool is_ha_ctx, ami::Property& props)>
    OnConfigureContextPropertyFunc;
}  // namespace aaf
