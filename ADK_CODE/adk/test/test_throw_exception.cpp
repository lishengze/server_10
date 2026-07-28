#include <iostream>
#include <boost/exception/diagnostic_information.hpp>
#include <adk/exception.h>

int main(int argc, char const *argv[])
{
    try {
        ADK_THROW("hello world");
    } catch (...)
    {
        std::cout << boost::current_exception_diagnostic_information() << std::endl;
    }
    return 0;
}

