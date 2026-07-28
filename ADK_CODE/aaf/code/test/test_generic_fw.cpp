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

private:
} g_my_app;
