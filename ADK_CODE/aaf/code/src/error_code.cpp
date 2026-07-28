/**
 * @file error_code.cpp
 * @brief 错误代码
 * @author Li Yunchong
 * @version 0.1
 * @date 2016-11-25
 */

#include <aaf/error_code.h>
#include <boost/locale/format.hpp>
#include <boost/locale/message.hpp>


namespace aaf
{

using boost::locale::translate;

const std::error_category& GetAafErrorCategory()
{
    static AafErrorCategory s_category;
    return s_category;
}

const char* AafErrorCategory::name() const noexcept
{
    return "aaf";
}

std::string AafErrorCategory::message(int ev) const
{
    switch(ev)
    {
    case kSuccess:
        return translate("Success").str();
    case kFailure:
        return translate("General Error").str();
    case kPassed:
        return translate("Passed").str();
    case kInvalidOptionName:
        return translate("Invalid Option Name").str();
    case kDuplicatedOption:
        return translate("Duplicated Option").str();
    case kAborted:
        return translate("Aborted").str();
    default:
        return translate("Unknown").str();
    }

    return "not implementation";
}

std::error_code make_error_code(ErrorCode e)
{
    return std::error_code(static_cast<int>(e), GetAafErrorCategory());
}

std::error_condition make_error_condition(ErrorCode e)
{
    return std::error_condition(static_cast<int>(e), GetAafErrorCategory());
}

} //namespace aaf
