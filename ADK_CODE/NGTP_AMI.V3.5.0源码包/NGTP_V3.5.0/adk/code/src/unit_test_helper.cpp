#include <map>

#include <boost/thread/mutex.hpp>

#include <adk/unit_test_helper.h>

namespace adk_impl
{

namespace unittest
{
boost::any place_holder;    
}

std::map<boost::string_ref, ExpCheckPointHandler> g_exp_check_point_map;
boost::mutex g_check_point_mutex;
void ExpressionCheckPoint(const boost::string_ref& exp_name, 
                          bool& exp_val,
                          const boost::any& exp_ctx)
{
    boost::mutex::scoped_lock lock_guard(g_check_point_mutex);
    auto it = g_exp_check_point_map.find(exp_name);
    if (it != g_exp_check_point_map.end())
    {
        (it->second)(exp_val, exp_ctx);
    }
}

void RegExpHandler(const boost::string_ref& exp_name, const ExpCheckPointHandler& hdl)
{
    boost::mutex::scoped_lock lock_guard(g_check_point_mutex);
    auto it = g_exp_check_point_map.find(exp_name);
    if (it != g_exp_check_point_map.end())
    {
        it->second = hdl;
    }
    else
    {
        std::string* exp_name_str = new std::string(exp_name);
        g_exp_check_point_map.insert(std::make_pair(boost::string_ref(*exp_name_str), hdl));
    }
}

std::map<boost::string_ref, ExeCheckPointHandler> g_exe_check_point_map;
boost::mutex g_exe_check_point_mutex;
bool ReturnCheckPoint(const boost::string_ref& exp_name, const boost::any& exe_ctx, boost::any& exe_ret)
{
    boost::mutex::scoped_lock lock_guard(g_exe_check_point_mutex);
    auto it = g_exe_check_point_map.find(exp_name);
    if (it != g_exe_check_point_map.end())
    {
        return (it->second)(exe_ctx, exe_ret);
    }
    return true;
}

void RegRetHandler(const boost::string_ref& exp_name, const ExeCheckPointHandler& hdl)
{
    boost::mutex::scoped_lock lock_guard(g_exe_check_point_mutex);
    auto it = g_exe_check_point_map.find(exp_name);
    if (it != g_exe_check_point_map.end())
    {
        it->second = hdl;
    }
    else
    {
        std::string* exp_name_str = new std::string(exp_name);
        g_exe_check_point_map.insert(std::make_pair(boost::string_ref(*exp_name_str), hdl));
    }
}

} // adk

