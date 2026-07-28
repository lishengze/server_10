#ifndef AAF_ERROR_CODE_H_
#define AAF_ERROR_CODE_H_

#include <string>
#include <system_error>

namespace aaf
{

enum ErrorCode
{
    kSuccess = 0,
    kFailure,
    kPassed,
    kInvalidOptionName,
    kDuplicatedOption,
    kAborted,
    kTryAgain
};

/**************************************************************
 * 支持c++11的错误处理框架
 */
class AafErrorCategory : public std::error_category
{
public:
    virtual const char* name() const noexcept;
    virtual std::string message(int ev) const;
};

const std::error_category& GetAafErrorCategory();
std::error_code make_error_code(ErrorCode e);
std::error_condition make_error_condition(ErrorCode e);
/**************************************************************/

} // aaf

#endif // AAF_ERROR_CODE_H_
