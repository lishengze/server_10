#ifndef ADK_IMPL_TERM_TEXT_COLORS_H_
#define ADK_IMPL_TERM_TEXT_COLORS_H_

#include <string>

namespace adk_impl
{
// Foreground colors
#define ADK_FG_BLACK          "\033[0;30m"
#define ADK_FG_RED            "\033[0;31m"
#define ADK_FG_GREEN          "\033[0;32m"
#define ADK_FG_YELLOW         "\033[0;33m"
#define ADK_FG_BLUE           "\033[0;34m"
#define ADK_FG_PURPLE         "\033[0;35m"
#define ADK_FG_CYAN           "\033[0;36m"
#define ADK_FG_LIGHT_GREY     "\033[0;37m"
#define ADK_FG_DARK_GREY      "\033[1;30m"
#define ADK_FG_LIGHT_BLUE     "\033[1;34m"
#define ADK_FG_LIGHT_GREEN    "\033[1;32m"
#define ADK_FG_LIGHT_CYAN     "\033[1;36m"
#define ADK_FG_LIGHT_RED      "\033[1;31m"
#define ADK_FG_LIGHT_PURPLE   "\033[1;35m"
#define ADK_FG_LIGHT_YELLOW   "\033[1;33m"
#define ADK_FG_WHITE          "\033[1;37m"

// Background colors
#define ADK_BG_BLACK          "\033[0;40m"
#define ADK_BG_BLUE           "\033[0;44m"
#define ADK_BG_GREEN          "\033[0;42m"
#define ADK_BG_CYAN           "\033[0;46m"
#define ADK_BG_RED            "\033[0;41m"
#define ADK_BG_PURPLE         "\033[0;45m"
#define ADK_BG_YELLOW         "\033[0;43m"
#define ADK_BG_LIGHT_GREY     "\033[0;47m"
#define ADK_BG_DARK_GREY      "\033[1;40m"
#define ADK_BG_LIGHT_BLUE     "\033[1;44m"
#define ADK_BG_LIGHT_GREEN    "\033[1;42m"
#define ADK_BG_LIGHT_CYAN     "\033[1;46m"
#define ADK_BG_LIGHT_RED      "\033[1;41m"
#define ADK_BG_LIGHT_PURPLE   "\033[1;45m"
#define ADK_BG_LIGHT_YELLOW   "\033[1;43m"
#define ADK_BG_WHITE          "\033[1;47m"

// Controls
#define ADK_DEFAULT           "\033[0m"
#define ADK_BOLD              "\033[1m"
#define ADK_UNDERLINE         "\033[4m"
#define ADK_BLINK             "\033[5m"
#define ADK_INVERSE           "\033[7m"
#define ADK_CONCEALED         "\033[8m"


#define ADK_RED_TXT(x)        std::string(ADK_FG_RED) + (x) + std::string(ADK_DEFAULT)
#define ADK_GREEN_TXT(x)      std::string(ADK_FG_GREEN) + (x) + std::string(ADK_DEFAULT)
#define ADK_YELLOW_TXT(x)     std::string(ADK_FG_YELLOW) + (x) + std::string(ADK_DEFAULT)
#define ADK_BLUE_TXT(x)       std::string(ADK_FG_BLUE) + (x) + std::string(ADK_DEFAULT)
#define ADK_PURPLE_TXT(x)     std::string(ADK_FG_PURPLE) + (x) + std::string(ADK_DEFAULT)
#define ADK_CYAN_TXT(x)       std::string(ADK_FG_CYAN) + (x) + std::string(ADK_DEFAULT)
#define ADK_BOLD_TXT(x)       std::string(ADK_BOLD) + (x) + std::string(ADK_DEFAULT)
#define ADK_UNDERLINE_TXT(x)  std::string(ADK_UNDERLINE) + (x) + std::string(ADK_DEFAULT)

} // adk

#endif // ADK_TERM_TEXT_COLORS_H_
