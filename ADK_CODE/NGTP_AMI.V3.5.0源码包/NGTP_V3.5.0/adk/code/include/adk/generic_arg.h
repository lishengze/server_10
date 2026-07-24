#ifndef ADK_IMPL_GENERIC_ARG_H_
#define ADK_IMPL_GENERIC_ARG_H_

#include <string>
#include <boost/any.hpp>

namespace adk_impl
{

struct GenericArg
{
    GenericArg(const std::string& name)
    {
        arg_name = name;
    }

    template<typename T>
    GenericArg& operator=(T value)
    {
        arg_value = value;
        return *this;
    }

    bool operator==(const std::string& value) const
    {
        return arg_name == value;
    }

    #define ADK_TRY(T) do {  \
        bool is_succ = false;   \
        try {   \
            ret = boost::any_cast<T>(arg_value);        \
            is_succ = true; \
        } catch (...) {}    \
        if (is_succ)    \
            return ret; \
    } while (false)

    template<typename T>
    T any_cast() const
    {
        T ret;
        ADK_TRY(T);
        ADK_TRY(int32_t);
        ADK_TRY(uint32_t);
        ADK_TRY(int64_t);
        ADK_TRY(uint64_t);
        ADK_TRY(int16_t);
        ADK_TRY(uint16_t);
        ret = boost::any_cast<T>(arg_value);
        return ret;
    }

    #undef ADK_TRY

    std::string arg_name;
    boost::any arg_value;
};

} // adk

#endif // ADK_GENERIC_ARG_H_
