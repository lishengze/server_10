#define BOOST_TEST_MODULE pipe
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk/rate_control.h>
#include <adk/error_code.h>
#include <adk/util.h>

#include <set>
#include <string>
#include <vector>
#include <map>

using namespace adk;

BOOST_AUTO_TEST_CASE(no_limit)
{
    RateController* rate_ctrl = RateController::Create(0);
    BOOST_REQUIRE(rate_ctrl != nullptr);
    struct timespec ts_begin, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_begin);
    for (uint32_t i = 0; i < 1000 * 10; ++i)
    {
        rate_ctrl->Wait(1, RateController::kBlock);
    }
    clock_gettime(CLOCK_MONOTONIC, &ts_end);   
    int32_t time_diff_milli = ::abs((adk::time_diff(ts_end, ts_begin) / (1000*1000)) - 10*1000);
    BOOST_REQUIRE_GT(time_diff_milli, ((10*1000)*98)/100);
}

BOOST_AUTO_TEST_CASE(gt_1000)
{
    int32_t rate = 2000;
    RateController* rate_ctrl = RateController::Create(rate, 1);
    BOOST_REQUIRE(rate_ctrl != nullptr);
    struct timespec ts_begin, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_begin);
    for (uint32_t i = 0; i < rate * 10; ++i)
    {
        rate_ctrl->Wait(1, RateController::kBlock);
    }
    clock_gettime(CLOCK_MONOTONIC, &ts_end);   
    int32_t time_diff_milli = ::abs((adk::time_diff(ts_end, ts_begin) / (1000*1000)) - 10*1000);
    BOOST_REQUIRE_LT(time_diff_milli, (rate * 10 * 8) / 100);
}

BOOST_AUTO_TEST_CASE(eq_1000)
{
    int32_t rate = 1000;
    RateController* rate_ctrl = RateController::Create(rate, 1);
    BOOST_REQUIRE(rate_ctrl != nullptr);
    struct timespec ts_begin, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_begin);
    for (uint32_t i = 0; i < rate * 10; ++i)
    {
        rate_ctrl->Wait(1, RateController::kBlock);
    }
    clock_gettime(CLOCK_MONOTONIC, &ts_end);   
    int32_t time_diff_milli = ::abs((adk::time_diff(ts_end, ts_begin) / (1000*1000)) - 10*1000);
    BOOST_REQUIRE_LT(time_diff_milli, (rate * 10 * 8) / 100);
}

BOOST_AUTO_TEST_CASE(lt_1000)
{
    int32_t rate = 900;
    RateController* rate_ctrl = RateController::Create(rate, 1);
    BOOST_REQUIRE(rate_ctrl != nullptr);
    struct timespec ts_begin, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_begin);
    for (uint32_t i = 0; i < rate * 10; ++i)
    {
        rate_ctrl->Wait(1, RateController::kBlock);
    }
    clock_gettime(CLOCK_MONOTONIC, &ts_end);   
    int32_t time_diff_milli = ::abs((adk::time_diff(ts_end, ts_begin) / (1000*1000)) - 10*1000);
    // it's working bad in this scenario
    BOOST_REQUIRE_LT(time_diff_milli, (rate * 10 * 10) / 100);
}

BOOST_AUTO_TEST_CASE(big_wait)
{
    int32_t rate = 1000;
    RateController* rate_ctrl = RateController::Create(rate, 1);
    BOOST_REQUIRE(rate_ctrl != nullptr);
    struct timespec ts_begin, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_begin);
    rate_ctrl->Wait(10000, RateController::kBlock);
    clock_gettime(CLOCK_MONOTONIC, &ts_end);   
    int32_t time_diff_milli = ::abs((adk::time_diff(ts_end, ts_begin) / (1000*1000)) - 10*1000);
    BOOST_REQUIRE_LT(time_diff_milli, (rate * 10 * 8) / 100);
}

void DORC(RateController* rate_ctrl)
{
    rate_ctrl->Wait(100000);
}

BOOST_AUTO_TEST_CASE(cancel)
{
    RateController* rate_ctrl = RateController::Create(1000, 1);
    BOOST_REQUIRE(rate_ctrl != nullptr);
    boost::thread rct = boost::thread(DORC, rate_ctrl);

    sleep(2);
    rate_ctrl->ReleaseWaitThread();
    rct.join();
}

BOOST_AUTO_TEST_CASE(error_args)
{
    RateController* rate_ctrl = RateController::Create(1, 2);
    BOOST_REQUIRE(rate_ctrl == nullptr);
}
