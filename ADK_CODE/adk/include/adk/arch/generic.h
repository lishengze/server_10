/**
 * @brief      macros and types for prortable
 * @author     zhao nan
 * @date       2016/10/12
 */

#ifndef ADK_IMPL_COMMON_UTIL_H_
#define ADK_IMPL_COMMON_UTIL_H_

#include "../constant.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <malloc.h>

#include <string>
#include <cstddef>

#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#if SIZE_MAX == UINT_MAX
typedef int ssize_t; /* common 32 bit case */
#define SSIZE_MIN INT_MIN
#define SSIZE_MAX INT_MAX
#elif SIZE_MAX == ULONG_MAX
typedef long ssize_t; /* linux 64 bits */
#define SSIZE_MIN LONG_MIN
#define SSIZE_MAX LONG_MAX
#elif SIZE_MAX == ULLONG_MAX
typedef long long ssize_t; /* windows 64 bits */
#define SSIZE_MIN LLONG_MIN
#define SSIZE_MAX LLONG_MAX
#elif SIZE_MAX == USHRT_MAX
typedef short ssize_t; /* is this even possible? */
#define SSIZE_MIN SHRT_MIN
#define SSIZE_MAX SHRT_MAX
#elif SIZE_MAX == UINTMAX_MAX
typedef uintmax_t ssize_t; /* last resort, chux suggestion */
#define SSIZE_MIN INTMAX_MIN
#define SSIZE_MAX INTMAX_MAX
#else
#error platform has exotic SIZE_MAX
#endif

#if (defined _MSC_VER && (defined _M_IX86 || defined _M_X64))
#include <intrin.h>
#include <WinSock2.h>   //必须在windows之前包含。
#include <windows.h>
#endif

#if defined(_MSC_VER)

#define CLOCK_MONOTONIC_RAW 0
#define CLOCK_REALTIME 1

#define msleep(msec) Sleep(static_cast<DWORD>(msec))
#define usleep(msec) Sleep(static_cast<DWORD>((msec) / 1000))
#define bzero(a, b) ZeroMemory((a), (b))

static inline long long microseconds_now(bool s_use_q = true)
{
    static LARGE_INTEGER s_frequency;
    static BOOL s_use_qpc = QueryPerformanceFrequency(&s_frequency);
    if (s_use_qpc && s_use_q)
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return (1000LL * now.QuadPart) / (s_frequency.QuadPart / 1000);
    }
    else
    {
        return 1000 * GetTickCount64();
    }
}

static inline int clock_gettime(int mode, struct timespec* spec, bool s_use_q = true)  //C-file part
{
    if (mode == CLOCK_MONOTONIC_RAW)
    {
        uint64_t useond = microseconds_now(s_use_q);
        spec->tv_sec    = useond / 1000000i64;         //seconds
        spec->tv_nsec   = useond % 1000000i64 * 1000; //nano-seconds
    }
    else if (mode == CLOCK_REALTIME)
    {
        __int64 wintime;
        GetSystemTimeAsFileTime((FILETIME*)&wintime);
        wintime -= 116444736000000000i64;             //1jan1601 to 1jan1970
        spec->tv_sec  = wintime / 10000000i64;        //seconds
        spec->tv_nsec = wintime % 10000000i64 * 100;  //nano-seconds
    }
    return 0;
}

#endif

#if defined(_MSC_VER)
  #define LIBADK_EXPORT __declspec(dllexport)
  #pragma warning(disable:4200)
#else
  #define LIBADK_EXPORT
#endif

typedef unsigned int uint;

#ifdef _MSC_VER
#define ADK_DEL_LL_CONST(x) x##I64
#define ADK_DEL_ULL_CONST(x) x##UI64
#define ADK_LLD "%I64d"        // used by printf("xx" ADK_LLD "xx");
#else
#define ADK_DEL_LL_CONST(x) x##LL
#define ADK_DEL_ULL_CONST(x) x##ULL
#define ADK_LLD "%lld"
#endif

typedef uint64_t SizeType;

static const int32_t kint32Max = 0x7FFFFFFF;
static const int32_t kint32Min = -kint32Max - 1;
static const int64_t kint64Max = ADK_DEL_LL_CONST(0x7FFFFFFFFFFFFFFF);
static const int64_t kint64Min = -kint64Max - 1;
static const uint32_t kuint32Max = 0xFFFFFFFFu;
static const uint64_t kuint64Max = ADK_DEL_ULL_CONST(0xFFFFFFFFFFFFFFFF);

#ifdef __GNUC__

  #define ADK_NORETURN           __attribute__((noreturn))
  #define ADK_ALIGN(align_size)  __attribute__((aligned(align_size)))
  #define ADK_ALIGN_DEF(struct_type, struct_name, align_size) struct_type struct_name __attribute__((aligned(align_size)))
  #define ADK_UNUSED             __attribute__((unused))
  #define ADK_ALWAYS_INLINE      inline __attribute__((always_inline))
  #define ADK_NOINLINE           __attribute__((noinline))
  #define ADK_CONSTFUNC          __attribute__((const))
  #define ADK_UNREACHABLE        __builtin_unreachable

#define ADK_EMPTY_CACHE_LINE     ADK_CONCATENATE(char pad, __LINE__) \
                                 __attribute__((aligned(ADK_CACHE_LINE_SIZE)))

  /**
   * @brief      生成有利于分支预测的代码,告知编译器条件表达式"x"为true的概率较高
   *
   * @param      x     条件表达式
   *
   * @return     条件表达式的布尔值
   */
  #ifndef ADK_LIKELY
    #define ADK_LIKELY(x) (__builtin_expect(!!(x), 1))
  #endif

  /**
   * @brief      生成有利于分支预测的代码,告知编译器条件表达式"x"为false的概率较高
   *
   * @param      x     条件表达式
   *
   * @return     条件表达式的布尔值
   */
  #ifndef ADK_UNLIKELY
    #define ADK_UNLIKELY(x) (__builtin_expect(!!(x), 0))
  #endif

  #define ADK_GET_PID                       getpid();
  #define ADK_GET_TID                       syscall(SYS_gettid);
  #define aligned_malloc(alignment, size)   memalign(alignment, size)
  #define aligned_free(memory)              free(memory)

  #if __GNUC__ > 4 || (__GNUC__ >= 4 && __GNUC_MINOR__ >= 3)
    #define ADK_HOT              __attribute__((hot,flatten))
    #define ADK_COLD             __attribute__((cold))
  #else
    #define ADK_HOT
    #define ADK_COLD
  #endif

  #if defined(__x86_64__)
    /**
     * @brief      编译器内存屏障，防止编译器生成乱序的代码
     */
    #ifndef ADK_BARRIER
      #define ADK_BARRIER()     asm volatile ("" : : : "memory")
    #endif

    /**
     * @brief      CPU内存屏障，防止CPU乱序执行指令
     */
    #define ADK_MB()            asm volatile ("mfence":::"memory")

    /**
     * @brief      CPU内存屏障，防止CPU乱序执行内存读取指令
     */
    #define ADK_RMB()           asm volatile ("lfence":::"memory")

    /**
     * @brief      CPU内存屏障，防止CPU乱序执行内存写入指令
     */
    #define ADK_WMB()           asm volatile ("sfence" ::: "memory")

    /**
     * @brief      产生一条CPU暂停指令，以达到纳秒级delay的效果，主要用于无锁编程中
     *             的busy retry环节
     */
    #ifndef ADK_PAUSE
      #define ADK_PAUSE()       asm volatile ("pause" : : : "memory")
    #endif
  #elif defined(__aarch64__)
    #ifndef ADK_BARRIER
      #define ADK_BARRIER()     __asm__ __volatile__("dmb ish" : : : "memory")
    #endif
    #define ADK_MB()            __asm__ __volatile__("dmb ish" : : : "memory")
    #define ADK_RMB()           __asm__ __volatile__("dmb ishld" : : : "memory")
    #define ADK_WMB()           __asm__ __volatile__("dmb ishst" : : : "memory")
    #ifndef ADK_PAUSE
      #define ADK_PAUSE()       __asm__ __volatile__("yield" : : : "memory")
    #endif
  #endif
#else
  
  #if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    #define ADK_ALIGN(align_size)           __declspec(align(align_size))
    #define ADK_ALIGN_DEF(struct_type, struct_name, align_size) __declspec(align(align_size)) struct_type struct_name
    #define ADK_BARRIER()       ::_ReadWriteBarrier()
    #define ADK_MB()            ::_mm_mfence()
    #define ADK_RMB()           ::_mm_mfence()
    #define ADK_WMB()           ::_mm_mfence()
    #define ADK_PAUSE()         ::_mm_pause()
    #define ADK_EMPTY_CACHE_LINE            ADK_ALIGN_DEF(char, ADK_CONCATENATE(pad, __LINE__), ADK_CACHE_LINE_SIZE)

    #define PATH_MAX            MAX_PATH
    #define pid_t               int
    #define ADK_GET_PID         GetCurrentProcessId();
    #define ADK_GET_TID         GetCurrentThreadId();

    #define aligned_malloc(alignment, size) _aligned_malloc(size, alignment)
    #define aligned_free(memory)            _aligned_free(memory)

template <typename From, typename To, typename = void>
struct is_static_castable : std::false_type
{
};

template <typename From, typename To>
struct is_static_castable<From, To, decltype(static_cast<To>(std::declval<From>()), void())>
    : std::true_type
{
};

template <typename T, typename V> __forceinline static
typename std::enable_if<sizeof(T) == sizeof(char)
                        && is_static_castable<T, char>::value
                        && is_static_castable<V, char>::value, T>::type
    __sync_lock_test_and_set(T* t, V v)
{
    return (T)_InterlockedExchange8((volatile char*)t, (char)v);
}

template <typename T, typename V> __forceinline static
typename std::enable_if<sizeof(T) == sizeof(char) 
                        && is_static_castable<T, char>::value 
                        && is_static_castable<V, char>::value, T>::type
    __sync_fetch_and_add(T* t, V v)
{
    return (T)_InterlockedExchangeAdd8((volatile char*)t, (char)v);
}

template <typename T, typename V> __forceinline static 
typename std::enable_if<sizeof(T) == sizeof(char)
                        && is_static_castable<T, char>::value 
                        && is_static_castable<V, char>::value, T>::type
    __sync_fetch_and_sub(T* t, V v)
{
    return (T)_InterlockedExchangeAdd8((volatile char*)t, -(char)v);
}

template <typename T, typename C, typename X> __forceinline static
typename std::enable_if<sizeof(T) == sizeof(char) 
                        && is_static_castable<T, char>::value 
                        && is_static_castable<C, char>::value 
                        && is_static_castable<X, char>::value, bool>::type
    __sync_bool_compare_and_swap(T* t, C c, X x)
{
    return (c == (T)_InterlockedCompareExchange8((volatile char*)t, (char)x, (char)c));
}

template <typename T, typename V> __forceinline static
typename std::enable_if<sizeof(T) == sizeof(short) 
                        && is_static_castable<T, short>::value
                        && is_static_castable<V, short>::value, T>::type
    __sync_lock_test_and_set(T* t, V v)
{
    return (T)_InterlockedExchange16((volatile short*)t, (short)v);
}

template <typename T, typename V> __forceinline static
typename std::enable_if<sizeof(T) == sizeof(short) 
                        && is_static_castable<T, short>::value
                        && is_static_castable<V, short>::value, T>::type
    __sync_fetch_and_add(T* t, V v)
{
    return (T)_InterlockedExchangeAdd16((volatile short*)t, (short)v);
}

template <typename T, typename V> __forceinline static
typename std::enable_if<sizeof(T) == sizeof(short) 
                        && is_static_castable<T, short>::value 
                        && is_static_castable<V, short>::value, T>::type
    __sync_fetch_and_sub(T* t, V v)
{
    return (T)_InterlockedExchangeAdd16((volatile short*)t, -(short)v);
}

template <typename T, typename C, typename X> __forceinline static
typename std::enable_if<sizeof(T) == sizeof(short) 
                        && is_static_castable<T, short>::value 
                        && is_static_castable<C, short>::value 
                        && is_static_castable<X, short>::value, bool>::type
    __sync_bool_compare_and_swap(T* t, C c, X x)
{
    return (c == (T)_InterlockedCompareExchange16((volatile short*)t, (short)x, (short)c));
}

template <typename T, typename V> __forceinline static
typename std::enable_if<sizeof(T) == sizeof(long) 
                        && is_static_castable<T, long>::value
                        && is_static_castable<V, long>::value, T>::type
    __sync_lock_test_and_set(T* t, V v)
{
    return (T)_InterlockedExchange((volatile long*)t, (long)v);
}

template <typename T, typename V> __forceinline static
typename std::enable_if<sizeof(T) == sizeof(long) 
                        && is_static_castable<T, long>::value 
                        && is_static_castable<V, long>::value, T>::type
    __sync_fetch_and_add(T* t, V v)
{
    return (T)_InterlockedExchangeAdd((volatile long*)t, (long)v);
}

template <typename T, typename V> __forceinline static
typename std::enable_if<sizeof(T) == sizeof(long) 
                        && is_static_castable<T, long>::value 
                        && is_static_castable<V, long>::value, T>::type
    __sync_fetch_and_sub(T* t, V v)
{
    return (T)_InterlockedExchangeAdd((volatile long*)t, -(long)v);
}

template <typename T, typename C, typename X> __forceinline static 
typename std::enable_if<sizeof(T) == sizeof(long) 
                        && is_static_castable<T, long>::value 
                        && is_static_castable<C, long>::value 
                        && is_static_castable<X, long>::value, bool>::type
    __sync_bool_compare_and_swap(T* t, C c, X x)
{
    return (c == (T)_InterlockedCompareExchange((volatile long*)t, (long)x, (long)c));
}

template <typename T, typename V> __forceinline static
typename std::enable_if<sizeof(T) == sizeof(__int64) 
                        && is_static_castable<T, __int64>::value
                        && is_static_castable<V, __int64>::value, T>::type
    __sync_lock_test_and_set(T* t, V v)
{
    return (T)_InterlockedExchange64((volatile __int64*)t, (__int64)v);
}

template <typename T, typename V> __forceinline static
typename std::enable_if<sizeof(T) == sizeof(__int64) 
                        && is_static_castable<T, __int64>::value 
                        && is_static_castable<V, __int64>::value, T>::type
    __sync_fetch_and_add(T* t, V v)
{
    return (T)_InterlockedExchangeAdd64((volatile __int64*)t, (__int64)v);
}

template <typename T, typename V> __forceinline static
typename std::enable_if<sizeof(T) == sizeof(__int64)
                        && is_static_castable<T, __int64>::value 
                        && is_static_castable<V, __int64>::value, T>::type
    __sync_fetch_and_sub(T* t, V v)
{
    return (T)_InterlockedExchangeAdd64((volatile __int64*)t, -(__int64)v);
}

template <typename T, typename C, typename X> __forceinline static
typename std::enable_if<sizeof(T) == sizeof(__int64) 
                        && is_static_castable<T, __int64>::value 
                        && is_static_castable<C, __int64>::value 
                        && is_static_castable<X, __int64>::value, bool>::type
    __sync_bool_compare_and_swap(T* t, C c, X x)
{
    return (c == (T)_InterlockedCompareExchange64((volatile __int64*)t, (__int64)x, (__int64)c));
}

  #endif

  #define ADK_NORETURN
  #define ADK_UNUSED
#ifdef _MSC_VER
  #define ADK_ALWAYS_INLINE __forceinline
#endif 

#ifdef __GNUC__
  #define ADK_ALWAYS_INLINE inline
#endif
  #define ADK_NOINLINE
  #define ADK_CONSTFUNC
  #define ADK_UNREACHABLE
  #define ADK_HOT
  #define ADK_COLD
  #define ADK_LIKELY(x) (x)
  #define ADK_UNLIKELY(x) (x)

#endif

#define ADK_SET_USED(var)       (void)(var)
#define ADK_ALIGND(align_size)  ADK_ALIGN(align_size)

#define ADK_CACHE_LINE_ALIGN    ADK_ALIGN(ADK_CACHE_LINE_SIZE)

#define ADK_ASSERT(func_call, result)       \
{                                           \
    ADK_UNUSED const auto ec = func_call;   \
    assert(result == ec);                   \
}

#define ADK_ASSERT_BOOL(func_call)          \
{                                           \
    ADK_UNUSED const auto ec = func_call;   \
    assert(ec);                             \
}

#define ADK_ASSERT_SUCCESS(func_call) ADK_ASSERT(func_call, ErrorCode::kSuccess);

/**
 * @brief      强制编译器生成一条从内存读取变量x值的CPU指令
 *
 * @param      x     要读取的变量
 *
 * @return     变量x的最新值
 */
#ifdef __aarch64__
#define ACCESS_ONCE(x) (                                    \
{                                                           \
     const auto touch_x = (*(volatile decltype(x) *)&(x));  \
     ADK_RMB();                                             \
     touch_x;                                               \
}                                                           \
)
#else
#define ACCESS_ONCE(x) (*(volatile decltype(x) *)&(x))
#endif

namespace adk_impl
{
/**
 * @brief      获取CPU的时间戳寄存器值
 *
 * @return     CPU时间戳寄存器的值
 */
static inline uint64_t GetTSC()
{
#if (defined _MSC_VER && (defined _M_IX86 || defined _M_X64))
     return __rdtsc ();
#elif (defined __GNUC__ && (defined __i386__ || defined __x86_64__))
    uint32_t low, high;
    asm volatile ("rdtsc" : "=a" (low), "=d" (high));
    return (uint64_t) high << 32 | low;
#elif (defined __GNUC__ && defined __aarch64__)
    struct timespec tmp;
    clock_gettime(CLOCK_MONOTONIC, &tmp);
    return (uint64_t)(tmp.tv_sec*1000000000 + tmp.tv_nsec);
#endif
}

template<int pricision = 0, int shift = 0, bool is_sync = true>
inline uint64_t GetTSCLowerBits()
{
#if (defined __GNUC__ && (defined __i386__ || defined __x86_64__))
    uint32_t low, high, cx;
    if (!is_sync)
    {
        asm volatile ("rdtsc" : "=a" (low), "=d" (high));
    }
    else
    {
        asm volatile ("rdtscp" : "=a" (low), "=d" (high), "=c" (cx));
    }
    return ((uint64_t)(low >> pricision) << shift);
#elif (defined __GNUC__ && defined __aarch64__)
    struct timespec tmp;
    clock_gettime(CLOCK_MONOTONIC, &tmp);
    uint64_t ret = (uint64_t)(tmp.tv_sec*1000000000 + tmp.tv_nsec);
    return ((uint64_t)((ret & ((1ull << 32) -1)) >> pricision) << shift);
#endif
}

inline uint64_t SyncGetTSC()
{
#if (defined _MSC_VER && (defined _M_IX86 || defined _M_X64))
     return __rdtsc ();
#elif (defined __GNUC__ && (defined __i386__ || defined __x86_64__))
    uint64_t ret;
    unsigned int ax, dx, cx;
    asm volatile("rdtscp" : "=a" (ax), "=d" (dx), "=c" (cx));
    ret = ((uint64_t)ax) | (((uint64_t)dx) << 32);
    return ret;
#elif (defined __GNUC__ && defined __aarch64__)
    struct timespec tmp;
    clock_gettime(CLOCK_MONOTONIC, &tmp);
    return (uint64_t)(tmp.tv_sec*1000000000 + tmp.tv_nsec);
#endif
}

}    // namespace adk_impl

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
static inline uint64_t getpagesize()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return static_cast<uint64_t>(si.dwPageSize);
}
#define ADK_PAGE_SIZE getpagesize()
#else
#include <unistd.h>
#define ADK_PAGE_SIZE static_cast<uint64_t>(getpagesize())
#endif

#define ADK_PAGE_MASK (~(ADK_PAGE_SIZE - 1))

/**
 * @brief      将一个内存地址向上对齐到一个内存页的边界
 *
 * @return     对齐后的内存地址
 */
#define ADK_PAGE_ALIGN(x) ((decltype(x))((uint64_t)((char*)(x) + ADK_PAGE_SIZE - 1) & ADK_PAGE_MASK))

/**
 * @brief      检查内存地址是否对齐到4K大小的页面
 *
 * @param      x     内存地址
 *
 * @return     内存地址x对齐到4K页面时返回true
 */
#define ADK_IS_PAGE_ALIGN(x) (((uint64_t)(x) & (ADK_PAGE_SIZE - 1)) == 0ul)

/**
 * @brief      获取结构中某一字段距结构首地址的偏移值
 *
 * @param[in]   TYPE    结构的类型
 * @param[in]   MEMBER  结构中字段的名称
 *
 * @return     偏移值
 */
#define ADK_OFFSET_OF(TYPE, MEMBER) (size_t)(offsetof(TYPE, MEMBER))  //((size_t) &(((TYPE *)0)->MEMBER))
//Note: offsetof() can be used in constexpr, but pointer access can't, since it has a implicit reinterpret_cast<>() call.


/**
 * @brief      获取ptr所指向字段的外部容器结构的首地址
 *
 * @param[in]   ptr     指针地址值
 * @param[in]   type    外部容器的类型
 * @param[in]   member  ptr在容器结构定义中的名称
 *
 * @return     CPU时间戳寄存器的值
 */
#define ADK_CONTAINER_OF(ptr, type, member) ((type*)((char*)(ptr) - ADK_OFFSET_OF(type, member)))

#define ADK_DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

#define ADK_ROUND_UP(x, y) (decltype(y)((((x) + ((y) - 1)) / (y)) * (y)))

/**
 * @brief      将一个32位的整数值，向上取整到最近的2的N次幂
 *
 * @param      整数值
 *
 * @return     2的N次幂
 */
#ifdef __GNUC__
#define ADK_ROUND_TO_POWER_OF_2(v) (                       \
 {                                                         \
     uint32_t __v = (uint32_t)(v);                         \
     --__v;                                                \
     __v |= __v >> 1;                                      \
     __v |= __v >> 2;                                      \
     __v |= __v >> 4;                                      \
     __v |= __v >> 8;                                      \
     __v |= __v >> 16;                                     \
     ++__v;                                                \
 }                                                         \
 )
#else
 namespace adk_impl
 {
 static inline uint32_t RoundToPowerOf2(uint32_t v)
 {
     uint32_t __v = (uint32_t)(v);
     --__v;
     __v |= __v >> 1;
     __v |= __v >> 2;
     __v |= __v >> 4;
     __v |= __v >> 8;
     __v |= __v >> 16;
     ++__v;
     return __v;
 }
 }
 #define ADK_ROUND_TO_POWER_OF_2(v) adk_impl::RoundToPowerOf2(v)
#endif

#ifndef ADK_MAX
/**
 * @brief      求两个同样类型变量的最大值
 *
 * @param      x     变量x
 * @param      y     变量y
 *
 * @return     x和y中的最大值
 */
#define ADK_MAX(x, y) ({                             \
        decltype(x) _max1 = (x);                      \
        decltype(y) _max2 = (y);                      \
        (void) (&_max1 == &_max2);                  \
        _max1 > _max2 ? _max1 : _max2; })
#endif

#ifndef ADK_MIN
/**
 * @brief      求两个同样类型变量的最小值
 *
 * @param      x     变量x
 * @param      y     变量y
 *
 * @return     x和y中的最小值
 */
#define ADK_MIN(x, y) ({                             \
        decltype(x) _min1 = (x);                      \
        decltype(y) _min2 = (y);                      \
        (void) (&_min1 == &_min2);                  \
        _min1 < _min2 ? _min1 : _min2; })
#endif

#define ADK_GET_NARG(...) ADK_GET_NARG_(__VA_ARGS__, ADK_GET_RSEQ_N_ARG())
#define ADK_GET_NARG_(...) ADK_GET_N_ARG(__VA_ARGS__) 
#ifndef ADK_GET_N_ARG
#define ADK_GET_N_ARG(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, N, ...) N
#endif
#ifndef ADK_GET_RSEQ_N_ARG
#define ADK_GET_RSEQ_N_ARG() 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
#endif
#define ADK_CONCATENATE(arg1, arg2)   ADK_CONCATENATE1(arg1, arg2)
#define ADK_CONCATENATE1(arg1, arg2)  ADK_CONCATENATE2(arg1, arg2)
#define ADK_CONCATENATE2(arg1, arg2)  arg1##arg2

#define ADK_TTPARAM(type) template<typename> class type

namespace adk_impl
{

/**
 * @brief      将value向上取整到最近的2的N次幂，返回数值N
 *
 * @param[in]  value  待向上取整的变量
 *
 * @return     幂次N
 */
static inline uint32_t GetBits(uint32_t value)
{
    uint32_t bits = 0;
    while (((uint32_t)1 << bits) < value)
    {
        ++bits;
    }
    return bits;
}

/**
 * @brief      检查是否设置环境变量AF_PERFORMANCE且变量值设置为低资源模式LowUtilization
 *
 *
 * @return     设置该环境变量且变量值为LowUtilization则返回true,否则返回false
 */
bool IsEnvSetLowUtilization();

} // adk

#endif    // ADK_COMMON_UTIL_H_
