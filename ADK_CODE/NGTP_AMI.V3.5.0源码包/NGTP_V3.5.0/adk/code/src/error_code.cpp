#include <map>

#include <boost/locale/format.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/locale/message.hpp>

#include <adk/error_code.h>

using boost::locale::translate;

namespace adk_impl
{

typedef std::map<ErrorCode, std::string> ErrorMessageMap;

static ErrorMessageMap err_msg_map = boost::assign::map_list_of
    (ErrorCode::kSuccess, translate("Success").str())
    (ErrorCode::kFailure, translate("Failure").str())
    (ErrorCode::kQueueEmpty, translate("Queue empty").str())
    (ErrorCode::kMemPoolIndexError, translate("Memory pool index error").str())
    (ErrorCode::kMemPoolDoubleDelete, translate("Memory pool double delete").str())
    (ErrorCode::kWouldblock, translate("Would block").str())
    (ErrorCode::kInvalidParameters, translate("Invalid parameters").str())
    (ErrorCode::kQueueFull, translate("Queue full").str())
    ;


const std::string& GetErrorDesc(ErrorCode ec)
{
    static std::string s_unknown_err_str = translate("Unknown Error").str();

    ErrorMessageMap::const_iterator it = err_msg_map.find(ec);
    if (it != err_msg_map.end())
    {
        return it->second;
    }
    else
    {
        return s_unknown_err_str;
    }
}

}
