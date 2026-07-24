#include <stdlib.h>

#include <iostream>

#include <string>
#include <boost/format.hpp>

#include <adk/arch/generic.h>
#include <adk/convert.h>
using namespace adk;

using std::string;

int main(int argc, char* argv[])
{
    double double_value = 56465431657.64654687;

    // test case1
    {
        string str_value = (boost::format("%d") % (int64_t)double_value).str();
        std::cout << "test case1: original string = " << str_value << std::endl;

        int64_t value1 = FloatConvert::Convert<0>(str_value.c_str());
        std::cout << "coefficient = 0, value = " << value1 << std::endl;

        int64_t value2 = FloatConvert::Convert<2>(str_value.c_str());
        std::cout << "coefficient = 2, value = " << value2 << std::endl;

        int64_t value3 = FloatConvert::Convert<4>(str_value.c_str());
        std::cout << "coefficient = 4, value = " << value3 << std::endl;

        int64_t value4 = FloatConvert::Convert<8>(str_value.c_str());
        std::cout << "coefficient = 8, value = " << value4 << std::endl;
    }

    // test case2
    {
        string str_value = (boost::format("%.4f") % double_value).str();
        std::cout << "test case2: original string = " << str_value << std::endl;

        int64_t value1 = FloatConvert::Convert<0>(str_value.c_str());
        std::cout << "coefficient = 0, value = " << value1 << std::endl;

        int64_t value2 = FloatConvert::Convert<2>(str_value.c_str());
        std::cout << "coefficient = 2, value = " << value2 << std::endl;

        int64_t value3 = FloatConvert::Convert<4>(str_value.c_str());
        std::cout << "coefficient = 4, value = " << value3 << std::endl;

        int64_t value4 = FloatConvert::Convert<8>(str_value.c_str());
        std::cout << "coefficient = 8, value = " << value4 << std::endl;
    }

    // test case3
    {
        string str_value = (boost::format("%.8f") % double_value).str();
        std::cout << "test case3: original string = " << str_value << std::endl;

        int64_t value1 = FloatConvert::Convert<0>(str_value.c_str());
        std::cout << "coefficient = 0, value = " << value1 << std::endl;

        int64_t value2 = FloatConvert::Convert<2>(str_value.c_str());
        std::cout << "coefficient = 2, value = " << value2 << std::endl;

        int64_t value3 = FloatConvert::Convert<4>(str_value.c_str());
        std::cout << "coefficient = 4, value = " << value3 << std::endl;

        int64_t value4 = FloatConvert::Convert<8>(str_value.c_str());
        std::cout << "coefficient = 8, value = " << value4 << std::endl;
    }

    // test case4
    {
        string str_value = (boost::format("  -%.4fabcd") % double_value).str();
        std::cout << "test case4: original string = " << str_value << std::endl;        

        int64_t value1 = FloatConvert::Convert<0>(str_value.c_str());
        std::cout << "coefficient = 0, value = " << value1 << std::endl;

        int64_t value2 = FloatConvert::Convert<2>(str_value.c_str());
        std::cout << "coefficient = 2, value = " << value2 << std::endl;

        int64_t value3 = FloatConvert::Convert<4>(str_value.c_str());
        std::cout << "coefficient = 4, value = " << value3 << std::endl;

        int64_t value4 = FloatConvert::Convert<8>(str_value.c_str());
        std::cout << "coefficient = 8, value = " << value4 << std::endl;
    }

    // test case5
    {
        string str_value = (boost::format("    0000%.4fabcd") % double_value).str();
        std::cout << "test case5: original string = " << str_value << std::endl;        

        int64_t value1 = FloatConvert::Convert<0>(str_value.c_str());
        std::cout << "coefficient = 0, value = " << value1 << std::endl;

        int64_t value2 = FloatConvert::Convert<2>(str_value.c_str());
        std::cout << "coefficient = 2, value = " << value2 << std::endl;

        int64_t value3 = FloatConvert::Convert<4>(str_value.c_str());
        std::cout << "coefficient = 4, value = " << value3 << std::endl;

        int64_t value4 = FloatConvert::Convert<8>(str_value.c_str());
        std::cout << "coefficient = 8, value = " << value4 << std::endl;
    }

    // test case6
    {
        string str_value = (boost::format("    +%.4fabcd") % double_value).str();
        std::cout << "test case6: original string = " << str_value << std::endl;        

        int64_t value1 = FloatConvert::Convert<0>(str_value.c_str());
        std::cout << "coefficient = 0, value = " << value1 << std::endl;

        int64_t value2 = FloatConvert::Convert<2>(str_value.c_str());
        std::cout << "coefficient = 2, value = " << value2 << std::endl;

        int64_t value3 = FloatConvert::Convert<4>(str_value.c_str());
        std::cout << "coefficient = 4, value = " << value3 << std::endl;

        int64_t value4 = FloatConvert::Convert<8>(str_value.c_str());
        std::cout << "coefficient = 8, value = " << value4 << std::endl;
    }

    // test case7
    {
        string str_value = (boost::format("-efg%.4fabcd") % double_value).str();
        std::cout << "test case7: original string = " << str_value << std::endl;        

        int64_t value1 = FloatConvert::Convert<0>(str_value.c_str());
        std::cout << "coefficient = 0, value = " << value1 << std::endl;

        int64_t value2 = FloatConvert::Convert<2>(str_value.c_str());
        std::cout << "coefficient = 2, value = " << value2 << std::endl;

        int64_t value3 = FloatConvert::Convert<4>(str_value.c_str());
        std::cout << "coefficient = 4, value = " << value3 << std::endl;

        int64_t value4 = FloatConvert::Convert<8>(str_value.c_str());
        std::cout << "coefficient = 8, value = " << value4 << std::endl;
    }

    // test case8
    {
        string str_value = (boost::format("%.4f.6789") % double_value).str();
        std::cout << "test case8: original string = " << str_value << std::endl;        

        int64_t value1 = FloatConvert::Convert<0>(str_value.c_str());
        std::cout << "coefficient = 0, value = " << value1 << std::endl;

        int64_t value2 = FloatConvert::Convert<2>(str_value.c_str());
        std::cout << "coefficient = 2, value = " << value2 << std::endl;

        int64_t value3 = FloatConvert::Convert<4>(str_value.c_str());
        std::cout << "coefficient = 4, value = " << value3 << std::endl;

        int64_t value4 = FloatConvert::Convert<8>(str_value.c_str());
        std::cout << "coefficient = 8, value = " << value4 << std::endl;
    }

    // test case9
    {
        string str_value = "0.4646543.64654687";
        std::cout << "test case9: original string = " << str_value << std::endl;        

        int64_t value1 = FloatConvert::Convert<0>(str_value.c_str());
        std::cout << "coefficient = 0, value = " << value1 << std::endl;

        int64_t value2 = FloatConvert::Convert<2>(str_value.c_str());
        std::cout << "coefficient = 2, value = " << value2 << std::endl;

        int64_t value3 = FloatConvert::Convert<4>(str_value.c_str());
        std::cout << "coefficient = 4, value = " << value3 << std::endl;

        int64_t value4 = FloatConvert::Convert<8>(str_value.c_str());
        std::cout << "coefficient = 8, value = " << value4 << std::endl;
    }

    // test case10
    {
        string str_value = (boost::format("    0000-%.4fabcd") % double_value).str();
        std::cout << "test case10: original string = " << str_value << std::endl;        

        int64_t value1 = FloatConvert::Convert<0>(str_value.c_str());
        std::cout << "coefficient = 0, value = " << value1 << std::endl;

        int64_t value2 = FloatConvert::Convert<2>(str_value.c_str());
        std::cout << "coefficient = 2, value = " << value2 << std::endl;

        int64_t value3 = FloatConvert::Convert<4>(str_value.c_str());
        std::cout << "coefficient = 4, value = " << value3 << std::endl;

        int64_t value4 = FloatConvert::Convert<8>(str_value.c_str());
        std::cout << "coefficient = 8, value = " << value4 << std::endl;
    }

    // test case11
    {
        string str_value = (boost::format(" 5498451231-53424")).str();
        std::cout << "test case11: original string = " << str_value << std::endl;        

        int64_t value1 = FloatConvert::Convert<0>(str_value.c_str());
        std::cout << "coefficient = 0, value = " << value1 << std::endl;

        int64_t value2 = FloatConvert::Convert<2>(str_value.c_str());
        std::cout << "coefficient = 2, value = " << value2 << std::endl;

        int64_t value3 = FloatConvert::Convert<4>(str_value.c_str());
        std::cout << "coefficient = 4, value = " << value3 << std::endl;

        int64_t value4 = FloatConvert::Convert<8>(str_value.c_str());
        std::cout << "coefficient = 8, value = " << value4 << std::endl;
    }

    // test case12
    {
        string str_value = (boost::format(" 5498451231.-53424")).str();
        std::cout << "test case12: original string = " << str_value << std::endl;        

        int64_t value1 = FloatConvert::Convert<0>(str_value.c_str());
        std::cout << "coefficient = 0, value = " << value1 << std::endl;

        int64_t value2 = FloatConvert::Convert<2>(str_value.c_str());
        std::cout << "coefficient = 2, value = " << value2 << std::endl;

        int64_t value3 = FloatConvert::Convert<4>(str_value.c_str());
        std::cout << "coefficient = 4, value = " << value3 << std::endl;

        int64_t value4 = FloatConvert::Convert<8>(str_value.c_str());
        std::cout << "coefficient = 8, value = " << value4 << std::endl;
    }

    return 0;
}