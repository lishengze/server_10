#include <stdlib.h>

#include <iostream>

#include <string>
#include <boost/format.hpp>

#include <adk/arch/generic.h>
#include <adk_pack/convert.h>
using namespace adk;

int main(int argc, char* argv[])
{
    for (uint32_t index=0; index<1000; ++index)
    {
        const uint16_t rand_num = random() % 10000;
        const std::string string_num = (boost::format("%04d") % rand_num).str();
        const uint16_t convert = DecimalConvert::String4ToInt(string_num.c_str());

        if (rand_num != convert)
        {
            std::cout << "String4ToInt bug"
                      << ", rand_num = " << string_num
                      << ", convert = " << convert
                      << std::endl;
        }
    }

    for (uint32_t index=0; index<1000; ++index)
    {
        const uint32_t rand_num = random() % 100000000;
        const std::string string_num = (boost::format("%08d") % rand_num).str();
        const uint32_t convert = DecimalConvert::StringToInt(string_num.c_str(), 8);

        if (rand_num != convert)
        {
            std::cout << "StringToInt bug"
                      << ", rand_num = " << string_num
                      << ", convert = " << convert
                      << std::endl;
        }
    }

    const uint32_t kStringCount = 10000;
    std::string* rand_num_str = new std::string[kStringCount];
    for (uint32_t index=0; index<kStringCount; ++index)
    {
        const uint32_t rand_num = random() % 100000000;
        rand_num_str[index] = (boost::format("%08d") % rand_num).str();
    }

    const uint32_t kTestCount = 100000;

    if (2 != argc)
    {
        std::cout << "Input test type: 1:atoi\t2:String8ToInt\t3:StringToIntRaw\t4:StringToInt<8>\t5:StringToInt" << std::endl;
        return 0;
    }

    const uint32_t test_type = atoi(argv[1]);

    uint32_t test_value = 0;

    switch (test_type)
    {
    case 1:
        {
            uint64_t begin0 = adk_impl::GetTSC();
            for (uint32_t test_index=0; test_index < kTestCount; ++test_index)
            {
                for (uint32_t index=0; index<kStringCount; ++index)
                {
                    ADK_BARRIER();
                    test_value = atoi(rand_num_str[index].c_str());
                }
            }
            uint64_t end0 = adk_impl::GetTSC();
            std::cout << "atoi() = \t\t" << end0-begin0 << std::endl;
        }
        break;
    case 2:
        {
            uint64_t begin1 = adk_impl::GetTSC();
            for (uint32_t test_index=0; test_index < kTestCount; ++test_index)
            {
                for (uint32_t index=0; index<kStringCount; ++index)
                {
                    ADK_BARRIER();
                    test_value = DecimalConvert::String8ToInt(rand_num_str[index].c_str());
                }
            }
            uint64_t end1 = adk_impl::GetTSC();

            std::cout << "String8ToInt() = \t" << end1-begin1 << std::endl;
        }
        break;
    case 3:
        {
            uint64_t begin2 = adk_impl::GetTSC();
            for (uint32_t test_index=0; test_index < kTestCount; ++test_index)
            {
                for (uint32_t index=0; index<kStringCount; ++index)
                {
                    ADK_BARRIER();
                    test_value = DecimalConvert::StringToIntRaw(rand_num_str[index].c_str(), 8);
                }
            }
            uint64_t end2 = adk_impl::GetTSC();

            std::cout << "StringToIntRaw() = \t" << end2-begin2 << std::endl;
        }
        break;
    case 4:
        {
            uint64_t begin3 = adk_impl::GetTSC();
            for (uint32_t test_index=0; test_index < kTestCount; ++test_index)
            {
                for (uint32_t index=0; index<kStringCount; ++index)
                {
                    ADK_BARRIER();
                    test_value = DecimalConvert::StringToInt<8>(rand_num_str[index].c_str());
                }
            }
            uint64_t end3 = adk_impl::GetTSC();

            std::cout << "StringToInt<8>() = \t" << end3-begin3 << std::endl;
        }
        break;
    case 5:
        {
            uint64_t begin4 = adk::GetTSC();
            for (uint32_t test_index=0; test_index < kTestCount; ++test_index)
            {
                for (uint32_t index=0; index<kStringCount; ++index)
                {
                    ADK_BARRIER();
                    test_value = DecimalConvert::StringToInt(rand_num_str[index].c_str(), 8);
                }
            }
            uint64_t end4 = adk::GetTSC();

            std::cout << "StringToInt() = \t" << end4-begin4 << std::endl;
        }
        break;
    default:
        std::cout << "Input test type: 1:atoi\t2:String8ToInt\t3:StringToIntRaw\t4:StringToInt<8>\t5:StringToInt" << std::endl;
    }

    return test_value;
}