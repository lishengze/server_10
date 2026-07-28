/**
 * @author     zhaonan, zhaonan@archforce.com.cn
 * @date       2017/12/18
 * @brief      helper functions and macros for unit test
 */
#ifndef ADK_IMPL_UNIT_TEST_HELPER_H_
#define ADK_IMPL_UNIT_TEST_HELPER_H_

#include "arch/generic.h"

#include <boost/any.hpp>
#include <boost/function.hpp>
#include <boost/utility/string_ref.hpp>

namespace adk_impl
{

#define ADK_EXECUTION_RETURN false
#define ADK_EXECUTION_CONTINUE true

namespace unittest 
{
const auto kAlwaysTrue = [](bool& exp_val, const boost::any&){
    exp_val = true; 
};
const auto kAlwaysFalse = [](bool& exp_val, const boost::any&){
    exp_val = false; 
};
const auto kAlwaysIgnore = [](bool& exp_val, const boost::any&){};

const auto kAlwaysContinue = [](const boost::any&, boost::any&){
    return ADK_EXECUTION_CONTINUE;
};
const auto kAlwaysReturn = [](const boost::any&, boost::any&){
    return ADK_EXECUTION_RETURN;
};
extern boost::any place_holder;
}

typedef boost::function<void (bool&, const boost::any&)> ExpCheckPointHandler;
typedef boost::function<bool (const boost::any&, boost::any&)> ExeCheckPointHandler;
extern void ExpressionCheckPoint(const boost::string_ref& exp_name, bool& exp_val);
extern void ExpressionCheckPoint(const boost::string_ref& exp_name, 
                                 bool& exp_val,
                                 const boost::any& exp_ctx);
extern bool ReturnCheckPoint(const boost::string_ref& exp_name, const boost::any&, boost::any&);
extern void RegExpHandler(const boost::string_ref& exp_name, const ExpCheckPointHandler& hdl);
extern void RegRetHandler(const boost::string_ref& exp_name, const ExeCheckPointHandler& hdl);

#ifdef __ADK_UNIT_TEST__

#define ADK_MOCKABLE_METHOD    virtual
#define ADK_MOCKABLE_EXPRESSION_2(exp_name, exp) ({   \
    bool exp_val = !!(exp); \
    adk_impl::ExpressionCheckPoint(exp_name, exp_val, adk_impl::unittest::place_holder);   \
    exp_val;    \
})

#define ADK_MOCKABLE_EXPRESSION_3(exp_name, exp, ctx) ({   \
    bool exp_val = !!(exp); \
    adk_impl::ExpressionCheckPoint(exp_name, exp_val, ctx);   \
    exp_val;    \
})

#define ADK_MOCKABLE_RETURN_1(cp_name) do { \
    if (!adk_impl::ReturnCheckPoint(cp_name, adk_impl::unittest::place_holder, adk_impl::unittest::place_holder))    \
    {   \
        return ;  \
    }   \
} while (false)

#define ADK_MOCKABLE_RETURN_2(cp_name, exe_ctx) do { \
    if (!adk_impl::ReturnCheckPoint(cp_name, exe_ctx, adk_impl::unittest::place_holder))    \
    {   \
        return ;  \
    }   \
} while (false)

#define ADK_MOCKABLE_RETURN_3(cp_name, exe_ctx, type) do { \
    boost::any ret; \
    if (!adk_impl::ReturnCheckPoint(cp_name, exe_ctx, ret))    \
    {   \
        return boost::any_cast<type>(ret);  \
    }   \
} while (false)

#else

#define ADK_MOCKABLE_METHOD  
#define ADK_MOCKABLE_EXPRESSION_2(exp_name, exp) exp
#define ADK_MOCKABLE_EXPRESSION_3(exp_name, exp, ctx)   exp
#define ADK_MOCKABLE_RETURN_1(cp_name)
#define ADK_MOCKABLE_RETURN_2(cp_name, type)
#define ADK_MOCKABLE_RETURN_3(cp_name, type, exe_ctx)

#endif

#define ADK_MOCKABLE_EXPRESSION_(N, ...) ADK_CONCATENATE(ADK_MOCKABLE_EXPRESSION_, N)(__VA_ARGS__)
#define ADK_MOCKABLE_EXPRESSION(...) ADK_MOCKABLE_EXPRESSION_(ADK_GET_NARG(__VA_ARGS__), __VA_ARGS__)

#define ADK_MOCKABLE_RETURN_(N, ...) ADK_CONCATENATE(ADK_MOCKABLE_RETURN_, N)(__VA_ARGS__)
#define ADK_MOCKABLE_RETURN(...) ADK_MOCKABLE_RETURN_(ADK_GET_NARG(__VA_ARGS__), __VA_ARGS__)

} // adk

#endif // ADK_UNIT_TEST_HELPER_H_
