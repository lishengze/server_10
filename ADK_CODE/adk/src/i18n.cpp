#include <adk/i18n.h>

namespace adk_impl
{

static std::locale* GenLocale(const std::string& locale)
{
    boost::locale::generator gen;
    char* path = getenv("TRANSLATER_PATH");
    gen.add_messages_path(path ? path : ".");
    gen.add_messages_domain("translate");
    return new std::locale(gen(locale));
}

std::locale* get_locale()
{
    static std::locale* s_locale = GenLocale("zh_CN.UTF-8");
    return s_locale;
}


} // namespace adk_impl
