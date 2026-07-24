#define BOOST_TEST_MODULE rate_change_controller
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk/simple_rate_controller.h>
#include <adk/error_code.h>
#include <adk/util.h>

#include <set>
#include <string>
#include <vector>
#include <map>

using namespace adk;

BOOST_AUTO_TEST_CASE(limit_100_200)
{
    SimpleVariableRateCtrl* change_rate_ctrl = new SimpleVariableRateCtrl(100, 200);
    BOOST_REQUIRE(change_rate_ctrl->impl_ != nullptr);
    struct timespec ts_begin, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_begin);
    for (uint32_t i = 0; i < 100 * 10; ++i)
    {
        change_rate_ctrl->Wait();
    }
    clock_gettime(CLOCK_MONOTONIC, &ts_end);   
    int32_t time_diff_milli = ::abs((adk::time_diff(ts_end, ts_begin) / (1000*1000)) - 10*1000);
    BOOST_REQUIRE_GT(time_diff_milli, 98);
    BOOST_REQUIRE_LT(time_diff_milli, 6000);
}

BOOST_AUTO_TEST_CASE(limit_1000_2000)
{
    SimpleVariableRateCtrl* change_rate_ctrl = new SimpleVariableRateCtrl(1000, 2000);
    BOOST_REQUIRE(change_rate_ctrl->impl_ != nullptr);
    struct timespec ts_begin, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_begin);
    for (uint32_t i = 0; i < 1000 * 10; ++i)
    {
        change_rate_ctrl->Wait();
    }
    clock_gettime(CLOCK_MONOTONIC, &ts_end);   
    int32_t time_diff_milli = ::abs((adk::time_diff(ts_end, ts_begin) / (1000*1000)) - 10*1000);
    BOOST_REQUIRE_GT(time_diff_milli, 98);
    BOOST_REQUIRE_LT(time_diff_milli, 6000);
}

BOOST_AUTO_TEST_CASE(limit_10000_20000)
{
    SimpleVariableRateCtrl* change_rate_ctrl = new SimpleVariableRateCtrl(10000, 20000);
    BOOST_REQUIRE(change_rate_ctrl->impl_ != nullptr);
    struct timespec ts_begin, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_begin);
    for (uint32_t i = 0; i < 10000 * 10; ++i)
    {
        change_rate_ctrl->Wait();
    }
    clock_gettime(CLOCK_MONOTONIC, &ts_end);   
    int32_t time_diff_milli = ::abs((adk::time_diff(ts_end, ts_begin) / (1000*1000)) - 10*1000);
    BOOST_REQUIRE_GT(time_diff_milli, 98);
    BOOST_REQUIRE_LT(time_diff_milli, 6000);
}

BOOST_AUTO_TEST_CASE(error_args)
{
    SimpleVariableRateCtrl* change_rate_ctrl = new SimpleVariableRateCtrl(200, 100);
    BOOST_REQUIRE(change_rate_ctrl->impl_ == nullptr);
}
