#include <adk/term_text_colors.h>

#include <iostream>

using namespace adk;

int main(int argc, char const *argv[])
{
    std::cout << ADK_RED_TXT("hello world!") << std::endl;
    std::cout << ADK_GREEN_TXT("hello world!") << std::endl;
    std::cout << ADK_YELLOW_TXT("hello world!") << std::endl;
    std::cout << ADK_BLUE_TXT("hello world!") << std::endl;
    std::cout << ADK_PURPLE_TXT("hello world!") << std::endl;
    std::cout << ADK_CYAN_TXT("hello world!") << std::endl;
    std::cout << ADK_BOLD_TXT("hello world!") << std::endl;
    std::cout << ADK_UNDERLINE_TXT("hello world!") << std::endl;
    return 0;
}

