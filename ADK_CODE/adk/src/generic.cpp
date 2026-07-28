#include <adk/arch/generic.h>

namespace adk_impl
{

bool InnerIsEnvSetLowUtilization()
{
    char* af_performance = std::getenv("AF_PERFORMANCE");
    return (nullptr != af_performance) && (strcmp(af_performance, "LowUtilization") == 0);
}

bool IsEnvSetLowUtilization()
{
    static bool is_low_utilization = InnerIsEnvSetLowUtilization();
    return is_low_utilization;
}

}