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

	void SetProgramOption()
	{
		AddOption("Test,t", "for test");
		AddOptionWithArgument<uint32_t>("Test2", "for test 2");
		AddOptionWithArgument<uint32_t>("Test3", "for test 3", 1234);
	}

	void OnProgramOption(const std::string& option_name)
	{
		std::cout << "option_name = " << option_name << std::endl;
		if (option_name == "Test3")
		{
			std::cout << option_name << " arg = " << GetOptionArgument<uint32_t>(option_name) << std::endl;
		}
	}

	int32_t OnParseProgramOptionEnd()
	{
		std::cout << "OnParseProgramOptionEnd" << std::endl;
		std::cout << "pid = " << getpid() << std::endl;
		return ErrorCode::kSuccess;
	}

	int32_t SetSingletonLockFileDirectory(std::string& file_path) 
	{
		file_path = "./lock";
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
		log_dir = "./log/";
		log_name = "test";
		return ErrorCode::kSuccess;
	}
    
    virtual int32_t OnRun()
    {
        {
            ADK_LOG_LOCAL("yes");
            for (int i = 0; i < 100; ++i)
            {
                ADK_LOG_INFO(1, "yes", "log");
                try
                {
                    ADK_LOG_FATAL_STOP(2, "Log", "no param");
                }
                catch (std::runtime_error& err)
                {
                    std::cout << "Excetpion: " << err.what() << std::endl;
                }
                try
                {
                    ADK_LOG_FATAL_STOP_TF(3, "Log", "param: {1}, {2}", 123);
                }
                catch (std::runtime_error& err)
                {
                    std::cout << "Excetpion: " << err.what() << std::endl;
                }
            }
        }
        {
            ADK_LOG_LOCAL_AC("yes2", 100);
            for (int i = 0; i < 100; ++i)
            {
                ADK_LOG_INFO_AC("yes", "log");
                try
                {
                    ADK_LOG_FATAL_STOP_AC("Log", "no param");
                }
                catch (std::runtime_error& err)
                {
                    std::cout << "Excetpion: " << err.what() << std::endl;
                }
                try
                {
                    ADK_LOG_FATAL_STOP_AC_TF("Log", "param: {1}", 123, "abc");
                }
                catch (std::runtime_error& err)
                {
                    std::cout << "Excetpion: " << err.what() << std::endl;
                }
            }
        }
        Stop();
        return ErrorCode::kPassed;
    }

private:
} g_my_app;
