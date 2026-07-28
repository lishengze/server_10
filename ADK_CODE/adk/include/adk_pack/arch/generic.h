/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_ARCH_GENERIC_H_
#define ADK_ARCH_GENERIC_H_

namespace adk
{

namespace impl
{

extern void CpuPause();
extern void Barrier();

template<typename ObjectType>
static inline void AssignFunction(void* dst, const void* src)
{
    *((ObjectType*)dst) = *((ObjectType*)src);
}

}
bool IsEnvSetLowUtilization();

}

#ifndef ADK_PAUSE
#define ADK_PAUSE()  adk::impl::CpuPause()
#endif

#ifndef ADK_LIKELY
#define ADK_LIKELY(x) (x)
#endif

#ifndef ADK_UNLIKELY
#define ADK_UNLIKELY(x) (x)
#endif

#ifndef ADK_NORETURN
    #ifdef __GNUC__
        #define ADK_NORETURN           __attribute__((noreturn))
        #define ADK_ALIGN(align_size)  __attribute__((aligned(align_size)))
        #define ADK_UNUSED             __attribute__((unused))
        #define ADK_ALWAYS_INLINE      inline __attribute__((always_inline))
        #define ADK_NOINLINE           __attribute__((noinline))
        #define ADK_CONSTFUNC          __attribute__((const))
        #define ADK_UNREACHABLE        __builtin_unreachable
        #if __GNUC__ > 4 || (__GNUC__ >= 4 && __GNUC_MINOR__ >= 3)
            #define ADK_HOT __attribute__((hot,flatten))
            #define ADK_COLD __attribute__((cold))
        #else
            #define ADK_HOT
            #define ADK_COLD
        #endif
    #else
        #define ADK_NORETURN
        #define ADK_ALIGN(align_size)  __declspec(align(align_size))
        #define ADK_UNUSED
        #define ADK_ALWAYS_INLINE      inline
        #define ADK_NOINLINE
        #define ADK_CONSTFUNC
        #define ADK_UNREACHABLE
        #define ADK_HOT
        #define ADK_COLD
    #endif
#endif

#ifndef ADK_CACHE_LINE_SIZE
#define ADK_CACHE_LINE_SIZE                 64
#endif

/**
 * @brief      编译器内存屏障，防止编译器生成乱序的代码
 */
#ifndef ADK_BARRIER
#define ADK_BARRIER()  adk::impl::Barrier()
#endif

#ifndef ADK_ALIGND
#define ADK_ALIGND(align_size)  ADK_ALIGN(align_size)
#endif

#ifndef ADK_CACHE_LINE_ALIGN
#define ADK_CACHE_LINE_ALIGN    ADK_ALIGN(ADK_CACHE_LINE_SIZE)
#endif

#ifndef ADK_EMPTY_CACHE_LINE
#define ADK_EMPTY_CACHE_LINE    ADK_CONCATENATE(char pad, __LINE__) \
                                        __attribute__((aligned(ADK_CACHE_LINE_SIZE)))
#endif

#endif