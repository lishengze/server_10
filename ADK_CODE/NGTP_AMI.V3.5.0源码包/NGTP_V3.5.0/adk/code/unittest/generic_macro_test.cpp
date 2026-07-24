//
// Created by lzn on 12/9/19.
//
#define BOOST_TEST_MODULE generic_macro
#include <boost/test/included/unit_test.hpp>

#include "adk/arch/generic.h"



BOOST_AUTO_TEST_CASE(args_count_macro)
{
    int result1 = ADK_GET_NARG(-1);
    int result2 = ADK_GET_NARG(-1, -2);
    int result4 = ADK_GET_NARG(-1, -2, -3, -4);
    int result8 = ADK_GET_NARG(-1, -2, -3, -4, -5, -6, -7, -8);
    int result16 = ADK_GET_NARG(-1, -2, -3, -4, -5, -6, -7, -8, -1, -2, -3, -4, -5, -6, -7, -8);
    int result32 = ADK_GET_NARG(
            -1, -2, -3, -4, -5, -6, -7, -8,
            -1, -2, -3, -4, -5, -6, -7, -8,
            -1, -2, -3, -4, -5, -6, -7, -8,
            -1, -2, -3, -4, -5, -6, -7, -8);

    BOOST_REQUIRE(result1 == 1);
    BOOST_REQUIRE(result2 == 2);
    BOOST_REQUIRE(result4 == 4);
    BOOST_REQUIRE(result8 == 8);
    BOOST_REQUIRE(result16 == 16);
    BOOST_REQUIRE(result32 == 32);
}