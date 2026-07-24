#ifndef AAF_LOG_CODE_BASE_H_
#define AAF_LOG_CODE_BASE_H_

namespace aaf
{

struct LogCode
{
    static const int32_t kAppLaunched = 1000;
    static const int32_t kAppInitSuccess = 1001;
    static const int32_t kAppInitFailed = 1002;
    static const int32_t kAppToExit = 1003;
    static const int32_t kAppExit = 1004;
    static const int32_t kAMIEvent = 1005;
};

struct LogCodeBase
{
    static const int32_t kGenericApplicationMain         = 20000;
    static const int32_t kGenericApplication             = 20010;
    static const int32_t kGenericAmiApplication          = 20100;
    static const int32_t AmiDatabaseApplicationFramework = 21000;
    static const int32_t kAmiBypass                      = 21100;
};
}  // namespace aaf

#endif  // AGF_LOG_CODE_BASE_H_
