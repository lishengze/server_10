/**
 *  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved.
 *  Redistribution and use in source and binary forms, with or without  modification, are not permitted.
 *  For more information about Archforce, welcome to archforce.cn.
 *
 *  @file util.h
 *  @brief adk工具类
 **/
#ifndef ADK_IMPL_UTIL_H_
#define ADK_IMPL_UTIL_H_

#include <time.h>
#include <string.h>
#include <malloc.h>

#include <chrono>
#include <string>
#include <cstdint>
#include <fstream>
#include <iostream>

#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/regex.hpp>

#include "constant.h"
#include "arch/generic.h"
#include "json/json_fwd.hpp"
#include "adk/libadk.h"
namespace adk_impl
{

using std::string;

/**
 * @brief      计算timespec类型的时间点间的差值，即计算相应的时间差
 *
 * @param[in]  lv    结束时间点
 * @param[in]  rv    开始时间点
 *
 * @return     返回时间差，单位为纳秒
 */
static inline int64_t time_diff(struct timespec& lv, struct timespec& rv)
{
    return (lv.tv_sec - rv.tv_sec) * 1000000000L + lv.tv_nsec - rv.tv_nsec;
}

static inline bool time_before(struct timespec& lv, struct timespec& rv)
{
    return (lv.tv_sec < rv.tv_sec)
           || (lv.tv_sec == rv.tv_sec && lv.tv_nsec < rv.tv_nsec);
}

static inline int64_t timespec_now(int clock_type = CLOCK_MONOTONIC_RAW)
{
    struct timespec ts;
    clock_gettime(clock_type, &ts);
    auto time_now = (ts.tv_sec * 1000000000) + ts.tv_nsec;
    return time_now;
}

typedef char ADKNameType[ADK_MAX_NAME_LEN];

/**
 * @brief      将std::string类型内存储的命名，拷贝到ADKNameType类型内
 *
 * @param[in]  adk_name  ADKNameType类型
 * @param[in]  str_name  std::string类型
 */
static inline void NameCopy(ADKNameType& adk_name, const string& str_name)
{
    memcpy(&adk_name[0], str_name.c_str(), std::min<size_t>(str_name.size(), ADK_NAME_LEN_LIMIT));
    adk_name[ADK_LAST_NAME_INDEX] = 0;
}

/**
 * @brief      将ADKNameType类型内存储的命名，拷贝到std::string类型内
 *
 * @param[in]      adk_name  ADKNameType类型
 * @param[in]      str_name  std::string类型
 */
static inline void NameCopy(ADKNameType& adk_name, string* str_name)
{
    adk_name[ADK_LAST_NAME_INDEX] = 0;
    str_name->append(&adk_name[0]);
}

extern boost::mutex g_condition_mutex;

/**
 * @brief      条件表达式check_condition首次成立时，执行func一次
 *
 * @param[in]      check_condition  条件表达式
 * @param[in]      func             待执行的方法
 *
 * @note       在func执行一次后，需保证check_condition不再成立。即需要在func内修改check_condition置为false
 */
#define ADK_CONDITIONAL_CALL_ONCE(check_condition, func) do {	\
	if (check_condition)	\
	{	\
		boost::mutex::scoped_lock lock_guard(adk_impl::g_condition_mutex);	\
		if (check_condition)	\
		{	\
			func();	\
		}	\
	}	\
} while (false)

/**
 * @brief      条件表达式condition首次成立时，执行func一次
 *
 * @param[in]      condition  		条件表达式
 * @param[in]      func             待执行的方法
 *
 * @note       在func执行一次后，需保证condition不再成立。即需要在func内修改condition置为false
 */
template<typename ConditionType, typename FuncType>
void conditional_call_once(const ConditionType& condition, const FuncType& func)
{
	if (condition())
	{
		boost::mutex::scoped_lock lock_guard(g_condition_mutex);
		if (condition())
		{
			func();
		}
	}
}

/**
 * @brief       从library中获取libname的目录路径
 *
 * @param[in]   libname     动态库名字（从路径的末端进行匹配）
 * @param[in]   subpath     路径中子字符串。若为空，返回目录路径
 *
 * @return      成功：返回 libname 所在的目录路径（结尾为'/'），失败：返回空
 */

#if defined(__GNUC__)
static inline std::string GetInstallPath(const std::string& libname, std::string subpath = "/lib64/")
{
    std::string apath;
    std::string fpath;
    pid_t pid = getpid();

    try
    {
        std::ifstream ifs("/proc/" + std::to_string(pid) + "/maps");
        if (!ifs)
        {
            return apath;
        }

        // 7f95b3e23000-7f95b3e58000 rw-p 0108d000 fd:02 2684609037     /home/user/work/fix/ami/code/bin/gcc-4.8.5/debug/threading-multi/libami.so

        std::string line;
        std::string slibname = libname;
        slibname.insert(0, "/");
        while (std::getline(ifs, line))
        {
            std::size_t line_len = line.length();
            std::size_t slib_len = slibname.length();
            if (line_len < slib_len)
            {
                continue;
            }

            //在路径的末端查找 "/libname"
            std::size_t slib_pos = line.find(slibname, line_len - slib_len);
            if (slib_pos == std::string::npos)
            {
                continue;
            }

            //找到路径的起始位置
            std::size_t space_pos = line.find(" /");
            fpath = line.substr(space_pos + 1, slib_pos - space_pos);
            break;
        }
    }
    catch (const std::exception &err)
    {
        return apath;
    }

    if (subpath.empty())
    {
        return fpath;
    }

    size_t pos = fpath.find(subpath);
    if (pos != std::string::npos)
    {
        apath = fpath.substr(0, pos + 1);
    }

    return apath;
}
#endif

/**
 * @brief A Helper Structure to extract type info of a function/functor
 * @tparam F function Type
 */
    template<class F>
    struct function_traits;

    template<class R, class... Args>
    struct function_traits<R(*)(Args...)> : public function_traits<R(Args...)>
    {
    };

    template<class R, class... Args>
    struct function_traits<R(Args...)>
    {
        using return_type = R;
        using args_type = std::tuple<Args...>;
        static constexpr std::size_t args_count = sizeof...(Args);
        static constexpr bool is_function = true;

        template<std::size_t idx>
        struct argument
        {
            static_assert((idx >= 0 && idx < args_count), "Invalid parameter idx.");
            using type = typename std::tuple_element<idx, args_type>::type;
        };
    };

    template<class C, class R, class... Args>
    struct function_traits<R(C::*)(Args...)> : public function_traits<R(Args...)>
    {
    };

    template<class C, class R, class... Args>
    struct function_traits<R(C::*)(Args...) const> : public function_traits<R(Args...)>
    {
    };


    template<class F>
    struct function_traits
    {
    public:
        struct InvalidType{};
    private:

        template <class F_x>
        constexpr static auto getType(F_x && val,
                                      typename std::enable_if<!std::is_same<decltype(&F_x::operator()), void>::value, int>::type = 1) -> function_traits<decltype(&F_x::operator())>
        {
            return *((function_traits<decltype(&F_x::operator())>*)(nullptr));
        }

        template<class F_x>
        constexpr static auto getType(...) -> function_traits<InvalidType()>
        {
            return *((function_traits<InvalidType()> )(nullptr));
        }

        using call_type = decltype(getType<F>(std::declval<F>()));//function_traits<decltype(&F::operator())>;
    public:
        using return_type = typename call_type::return_type;
        using args_type = typename call_type::args_type;
        static constexpr std::size_t args_count = call_type::args_count;
        static constexpr bool is_function = !std::is_same<call_type, function_traits<InvalidType()> >::value;

        template<std::size_t idx>
        struct argument
        {
            //static_assert((idx >= 0 && idx < args_count),"Invalid parameter idx.");
            using type = typename std::tuple_element<idx, args_type>::type;
        };
    };

    template<class F>
    struct function_traits<F &> : public function_traits<F>
    {
    };

    template<class F>
    struct function_traits<F &&> : public function_traits<F>
    {
    };

/**
 * @brief Check if the Return Value Type of a member function pointer is equal to the expected type.
 * @tparam C class Type
 * @tparam F member function Type
 * @tparam RetType expected return Value Type
 * @tparam ArgType argument types of this member function.
 */
    template<class C, class F, class RetType, class... ArgType>
    struct MemberFunctionChecker
    {
        static_assert(std::is_member_function_pointer<F>::value, "F must be a member function pointer");
        struct not_void
        {
        };

        template<class CF>
        static auto check(int) -> decltype((std::declval<C>().*std::declval<CF>())(std::declval<ArgType>()...));

        template<class CF>
        static not_void check(...);

        static const bool value = std::is_same<decltype(check<typename std::decay<F>::type>(0)), RetType>::value;
    };

/**
 * @brief Check if the Return Value Type of a function pointer is equal to the expected type.
 * @tparam F function Type
 * @tparam RetType expected Return Value Type
 * @tparam ArgType argument types of this function
 */
    template<class F, class RetType, class... ArgType>
    struct FunctionChecker
    {
        struct not_void
        {
        };

        template<class CF>
        static auto check(int) -> decltype((*(CF * )

        nullptr)(

        std::declval<ArgType>()

        ...));

        template<class CF>
        static not_void check(...);

        static const bool value = std::is_same<decltype(check<typename std::decay<F>::type>(0)), RetType>::value;
    };

namespace InvokerInfo_Helper
{
    constexpr bool template_and()
    {
        return true;
    }

    constexpr bool template_and(bool value)
    {
        return value;
    }

    template<class... Args>
    constexpr bool template_and(bool value, Args... args)
    {
        return value && template_and(args...);
    }

    template<class T>
    constexpr bool RefOrPointer()
    {
        return std::is_reference<T>::value || std::is_pointer<T>::value;
    }

    template<class T, class U>
    constexpr bool checkSize()
    {
        return sizeof(T) == sizeof(U) || (RefOrPointer<T>() && RefOrPointer<U>());
    }
}

template<class Ret, class... Args>
struct InvokeInfo
{
    void* this_ptr = nullptr;
    Ret (*callptr)(void*, Args...) = nullptr;
    Ret Invoke(Args&&... args)
    {
        return callptr(this_ptr, std::forward<Args>(args)...);
    }
    Ret operator()(Args&&... args)
    {
        return callptr(this_ptr, std::forward<Args>(args)...);
    }

    /**
     * @brief Removes requirement of function signature.
     * @tparam InArgs
     * @param args
     * @return
     */
    template<class... InArgs>
    Ret ForceInvoke(InArgs&&... args)
    {
        static_assert(sizeof...(InArgs) == sizeof...(Args)
          ,"InvokeInfo::ForceInvoke(): Argument count mismatch.");
        using pointerType = Ret(void*, Args...);
        auto call_ptr = reinterpret_cast<pointerType*>(callptr);
        call_ptr(this_ptr, std::forward<InArgs>(args)...);
    }

    template <class Ret_In, class... Args_In>
    auto operator =(const InvokeInfo<Ret_In, Args_In...>& in) -> const InvokeInfo<Ret_In, Args_In...>&
    {
        using namespace InvokerInfo_Helper;
        static_assert(std::is_same<Ret_In, Ret>::value, "InvokeInfo::operator=(): Return Type must be same.");
        static_assert(sizeof...(Args_In) == sizeof...(Args)
                ,"InvokeInfo::operator=(): Argument count mismatch.");
        static_assert(template_and(checkSize<Args,Args_In>()...),
                      "InvokeInfo::operator=(): Size of Arguments mismatch.");
        callptr = reinterpret_cast<decltype(callptr)>(in.callptr);
        this_ptr = in.this_ptr;
        return in;
    }
};

/**
 * @brief Transform a Member Function pointer call to a function call, which have better performance.
 * @param this_ptr this_ptr of this member function call.
 * @param func Member function pointer
 * @return
 */
template<class C, class R, class T, class... Args>
auto TransformMemberFunction(T* this_ptr, R(C::*func)(Args...)) -> InvokeInfo<R, Args...>
{
#ifdef _MSC_VER
    static_assert(false, "TransformMemberFunction(): Do not support MSVC.");
#endif
    static_assert(std::is_base_of<C, T>::value,  "func is not a virtual member function of object this_ptr.");
    auto dat = reinterpret_cast<uint64_t*>(&func);
    InvokeInfo<R, Args...> ret;
    ret.this_ptr = (void*)(static_cast<C*>(this_ptr));
    if((dat[0] & 1) != 0) // virtual function
    {
        auto index = dat[0] - 1;
        /*
         * this_ptr -> vtable T
         * ret.this_ptr -> vtable C_in_T
         */
        auto vtable = *((uint64_t*)(ret.this_ptr));
        static_assert(sizeof(void*) == sizeof(uint64_t),
                      "TransformMemberFunction(): This Version of function requires size of a pointer is 64bit.");
        vtable += index;
        auto funcptr = reinterpret_cast<uint64_t*>(vtable); //uint to pointer
        ret.callptr = reinterpret_cast<decltype(ret.callptr)>(*funcptr);
    }
    else
    {
        ret.callptr = reinterpret_cast<decltype(ret.callptr)>(dat[0]);
    }
    //printf("size:%lu     %lu,%lx\n",sizeof(func), dat[0], dat[1]);
    return ret;
}

#if defined(__GNUC__)
int32_t ParseCpuSet(const std::string& cpu_list, cpu_set_t& cpu_set);
#endif

#define ADK_NOTUSE(var) (void) var

#define ADK_CALC_RATE(saved_ts, saved_stats, cur_stats, rate) do {  \
    const auto local_cur_stats = cur_stats; \
    struct timespec time_cur;   \
    clock_gettime(CLOCK_MONOTONIC_RAW, &time_cur);   \
    const int64_t diff = adk_impl::time_diff(time_cur, saved_ts);    \
    const int64_t stats_diff = local_cur_stats - saved_stats;    \
    double d_rate = stats_diff * 1000000000UL / (double)(diff); \
    d_rate += 0.5f;    \
    rate = (uint64_t)d_rate;    \
    saved_ts = time_cur;    \
    saved_stats = local_cur_stats;  \
} while(false)

struct RateState
{
    uint64_t nr_msgs_saved = 0;
    struct timespec saved_ts = {0,0};
};

static inline uint64_t CalcRate(RateState& rs, uint64_t cur_stats)
{
    uint64_t rate;
    ADK_CALC_RATE(rs.saved_ts, rs.nr_msgs_saved, cur_stats, rate);
    return rate;
}

#define ADK_CALC_RATE_BOUNDED(saved_ts, saved_stats, cur_stats, rate, min_rate) do {  \
    const auto local_cur_stats = cur_stats; \
    struct timespec time_cur;   \
    clock_gettime(CLOCK_MONOTONIC_RAW, &time_cur);   \
    const int64_t diff = adk_impl::time_diff(time_cur, saved_ts);    \
    const int64_t stats_diff = local_cur_stats - saved_stats;    \
    if (stats_diff < 10)  \
    {   \
        rate = stats_diff * 1300000000UL / diff;   \
    }   \
    else    \
    {   \
        rate = stats_diff * 1000000000UL / diff;   \
    }   \
    if (rate == 0 && stats_diff > 0)    \
    {   \
        rate = min_rate;    \
    }   \
    saved_ts = time_cur;    \
    saved_stats = local_cur_stats;  \
} while(false)


// class OnExit
// {
// public:
//     OnExit(const boost::function<void (void)>& do_on_exit)
//         :   do_on_exit_(do_on_exit)
//     {}

//     ~OnExit()
//     {
//         do_on_exit_();
//     }

// private:
//     const boost::function<void (void)> do_on_exit_;
// };

namespace policy {
typedef boost::function<void (void)>    Default;
typedef boost::function<void (void)>*   NoCopy;
}

template<typename FunctionType>
inline void CallOnExit(const FunctionType& func)
{}

template<>
inline void CallOnExit(const policy::Default& func)
{
    func();
}

template<>
inline void CallOnExit(const policy::NoCopy& func)
{
    (*func)();
}

template<typename Policy = policy::Default>
class OnExit
{
public:
    OnExit(const Policy&  do_on_exit)
        :   do_on_exit_(do_on_exit)
    {}

    ~OnExit()
    {
        CallOnExit<Policy>(do_on_exit_);
    }

private:
    const Policy do_on_exit_;
};

#define ADK_RC_NANOS_PER_SECOND     ((1000UL * 1000UL * 1000UL))
template<uint32_t micro_burst = 1, uint32_t mini_delay = 16>
class SimpleRateController
{
public:
    SimpleRateController(uint64_t rate)
        :   delay_interval_ns_(ADK_RC_NANOS_PER_SECOND * micro_burst / (rate > 0 ? rate : 1))
    {
        Reset();
    }

    ~SimpleRateController() {}

    void Reset()
    {
        current_burst_ = 0;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        time_begin_ns_ = ts.tv_sec * ADK_RC_NANOS_PER_SECOND + ts.tv_nsec;
    }

    void Wait()
    {
        // clock_gettime(CLOCK_MONOTONIC_RAW, &(time_now_)); don't do this, to save clock_gettime invoking in micro_burst scenario

        if (micro_burst == 1)
        {
            time_begin_ns_ += delay_interval_ns_;
            clock_gettime(CLOCK_MONOTONIC_RAW, &(time_now_));
            uint64_t time_now_ns = time_now_.tv_sec * ADK_RC_NANOS_PER_SECOND + time_now_.tv_nsec;
            const int64_t tx_delay_ns = time_begin_ns_ - time_now_ns;
            if (tx_delay_ns > 0)
            {
                DelayBySpin(tx_delay_ns);
            }
            else
            {
                time_begin_ns_ = time_now_ns;
            }
            return;
        }

        ++current_burst_;
        if (current_burst_ == micro_burst)
        {
            current_burst_ = 0;
            time_begin_ns_ += delay_interval_ns_;
            clock_gettime(CLOCK_MONOTONIC_RAW, &(time_now_));
            uint64_t time_now_ns = time_now_.tv_sec * ADK_RC_NANOS_PER_SECOND + time_now_.tv_nsec;
            const int64_t tx_delay_ns = time_begin_ns_ - time_now_ns;
            if (tx_delay_ns > 0)
            {
                DelayBySpin(tx_delay_ns);
            }
            else
            {
                time_begin_ns_ = time_now_ns;
            }
        }
    }

private:
    uint32_t            current_burst_;
    struct timespec     time_now_;
    uint64_t            time_begin_ns_;
    const uint64_t      delay_interval_ns_;

    inline void DelayBySpin(int64_t nanoseconds)
    {
        struct timespec begin, end;
        clock_gettime(CLOCK_MONOTONIC_RAW, &begin);
        while (true)
        {
            clock_gettime(CLOCK_MONOTONIC_RAW, &end);
            if (time_diff(end, begin) > nanoseconds)
                return;

            for (uint32_t i = mini_delay; i != 0; --i)
                ADK_PAUSE();
        }
    }
};

template<uint32_t micro_burst = 1, uint32_t mini_delay = 16>
class SimpleVariableRateController
{
public:
    SimpleVariableRateController(uint64_t min_rate, uint64_t max_rate)
        :   delay_interval_ns_(ADK_RC_NANOS_PER_SECOND * micro_burst / (min_rate > 0 ? min_rate : 1)),
            rate_diff_(max_rate - min_rate),
            min_rate_(min_rate > 0 ? min_rate : 1)
    {
        cur_rate_ = (min_rate > 0 ? min_rate : 1);
        Reset();
    }

    ~SimpleVariableRateController() {}

    void Reset()
    {
        current_burst_ = 0;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        time_begin_ns_ = ts.tv_sec * ADK_RC_NANOS_PER_SECOND + ts.tv_nsec;
        shift_time_ns_ = time_begin_ns_;
    }

    void Wait()
    {
        // clock_gettime(CLOCK_MONOTONIC_RAW, &(time_now_)); don't do this, to save clock_gettime invoking in micro_burst scenario

        if (micro_burst == 1)
        {
            time_begin_ns_ += delay_interval_ns_;
            clock_gettime(CLOCK_MONOTONIC_RAW, &(time_now_));
            uint64_t time_now_ns = time_now_.tv_sec * ADK_RC_NANOS_PER_SECOND + time_now_.tv_nsec;
            const int64_t tx_delay_ns = time_begin_ns_ - time_now_ns;
            if (tx_delay_ns > 0)
            {
                DelayBySpin(tx_delay_ns);
            }
            else
            {
                time_begin_ns_ = time_now_ns;
            }
            if (time_now_ns - shift_time_ns_ >= ADK_RC_NANOS_PER_SECOND)
            {
                shift_time_ns_ = time_now_ns;
                srand(static_cast<uint32_t>(time_begin_ns_));
                if (rate_diff_ == 0)
                {
                    cur_rate_ = min_rate_;
                }
                else
                {
                    cur_rate_ = min_rate_ + rand() % rate_diff_ + 1;
                }
                delay_interval_ns_ = ADK_RC_NANOS_PER_SECOND * micro_burst / (cur_rate_ > 0 ? cur_rate_ : 1);
            }

            return;
        }

        ++current_burst_;
        if (current_burst_ == micro_burst)
        {
            current_burst_ = 0;
            time_begin_ns_ += delay_interval_ns_;
            clock_gettime(CLOCK_MONOTONIC_RAW, &(time_now_));
            uint64_t time_now_ns = time_now_.tv_sec * ADK_RC_NANOS_PER_SECOND + time_now_.tv_nsec;
            const int64_t tx_delay_ns = time_begin_ns_ - time_now_ns;
            if (tx_delay_ns > 0)
            {
                DelayBySpin(tx_delay_ns);
            }
            else
            {
                time_begin_ns_ = time_now_ns;
            }
            if (time_now_ns - shift_time_ns_ >= ADK_RC_NANOS_PER_SECOND)
            {
                shift_time_ns_ = time_now_ns;
                srand(static_cast<uint32_t>(time_begin_ns_));
                if (rate_diff_ == 0)
                {
                    cur_rate_ = min_rate_;
                }
                else
                {
                    cur_rate_ = min_rate_ + rand() % rate_diff_ + 1;
                }
                delay_interval_ns_ = ADK_RC_NANOS_PER_SECOND * micro_burst / (cur_rate_ > 0 ? cur_rate_ : 1);
            }
        }
    }
    uint64_t            cur_rate_;
private:
    uint32_t            current_burst_;
    struct timespec     time_now_;
    uint64_t            time_begin_ns_;
    uint64_t            delay_interval_ns_;

    const int64_t       rate_diff_;
    const uint64_t      min_rate_;
    uint64_t            shift_time_ns_;
    // struct timespec     time_begin_;
    // struct timespec     time_end_;

    inline void DelayBySpin(int64_t nanoseconds)
    {
        struct timespec begin, end;
        clock_gettime(CLOCK_MONOTONIC_RAW, &begin);
        while (true)
        {
            clock_gettime(CLOCK_MONOTONIC_RAW, &end);
            if (time_diff(end, begin) > nanoseconds)
                return;

            for (uint32_t i = mini_delay; i != 0; --i)
                ADK_PAUSE();
        }
    }
};

class BurstRateController
{
public:
    BurstRateController(uint64_t rate, uint32_t micro_burst)
        :   micro_burst_(micro_burst),
            delay_interval_ns_(ADK_RC_NANOS_PER_SECOND * micro_burst / (rate > 0 ? rate : 1))
    {
        Reset();
    }

    ~BurstRateController() {}

    void Reset()
    {
        current_burst_ = 0;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        time_begin_ns_ = ts.tv_sec * ADK_RC_NANOS_PER_SECOND + ts.tv_nsec;
    }

    void Wait()
    {
        // clock_gettime(CLOCK_MONOTONIC_RAW, &(time_now_)); don't do this, to save clock_gettime invoking in micro_burst scenario

        if (micro_burst_ == 1)
        {
            time_begin_ns_ += delay_interval_ns_;
            clock_gettime(CLOCK_MONOTONIC_RAW, &(time_now_));
            uint64_t time_now_ns = time_now_.tv_sec * ADK_RC_NANOS_PER_SECOND + time_now_.tv_nsec;
            const int64_t tx_delay_ns = time_begin_ns_ - time_now_ns;
            if (tx_delay_ns > 0)
            {
                DelayBySpin(tx_delay_ns);
            }
            else
            {
                time_begin_ns_ = time_now_ns;
            }
            return;
        }

        ++current_burst_;
        if (current_burst_ == micro_burst_)
        {
            current_burst_ = 0;
            time_begin_ns_ += delay_interval_ns_;
            clock_gettime(CLOCK_MONOTONIC_RAW, &(time_now_));
            uint64_t time_now_ns = time_now_.tv_sec * ADK_RC_NANOS_PER_SECOND + time_now_.tv_nsec;
            const int64_t tx_delay_ns = time_begin_ns_ - time_now_ns;
            if (tx_delay_ns > 0)
            {
                DelayBySpin(tx_delay_ns);
            }
            else
            {
                time_begin_ns_ = time_now_ns;
            }
        }
    }

private:
    uint32_t            current_burst_;
    uint32_t            micro_burst_;
    struct timespec     time_now_;
    uint64_t            time_begin_ns_;
    const uint64_t      delay_interval_ns_;

    inline void DelayBySpin(int64_t nanoseconds)
    {
        struct timespec begin, end;
        clock_gettime(CLOCK_MONOTONIC_RAW, &begin);
        while (true)
        {
            clock_gettime(CLOCK_MONOTONIC_RAW, &end);
            if (time_diff(end, begin) > nanoseconds)
                return;
        }
    }
};

int32_t SetCpuAffinity(const std::string& cpu_list);
int32_t GetCpuAffinity(std::string& cpu_list);
int32_t SetCpuNode(const std::string& cpu_node);
int32_t GetCpuNodeInfo(std::string& cpu_node);

template<typename T>
T atomic_inc(T& var)
{
    return __sync_fetch_and_add(&var, 1);
}

template<typename T>
T atomic_dec(T& var)
{
    return __sync_fetch_and_sub(&var, 1);
}

#define ADK_DO_PERF_TEST(rounds, test_func) do {       \
    struct timeval begin, end;      \
    gettimeofday(&begin, NULL);     \
        \
    for (uint64_t i = (rounds); i != 0; --i)      \
    {   \
        asm volatile("" : : : "memory"); \
        test_func;  \
    }   \
        \
    gettimeofday(&end, NULL);       \
    int64_t total_time = (end.tv_sec - begin.tv_sec) * 1000000 + end.tv_usec - begin.tv_usec;      \
    std::cout << "time total use = " << total_time << " us \t"        \
              << "time each ops = " << (double)total_time / (rounds) << " us"       \
              << std::endl;     \
} while (false)

template<typename SetType>
std::string GetElementList(const SetType& elem_set)
{
    string ret;
    if (elem_set.empty())
        return ret;

    auto it = elem_set.begin();
    ret.append("[");
    ret.append(*it);
    ++it;

    for (; it != elem_set.end(); ++it)
    {
        ret.append(", ");
        ret.append(*it);
    }
    ret.append("]");
    return ret;
}

template<typename MapKey, typename MapValue>
std::string GetElementList(const std::map<MapKey, MapValue>& elem_set)
{
    string ret;
    if (elem_set.empty())
        return ret;

    auto it = elem_set.begin();
    ret.append("[");
    ret.append(boost::lexical_cast<std::string>(it->first));
    ++it;

    for (; it != elem_set.end(); ++it)
    {
        ret.append(", ");
        ret.append(boost::lexical_cast<std::string>(it->first));
    }
    ret.append("]");
    return ret;
}

extern std::string PtreeToString(const boost::property_tree::ptree& ptree, bool is_pretty = false);

static const char g_translation_table[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

#define ADK_TRANS_MASK ((1 << 4) - 1)
template<int width>
uint32_t DisplayFunc(char* buffer, const unsigned char* data)
{
    if (width == 1)
        // return sprintf(buffer, " %02x", *data);
    {
        buffer[0] = ' ';
        buffer[1] = g_translation_table[(*data) >> 4];
        buffer[2] = g_translation_table[(*data) & ADK_TRANS_MASK];
        return 3;
    }
    else if (width == 2)
        // return sprintf(buffer, " %02x%02x", data[0], data[1]);
    {
        buffer[0] = ' ';
        buffer[1] = g_translation_table[(data[0]) >> 4];
        buffer[2] = g_translation_table[(data[0]) & ADK_TRANS_MASK];
        buffer[3] = g_translation_table[(data[1]) >> 4];
        buffer[4] = g_translation_table[(data[1]) & ADK_TRANS_MASK];
        return 5;
    }
    else if (width == 4)
        // return sprintf(buffer, " %02x%02x%02x%02x", data[0], data[1], data[2], data[3]);
    {
        buffer[0] = ' ';
        buffer[1] = g_translation_table[(data[0]) >> 4];
        buffer[2] = g_translation_table[(data[0]) & ADK_TRANS_MASK];
        buffer[3] = g_translation_table[(data[1]) >> 4];
        buffer[4] = g_translation_table[(data[1]) & ADK_TRANS_MASK];
        buffer[5] = g_translation_table[(data[2]) >> 4];
        buffer[6] = g_translation_table[(data[2]) & ADK_TRANS_MASK];
        buffer[7] = g_translation_table[(data[3]) >> 4];
        buffer[8] = g_translation_table[(data[3]) & ADK_TRANS_MASK];
        return 9;
    }
    else if (width == 8)
        // return sprintf(buffer, " %02x%02x%02x%02x%02x%02x%02x%02x",
        //                data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
    {
        buffer[0] = ' ';
        buffer[1] = g_translation_table[(data[0]) >> 4];
        buffer[2] = g_translation_table[(data[0]) & ADK_TRANS_MASK];
        buffer[3] = g_translation_table[(data[1]) >> 4];
        buffer[4] = g_translation_table[(data[1]) & ADK_TRANS_MASK];
        buffer[5] = g_translation_table[(data[2]) >> 4];
        buffer[6] = g_translation_table[(data[2]) & ADK_TRANS_MASK];
        buffer[7] = g_translation_table[(data[3]) >> 4];
        buffer[8] = g_translation_table[(data[3]) & ADK_TRANS_MASK];
        buffer[9] = g_translation_table[(data[4]) >> 4];
        buffer[10] = g_translation_table[(data[4]) & ADK_TRANS_MASK];
        buffer[11] = g_translation_table[(data[5]) >> 4];
        buffer[12] = g_translation_table[(data[5]) & ADK_TRANS_MASK];
        buffer[13] = g_translation_table[(data[6]) >> 4];
        buffer[14] = g_translation_table[(data[6]) & ADK_TRANS_MASK];
        buffer[15] = g_translation_table[(data[7]) >> 4];
        buffer[16] = g_translation_table[(data[7]) & ADK_TRANS_MASK];
        return 17;
    }

    assert(false);
    return 0;
}

#undef ADK_TRANS_MASK

typedef uint32_t (*PrintFuncType)(char*, const unsigned char*);

struct PrintTable
{
    PrintFuncType print_func;
    const char* place_holder;
    const uint32_t place_holder_size;
};

static const struct PrintTable g_ptable[4] = {
    {&(DisplayFunc<1>), "   ", sizeof("   ") - 1},
    {&(DisplayFunc<2>), "     ", sizeof("     ") - 1},
    {&(DisplayFunc<4>), "         ", sizeof("         ") - 1},
    {&(DisplayFunc<8>), "                 ", sizeof("                 ") - 1}
};

// Note: thread unsafe!
ADK_API const char*
MemoryHexDump(const void* addr, uint32_t len, uint32_t step = 1, uint32_t width = 16);

template<typename T>
class GlobalPointerInitializer
{
public:
    GlobalPointerInitializer(T*& ptr)
    {
        ptr = new T();
    }

    template<typename PostJob>
    GlobalPointerInitializer(T*& ptr, const PostJob& job)
    {
        ptr = new T();
        job();
    }
};


#define ADK_INIT_GPTR_1(type, ptr) static adk_impl::GlobalPointerInitializer<type> \
    ADK_CONCATENATE(s_gptr_init_##type, __LINE__)(ptr);
#define ADK_INIT_GPTR_2(type, ptr, pjob) static adk_impl::GlobalPointerInitializer<type> \
    ADK_CONCATENATE(s_gptr_init_##type, __LINE__)(ptr, pjob);

#define ADK_INIT_GPTR_(N, type, ...) ADK_CONCATENATE(ADK_INIT_GPTR_, N)(type, __VA_ARGS__)
#define ADK_INIT_GPTR(type, ...) ADK_INIT_GPTR_(ADK_GET_NARG(__VA_ARGS__), type, __VA_ARGS__)

extern int32_t EnableShareMemoryDump(const char* env_name = "ADK_ENABLE_SHM_DUMP");

const std::string& GetLoginUserName();
const std::string& GetLoginUserHome();


template<typename T, typename ...ArgType>
T* aligned_new(size_t align_bytes, ArgType&&... args)
{
    assert(align_bytes > 0);
    void* buf = memalign(align_bytes, sizeof(T));
    return new (buf) T(std::forward<ArgType>(args)...);
}

template<typename T, typename ...ArgType>
T* cache_aligned_new(ArgType&&... args)
{
    void* buf = memalign(ADK_CACHE_LINE_SIZE, sizeof(T));
    return new (buf) T(std::forward<ArgType>(args)...);
}

void WaitPidUntil(pid_t pid, uint64_t timeout);

/**
 * @brief      统计延迟的帮助类
 *
 * @note       该接口可用于一些分析工具，不可用于业务处理
 */
struct LatencyStatistics
{
    /**
     * @brief      构造函数
     *
     * @param[in]  capacity  每秒内的时延数据缓存容量
     * @param[in]  delay_us  线程安全相关的延迟时间，一般不需要修改
     */
    LatencyStatistics(uint64_t capacity, uint32_t delay_us = 50000);

    /**
     * @brief      时延数据排序使用
     */
    static int CompareLatency(const void *a, const void *b)
    {
        const uint64_t a_v = *reinterpret_cast<const uint64_t*>(a);
        const uint64_t b_v = *reinterpret_cast<const uint64_t*>(b);
        return int(a_v - b_v);
    }

    /**
     * @brief      保存时延数据，单一线程使用
     *
     * @param      ts_begin  起始采样点
     * @param      ts_end    结束采样点
     */
    inline void Save(struct timespec& ts_begin, struct timespec& ts_end)
    {
        if (ADK_UNLIKELY(is_need_reset_))
        {
            nr_saved_[current_] = 1;
            is_need_reset_ = false;
        }

        register uint64_t current = current_;
        uint64_t& nr_saved = nr_saved_[current];

        auto* save = save_[current];

        save[nr_saved] = ts_end.tv_sec - ts_begin.tv_sec;
        save[nr_saved + 1] = ts_end.tv_nsec - ts_begin.tv_nsec;
        nr_saved = nr_saved + 2;
    }

    inline void Save(int64_t ts_begin, int64_t ts_end)
    {
        if (ADK_UNLIKELY(is_need_reset_))
        {
            nr_saved_[current_] = 1;
            is_need_reset_ = false;
        }

        register uint64_t current = current_;
        uint64_t& nr_saved = nr_saved_[current];

        auto* save = save_[current];

        save[nr_saved] = 0;
        save[nr_saved + 1] = ts_end - ts_begin;
        nr_saved = nr_saved + 2;
    }

    /**
     * @brief      对最近一段时间收集到的采样点计算指标，比较耗费性能，建议每秒调用1次，
     *             该函数内会使用构造函数 LatencyStatistics 的第二个输入参数进行睡眠
     *
     * @return     成功时返回true，此时方可调用获取指标的一系列函数 GetMax()...GetAvg() 等
     */
    bool Calculate();

    /**
     * @brief      获取 Calculate 计算出的最大值
     *
     * @return     最大值
     */
    uint64_t GetMax();

    /**
     * @brief      获取 Calculate 计算出的最小值
     *
     * @return     最小值
     */
    uint64_t GetMin();

    /**
     * @brief      获取 Calculate 计算出的百分数
     *
     * @param[in]  percent  百分数，例如，获取中位数可输入 0.5
     *
     * @return     百分数对应的时延值
     */
    uint64_t GetPercentNumber(float percent);

    /**
     * @brief      获取 Calculate 计算出的均值
     *
     * @return     均值
     */
    uint64_t GetAvg();

    /**
     * @brief      获取 Calculate 计算时所使用的采样记录数量
     *
     * @return     采样记录数量
     */
    uint64_t GetNumberRecords();

    /**
     * @brief      获取构造函数 LatencyStatistics 的第二个入参，即 Calculate 每次调用的睡眠时间
     *
     * @return     LatencyStatistics 的第二个入参
     */
    uint32_t delay_us() { return delay_us_; }

    /**
     * @brief      获取格式化过的统计指标，一般用于输出
     *
     * @return     格式化后的统计指标
     */
    std::string GetDisplayString();

    /**
     * @brief      以 interval_micro 为周期自动计算时延并输出到终端
     *
     * @param[in]  interval_micro  计算周期
     */
    void PeriodDisplayStatistic(uint32_t interval_micro = 1000000);

    /**
     * @brief      停止 PeriodDisplayStatistic 工作
     */
    void Stop() { is_running_ = false; }

    volatile bool is_running_ = true;
    uint32_t  delay_us_;
    bool      is_need_reset_ = false;
    uint64_t  current_ = 0;
    uint64_t  nr_saved_[2] = {1, 1};
    uint64_t  avg_[2] = {0, 0};
    int64_t*  save_[2];
    std::ostringstream oss_;
};

template<typename T, uint32_t instance_id>
static T& LocalGetInstance()
{
    static T* s_l_instance_ptr = new T();
    return *s_l_instance_ptr;
}

/**
 * @brief 使用本模板类，可以在全局变量区构造指定的T类型，通过此类构造的类型，在main函数结束时，不会被析构。所有构造T类型需要的参考，可以通过构造函数完美转发。
 * @tparam T 想要构造的类型
 * @example  GlobalHolder<X> myobj(3);
 */
template<class T>
struct GlobalHolder
{
    union holder
    {
        holder(){};
        ~holder(){};
        T obj;
    } inst;
public:
    template<class... Args>
    GlobalHolder(Args&&... args)
    {
        new (&(inst.obj)) T(std::forward<Args>(args)...);
    }

    T* operator ->()
    {
        return &(inst.obj);
    }

    T& operator *()
    {
        return inst.obj;
    }

    ~GlobalHolder()
    {
        //Do nothing, global lifetime
    }
};


/**
 * @brief      比较两个json，如果相等，返回true，否则返回false
 *
 * @param[in]  json_value_1  第一个json
 *
 * @param[in]  json_value_2  第二个json
 *
 * @return     如果两个json相等，则返回true; 否则，则返回false
 */
bool CompareTwoJson(const nlohmann::json& json_value_1, const nlohmann::json& json_value_2);

/**
 * @brief      比较两个string，如果它们转换成的json相等，返回true，否则返回false
 *
 * @param[in]  json_string_value_1  第一个字符串
 *
 * @param[in]  json_string_value_2  第二个字符串
 *
 * @return     如果两个字符串转换成的json相等，则返回true; 否则，则返回false
 */
bool CompareTwoJson(const std::string& json_string_value_1, const std::string& json_string_value_2);

/**
 * @brief      比较一个string和一个json，如果string转换成的json和后者相等，返回true，否则返回false
 *
 * @param[in]  json_string_value_1  第一个字符串
 *
 * @param[in]  json_value_2         第二个json
 *
 * @return     如果字符串转换成的json和第二个json相等，则返回true; 否则，则返回false
 */
bool CompareTwoJson(const std::string& json_string_value_1, const nlohmann::json& json_value_2);

} // adk

#endif // ADK_UTIL_H_
