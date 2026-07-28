#define BOOST_TEST_MODULE convert
#include <boost/test/included/unit_test.hpp>

#include <stdlib.h>

#include <iostream>

#include <string>
#include <boost/format.hpp>
#include <adk_pack/convert.h>

using namespace adk;

BOOST_AUTO_TEST_CASE(adk_convert)
{
    for (uint32_t index=0; index<1000; ++index)
    {
        const uint16_t rand_num = random() % 10000;
        const std::string string_num = (boost::format("%04d") % rand_num).str();
        const uint16_t convert = DecimalConvert::String4ToInt(string_num.c_str());
        
        BOOST_REQUIRE(rand_num == convert);
    }

    for (uint32_t index=0; index<1000; ++index)
    {
        const uint32_t rand_num = random() % 100000000;
        const std::string string_num = (boost::format("%08d") % rand_num).str();
        const uint32_t convert = DecimalConvert::StringToInt(string_num.c_str(), 8);

        BOOST_REQUIRE(rand_num == convert);
    }

    const uint32_t kStringCount = 10000;
    std::string* rand_num_str = new std::string[kStringCount];
    for (uint32_t index=0; index<kStringCount; ++index)
    {
        const uint32_t rand_num = random() % 100000000;
        rand_num_str[index] = (boost::format("%08d") % rand_num).str();
    }

}