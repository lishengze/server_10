#ifndef ADK_IMPL_ERROR_CODE_H_
#define ADK_IMPL_ERROR_CODE_H_

#include <string>

namespace adk_impl
{

typedef int32_t ErrorCode_def;

/**
 * @brief error code
 */
enum ErrorCode
{
    kSuccess = 0,
    kFailure,
    kQueueEmpty,
    kMemPoolIndexError,
    kMemPoolDoubleDelete,
    kWouldblock,
    kInvalidParameters,
    kQueueFull,
    kInvalidInvoke,
    kCreateLockFileDirFaild,
    kOutOfRange,
    kNoMemory,
    kDuplicatedOption,
    kInvalidOptionName,
    kThreadException,
    kUnknownMessge,
    kDefaultGateway,
    kTryAgain,
    kKeyNotExist,
};

/**
 * @brief      Gets the error description.
 *
 * @param[in]  ec    error code value
 *
 * @return     The error description.
 */
const std::string& GetErrorDesc(ErrorCode ec);

#ifndef ADK_CHECK_RET_SUCCESS
#define ADK_CHECK_RET_SUCCESS(result)\
    do\
    {\
        const int32_t local_result = (result);\
        if (ADK_UNLIKELY(ErrorCode::kSuccess != local_result))\
            return local_result;\
    }while(false)
#endif

#ifndef ADK_CHECK_RET_SUCCESS_WITH_EXCEPTION_PRO
#define ADK_CHECK_RET_SUCCESS_WITH_EXCEPTION_PRO(result, exception_pro)\
    do\
    {\
        const int32_t local_result = (result);\
        if (ADK_UNLIKELY(ErrorCode::kSuccess != local_result))\
		{\
			exception_pro;\
            return local_result;\
		}\
    }while(false)
#endif

} // adk

#endif // ADK_ERROR_CODE_H_
