#include <sys/types.h>
#include <unistd.h>

#include <iostream>

#include <aaf.h>

using namespace aaf;

class MyApp : public GenericApplication 
{
public:
    MyApp() {}
    ~MyApp() {}

    int32_t SetSingletonLockFileDirectory(std::string& file_path) 
    {
        file_path = "/home/nzhao/lock";
        return ErrorCode::kSuccess; 
    }

    int32_t OnDaemonizeEnd() 
    {
        std::cout << "pid = " << getpid() << std::endl;
        std::cout << "OnDaemonizeEnd" << std::endl;
        return ErrorCode::kSuccess;
    };

    int32_t OnLogInit(std::string& log_dir, std::string& log_name)
    {
        log_dir = "/home/nzhao/log/";
        log_name = "test";
        return ErrorCode::kSuccess;
    }

    virtual int32_t OnRun()
    {
        return aaf::ErrorCode::kSuccess;
    }

private:
} g_my_app;
