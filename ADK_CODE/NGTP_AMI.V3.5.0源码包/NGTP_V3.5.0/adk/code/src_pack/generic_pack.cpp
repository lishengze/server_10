#include <adk_pack/arch/generic.h>
#include <adk/arch/generic.h>

namespace adk
{

namespace impl
{

void CpuPause()
{
#if defined(__GNUC__) && (defined(__x86_64__))
    asm volatile ("pause" : : : "memory");
#elif defined(__GNUC__) && (defined(__aarch64__))
    asm volatile ("yield" : : : "memory");
#endif
}

void Barrier()
{
#if defined(__GNUC__) && (defined(__x86_64__))
    asm volatile ("" : : : "memory");
#elif defined(__GNUC__) && (defined(__aarch64__))
    asm volatile ("dmb ish" : : : "memory");
#elif defined(_MSC_VER)
    ::_ReadWriteBarrier();
#endif
}

}

/**
 * @brief      检查是否设置环境变量AF_PERFORMANCE且变量值设置为低资源模式LowUtilization
 *
 *
 * @return     设置该环境变量且变量值为LowUtilization则返回true,否则返回false
 */
bool IsEnvSetLowUtilization()
{
    return adk_impl::IsEnvSetLowUtilization();
}

}