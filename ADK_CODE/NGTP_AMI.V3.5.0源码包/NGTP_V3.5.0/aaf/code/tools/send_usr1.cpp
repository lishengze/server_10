/**
 * @brief      send SIG_USR1 to process
 * @author     zhaonan, zhaonan@archforce.com.cn
 * @date       2017/02/16
 */
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <pwd.h>

#include <unordered_map>
#include <vector>
#include <set>
#include <iostream>

#include <aaf/generic_application.h>
#include <aaf/error_code.h>
#include <boost/locale/format.hpp>
#include <boost/algorithm/string.hpp>

using namespace aaf;

using boost::locale::translate;
using boost::locale::format;

class SendUsr1 : public GenericApplication
{
private:
    ADK_LOG_DECLARE_AC(100000)
    
public:
    SendUsr1() 
    {  
        Init();
    }
    ~SendUsr1() {}

    void Init()
    {
        value_.sival_int = 1;
        sigval_info_.emplace("1", 1);
        sigval_info_.emplace("2", 2);
        sigval_info_.emplace("3", 3);
        sigval_info_.emplace("4", 4);
        sigval_info_.emplace("Exit", 1);
        sigval_info_.emplace("Trace", 2);
        sigval_info_.emplace("Debug", 3);
        sigval_info_.emplace("Info", 4);
    }

    bool IsPid(const std::string& str)
    {
        for (auto& chr : str)
        {
            if(!isdigit(chr))
            {
                return false;
            }
        }
        return true;
    }

    void GetPid(const std::string name, const std::string filter)
    {
        FILE* fd;
        std::string command;
        if (! filter.empty())
            command = "ps -ef | grep -v 'grep' | grep " + name + "| grep " + filter + "| awk '{print $2}'";
        else
            command = "ps -ef | grep -v 'grep' | grep " + name + "| awk '{print $2}'";
        if ((fd = popen(command.c_str(), "r")) == NULL)
        {
            ADK_LOG_ERROR_AC_TF("Error", "can not find process <{1}>", name);
            exit(1);
        }
        char buf[512] = {0};
        size_t read_len = 0;
        size_t pos = 0;
        while (true)
        {
            read_len = fread(&buf[pos], sizeof(char), 512, fd);
            pos += read_len;
            if (feof(fd) || ferror(fd))
            {
                break;
            }
            read_len = 0;
        } 
        std::vector<std::string> pid_list;
        boost::split(pid_list, buf, boost::is_any_of("\n"), boost::token_compress_on);
        for(auto& pid : pid_list)
        {
            if (!pid.empty())
            {
                pid_set_.emplace(atoi(pid.c_str()));
            }
        }
    }

    void SetProgramOption()
    {
        AddOption("no-sigusr2", "do not send SIGUSR2 "); 
        AddOptionWithArgument<std::string>("process,p", "the program name or process pid");
        AddOptionWithArgument<std::string>("filter,f", "filter policy when using program name");
        AddOptionWithArgument<std::string>("value,i", "the signal value supported by aaf application,default 1(Exit)\n"
                                        "1(Exit), 2(Trace), 3(Debug), 4(Info)", "1");
        AddOptionExample("\
        1.send SIGUSR1 with default value 1 to the process by pid:\n\
            ./send_usr1 -p 12345\n\
        2.send SIGUSR1 with specific value to the process by pid:\n\
            ./send_usr1 -p 12345 -i 2     # change process log level to trace\n\
            ./send_usr1 -p 12345 -i Trace # change process log level to trace\n\
            ./send_usr1 -p 12345 -i Exit  # tell process to exit\n\
        3.send SIGUSR1 to all the process with same program name:\n\
            ./send_usr1 -p hello_world -i 1 \n\
        4.send SIGUSR1 to the process with same program name and filtered processes by key-words:\n\
            ./send_usr1 -p hello_world -f key-words \
        ");
    }

    void OnProgramOption(const std::string& option_name)
    {
        std::string option_val;
        if (option_name == "no-sigusr2")
        {
            is_no_sigusr2_ = true;
        }

        if (option_name == "process")
        {
            option_val.clear();
            option_val = GetOptionArgument<std::string>(option_name);
            if (IsPid(option_val))
            {
                pid_t pid = atoi(option_val.c_str());
                pid_set_.emplace(pid); 
            }
            else
            {
                program_name_ = option_val;
            }
        }

        if (option_name == "filter")
        {
            option_val = GetOptionArgument<std::string>(option_name);
            filter_str_ = option_val;     
        }

        if (option_name == "value")
        {
            option_val = GetOptionArgument<std::string>(option_name);
            int sig_val = sigval_info_[option_val];
            if (sig_val == 0)
            {
                try {
                    sig_val = std::stoi(option_val);
                }
                catch(std::exception)
                {
                    ADK_LOG_ERROR_AC_TF("Error", "signal value <{1}> is incorrect", option_val);
                    Stop();
                }
            }
            value_.sival_int = sig_val; 
        }
    }

    virtual int32_t OnLogInit(std::string& log_dir, std::string& log_name)
    {
        std::string login_user_home;
        struct passwd* pw = getpwuid(geteuid());
        if (pw == NULL)
        {
            ADK_LOG_ERROR_AC_TF("get login user name failed", "errno <{1}>, desc <{2}>",
                            errno, strerror(errno));
            return ErrorCode::kFailure;
        }

        login_user_home = pw->pw_dir;
        log_dir = (format("{1}/log") %  login_user_home).str();

        log_name = "send_user1";
        return ErrorCode::kSuccess;
    }

    int32_t OnInit()
    {
        if (! program_name_.empty())
        {
            GetPid(program_name_, filter_str_);
        }
        Stop();
        return ErrorCode::kSuccess;
    }

    void OnExit()
    {
        for (auto& pid : pid_set_)
        {
            if (sigqueue(pid, SIGUSR1, value_) != 0)
            {
                ADK_LOG_ERROR_AC_TF("Error", "send SIGUSR1 with value <{1}> to pid <{2}> failed <{3}>", 
                                value_.sival_int, pid, strerror(errno));
            }
            else
            {
                ADK_LOG_INFO_AC_TF("Info", "send SIGUSR1 with value <{1}> to pid <{2}>", 
                                value_.sival_int, pid);
            }
            
            if (!is_no_sigusr2_)
            { 
                if (sigqueue(pid, SIGUSR2, value_) != 0)
                {
                    ADK_LOG_ERROR_AC_TF("Error", "send SIGUSR2 with value <{1}> to pid <{2}> failed <{3}>", 
                                    value_.sival_int, pid, strerror(errno));
                }
                else
                {
                    ADK_LOG_INFO_AC_TF("Info", "send SIGUSR2 with value <{1}> to pid <{2}>", 
                                    value_.sival_int, pid);
                }
            }
            
        }
    }

private:
    std::string         program_name_;
    std::string         filter_str_;
    std::set<pid_t>     pid_set_;
    std::unordered_map<std::string, int> sigval_info_;
    union sigval        value_;
    bool                is_no_sigusr2_ = false;
    
} g_send_user1;

ADK_LOG_DEFINE(SendUsr1)
