#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <execinfo.h>
#include <pwd.h>

#include <cstdlib>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/locale/format.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/format.hpp>

#include <adk/arch/generic.h>
#include <adk/singleton_process.h>

#include <aaf/generic_application.h>
#include <adk/boost_logger.h>

#include "aaf_env.h"
#include "api_internal.h"
#include "lib_version.h"
#include "signal_safe_log.h"

#ifdef __AMI_COVERAGE_TEST__
extern "C" void __gcov_flush();
#endif

namespace aaf
{
using boost::locale::translate;
using boost::locale::format;

ADK_LOG_LOCAL_AC("aaf::GenericApplication", LogCodeBase::kGenericApplication)


// 定义静态对象
SignalSafeLogger* SignalSafeLogger::s_logger_inst_ = new SignalSafeLogger();

bool GenericApplication::is_default_signalhdl_ = false;

static GenericApplication* g_app_instance = NULL;
std::string* g_proc_command_line = NULL;
volatile bool GenericApplication::is_running_ = false;
volatile bool g_is_signal_to_exit = false;
std::unordered_map<uint32_t, std::string> GenericApplication::signal_list_;

adk::log::LogLevel g_aaf_log_level = ADK_LOG_LEVEL_INFO;

adk::log::LogLevel GenericApplication::GetLogLevel()
{
    return g_aaf_log_level;
}

GenericApplication::GenericApplication()
    :   option_desc_("Allowed options", 120)
{
    is_running_ = true;
    is_no_print_exit_log_ = false;
    singleton_process_ = NULL;
    if (g_app_instance != NULL) // FIXME: thread safe?
    {
        ADK_LOG_ERROR_AC_TF("application instantiate more than once", "");
        Stop();
    }
    else
        g_app_instance = this;
}

GenericApplication::GenericApplication(bool flag)
{
    // 用于ShardingAgent 实例化
    if (g_app_instance == NULL)
    {
        is_running_           = true;
        is_no_print_exit_log_ = false;
        singleton_process_    = NULL;
    }

    g_app_instance = this;
}

GenericApplication::~GenericApplication()
{
    delete (adk::SingletonProcess*)singleton_process_;
    g_app_instance = NULL;
}

void GenericApplication::Stop()
{
    is_running_ = false;
}

static char g_tid_file_path[8192];
static uint32_t g_tid_file_path_len = 0;
void SetTidFilePath(const std::string& file_path)
{
    memcpy(g_tid_file_path, file_path.c_str(), file_path.size());
    g_tid_file_path[file_path.size()] = '/';
    g_tid_file_path_len = file_path.size() + 1;
}

static char* g_close_fd_when_coredump = nullptr;
static char* g_gen_tid_file_when_coredump = nullptr;

static std::set<int>* g_core_sig_map = nullptr;
void GenericApplication::SignalHandler(int sig_num, siginfo_t * sig_info, void * ptr)
{
    // FIXME: log info?
    if (g_app_instance == NULL)
        return;

    if ((g_core_sig_map == nullptr || g_core_sig_map->count(sig_num))
        && g_gen_tid_file_when_coredump != nullptr
        && !g_app_instance->IsAdvanceFollower())
    {
        uint32_t tid = syscall(SYS_gettid);
        g_tid_file_path_len += SignalSafeToString(tid, g_tid_file_path + g_tid_file_path_len);
        g_tid_file_path[g_tid_file_path_len] = 0x00;
        open(g_tid_file_path, O_CREAT, S_IRUSR | S_IWUSR);

        // note: 
        // 1. to close lock file fd
        // 2. to close the leader/back tcp connection fd
        // 3. to close the domain socket fd between application and recorder process   
        if (g_close_fd_when_coredump != nullptr)
        {
            for (int32_t i = 3; i != 8192; ++i)
            {
                close(i);
            }
        }
    }

    #ifdef __AAF_DEBUG__
    std::cout << "sig_num = " << sig_num << std::endl;
    #endif

    bool is_terminal_signal = false;

    // Handle USR1 Signal
    if (sig_num == SIGUSR1)
    {
        if (sig_info != NULL)
        {
            #ifdef __AAF_DEBUG__
            std::cout << "sig_info->si_int = " << sig_info->si_int << std::endl;
            #endif

            switch (sig_info->si_int)
            {
            case AAF_SIGNAL_VALUE_EXIT:
                g_is_signal_to_exit = true;
                Stop();
                break;
            case AAF_SIGNAL_VALUE_LOG_TRACE:
                g_aaf_log_level = ADK_LOG_LEVEL_TRACE;
                ADK_LOG_SET_THRESHOLD(g_aaf_log_level);
                break;
            case AAF_SIGNAL_VALUE_LOG_DEBUG:
                g_aaf_log_level = ADK_LOG_LEVEL_DEBUG;
                ADK_LOG_SET_THRESHOLD(g_aaf_log_level);
                break;
            case AAF_SIGNAL_VALUE_LOG_INFO:
                g_aaf_log_level = ADK_LOG_LEVEL_INFO;
                ADK_LOG_SET_THRESHOLD(g_aaf_log_level);
                break;
            default:
                break;
            }
        }
    }
    else    // Handle other Signal
    {
        static bool is_first = true;

        // initiative exit
        if (sig_num == SIGTERM || sig_num == SIGINT || sig_num == SIGQUIT)
        {
            #ifdef __AMI_COVERAGE_TEST__
            __gcov_flush();
            #endif
            
            SignalSafeLogger::Log( __LINE__, signal_list_[sig_num].c_str());
            is_terminal_signal = true;
        }

        // passive exit
        else if (sig_num != SIGUSR2)
        {
            if (is_first)
            {
                is_first = false;
                SignalSafeLogger::Log(__LINE__, signal_list_[sig_num].c_str());
                // void* buffer[100];
                // int32_t stack_num = backtrace(buffer, 100);
                // char** stack_trace = (char**)backtrace_symbols(buffer, 100);
                // SignalSafeLogger::Log(__LINE__, "stack info:", g_log_to_consle, false);
                // for (int i = 0; i < stack_num; i++)
                // {
                //     SignalSafeLogger::Log(__LINE__, stack_trace[i], g_log_to_consle, false);
                // }
                // free(stack_trace);
                signal(sig_num, SIG_DFL);
            }
        }
    }

    g_app_instance->OnSignal(sig_num, sig_info->si_int);
    if (is_terminal_signal && is_default_signalhdl_)
    {
        _exit(1);
    }
}

bool GenericApplication::InstallSignalHandler()
{
    /*
       SIGQUIT       3       Core    Quit from keyboard
       SIGILL        4       Core    Illegal Instruction
       SIGABRT       6       Core    Abort signal from abort(3)
       SIGFPE        8       Core    Floating point exception
       SIGBUS      10,7,10     Core    Bus error (bad memory access)
       SIGSEGV      11       Core    Invalid memory reference
       SIGSYS      12,31,12    Core    Bad argument to routine (SVr4)
       SIGTRAP        5        Core    Trace/breakpoint trap
       SIGXCPU     24,24,30    Core    CPU time limit exceeded (4.2BSD)
       SIGXFSZ     25,25,31    Core    File size limit exceeded (4.2BSD)
    */
    if (g_core_sig_map == nullptr)
    {
        g_core_sig_map = new std::set<int>;
    }
    g_core_sig_map->insert(SIGILL);
    g_core_sig_map->insert(SIGABRT);
    g_core_sig_map->insert(SIGFPE);
    g_core_sig_map->insert(SIGBUS);
    g_core_sig_map->insert(SIGSEGV);

    signal_list_.emplace(SIGUSR1, "SIGUSR1 <10>");
    signal_list_.emplace(SIGUSR2, "SIGUSR2 <12>");
    signal_list_.emplace(SIGINT,  "SIGINT <2>");     //2 Ctrl+C
    signal_list_.emplace(SIGQUIT, "SIGQUIT <3>");    //3 Ctrl+'\'
    signal_list_.emplace(SIGILL, "SIGILL <4>");      //4 Illegal Instruction
    signal_list_.emplace(SIGABRT, "SIGABRT <6>");    //6 abort指令
    signal_list_.emplace(SIGBUS,  "SIGBUS <7>");     //7 内存未对齐
    signal_list_.emplace(SIGFPE, "SIGFPE <8>");      //8 浮点数异常
    signal_list_.emplace(SIGSEGV, "SIGSEGV <11>");    //11 内存地址非法
    signal_list_.emplace(SIGTERM, "SIGTERM <15>");    //15 kill pid
   
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    {
        ADK_LOG_ERROR_AC_TF("ignore signal SIGPIPE failed", "");
        return false;
    }

    if (signal(SIGHUP, SIG_IGN) == SIG_ERR)
    {
        ADK_LOG_ERROR_AC_TF("ignore signal SIGHUP failed", "");
        return false;
    }

    struct sigaction sig_action;
    sig_action.sa_handler = NULL;
    sig_action.sa_sigaction = &GenericApplication::SignalHandler;
    sig_action.sa_flags = SA_RESTART | SA_SIGINFO;
    sigemptyset(&sig_action.sa_mask);
    sig_action.sa_restorer = NULL;

    for (auto sig : signal_list_)
    {
        int ret = sigaction(sig.first, &sig_action, NULL);
        if (ret != 0)
        {
            ADK_LOG_ERROR_AC_TF("sigaction failed", "signal <{1}>, errno <{2}>, error string <{3}>",
                            sig.second, errno, strerror(errno));
            return false;
        }
    }

    return true;
}

int32_t GenericApplication::OptionCheck(const char * option_name)
{
    std::vector<std::string> splits;
    std::string option_name_string(option_name);
    boost::split(splits, option_name_string, boost::is_any_of(", "), boost::token_compress_on);

    if (splits[0].empty())
        return ErrorCode::kInvalidOptionName;

    if (option_list_.find(splits[0]) != option_list_.end())
        return ErrorCode::kDuplicatedOption;

    option_list_.insert(splits[0]);
    return ErrorCode::kSuccess;
}

static std::map<std::string, adk::log::LogLevel> g_log_level_map = boost::assign::map_list_of
    ("Trace",   ADK_LOG_LEVEL_TRACE)
    ("Debug",   ADK_LOG_LEVEL_DEBUG)
    ("Info",    ADK_LOG_LEVEL_INFO)
    ("Warn",    ADK_LOG_LEVEL_WARN)
    ("Error",   ADK_LOG_LEVEL_ERROR)
    ("Fatal",   ADK_LOG_LEVEL_FATAL)
    ;

int32_t GenericApplication::ParseLogLevel(const std::string& log_level_str, adk::log::LogLevel& log_level)
{
    auto it = g_log_level_map.find(log_level_str);
    if (it == g_log_level_map.end())
    {
        ADK_LOG_ERROR_AC_TF("invalid log level", "");
        return ErrorCode::kFailure;
    }
    log_level = it->second;
    return ErrorCode::kSuccess;
}

static bool g_is_show_help = false;
static bool g_is_show_version = false;

bool GenericApplication::ParseProgramOption()
{
    po::store(po::parse_command_line(argc_, argv_, option_desc_), option_vm_);
    po::notify(option_vm_);

    if (option_vm_.count("help"))
    {
        g_is_show_help = true;
        std::cout << option_desc_ << std::endl;
        if (!option_example_.empty())
        {
            std::cout << "usage:\n";
            std::cout << option_example_ << std::endl;   
        }
        return false;
    }

    if (option_vm_.count("name"))
    {
        app_name_ = option_vm_["name"].as<std::string>();
    }

    if (option_vm_.count("daemon"))
    {
        is_daemonize_ = true;
    }

    if (option_vm_.count("version"))
    {
        g_is_show_version = true;
    }

    if (ParseLogLevel(option_vm_["log-level"].as<std::string>(), g_aaf_log_level) != ErrorCode::kSuccess)
        return false;
    ADK_LOG_SET_THRESHOLD(g_aaf_log_level);

    for (auto it = on_cfg_cbs_.begin(); it != on_cfg_cbs_.end(); ++it)
    {
        if (option_vm_.count(it->first))
        {
            CallbackInfo& cbi = it->second;
            // (it->second.on_cfg_cb)(it->first, (reinterpret_cast<GenericParser*>(this)->*(it->second.arg_parser))(it->first));
            int32_t ec = (reinterpret_cast<GenericParser*>(this)->*cbi.arg_parser)(it->first, cbi.on_cfg_cb, cbi.this_ptr);
            if (ec != aaf::ErrorCode::kSuccess)
                return false;
        }

        option_vm_.erase(it->first);
    }

    for (auto it = on_cfg_cbs_no_arg_.begin(); it != on_cfg_cbs_no_arg_.end(); ++it)
    {
        if (option_vm_.count(it->first))
        {
            int32_t ec = (it->second)(it->first);
            if (ec != aaf::ErrorCode::kSuccess)
                return false;
        }

        option_vm_.erase(it->first);
    }

    for (auto it = option_list_.begin(); it != option_list_.end(); ++it)
    {
        if (option_vm_.count(*it))
        {
            OnProgramOption(*it);
            if (!is_running())
                return false;
        }
    }
    return true;
}

void GenericApplication::RedirectInputOutput()
{
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    open("/dev/null", O_RDWR);
    open("/dev/null", O_RDWR);
    open("/dev/null", O_RDWR);
}

class CallOnExit
{
public:
    CallOnExit(const boost::function<void ()>& on_exit)
        :   call_on_exit_(on_exit)
    {}

    ~CallOnExit()
    {
        call_on_exit_();
    }
private:
    boost::function<void ()>    call_on_exit_;
};

void GenericApplication::LogAppExit()
{
    if (g_is_show_help
        || g_is_show_version || is_no_print_exit_log_)
        return;

    ADK_LOG_INFO_TF(LogCode::kAppExit, "application complete exit", "");
}

// init hook, for compatible
static AAFInitHookType g_init_hook_logger_ready;
static void* g_init_hook_data_logger_ready;
void RegisterAAFInitHookLoggerReady(AAFInitHookType hook, void* data)
{
    g_init_hook_logger_ready = hook;
    g_init_hook_data_logger_ready = data;
}

void GenericApplication::Run(int argc, const char* argv[])
{
    CallOnExit on_exit(boost::bind(&GenericApplication::LogAppExit, this));

    (void) on_exit;
    try {
        argc_ = argc;
        argv_ = argv;
        option_desc_.add_options()
        ("help,h",          "show this information")
        ("daemon,d",        "launch application as a daemon process")
        ("name,n",          po::value<std::string>(), "set the application name")
        ("log-level,l",     po::value<std::string>()->default_value(std::string("Info")), "set the application log level")
        ("log-dir",         po::value<std::string>(), "set the application log directory path")
        ("log-to-console",  "enable log to console")
        ("async-log",       "enable asynchronous log output")
        ("log-rotate-size", po::value<uint64_t>()->default_value(0), "log file rotating size, 0 means doesn't rotate by size")
        ("no-rotate-log-by-day", "doesn't rotate log file by day")
        ;
        // ("version,v", "show application version string")
        // FIXME: ("level,l", po::value<int32_t>()->default_value(1), "set application log level")
        
        SetProgramOption();
        if (!ParseProgramOption())
            return;

        if (OnParseProgramOptionEnd() != ErrorCode::kSuccess)
        {
            // FIXME : the derivative class should log the error message
            // ADK_LOG_ERROR_AC(translate("Failure: ParseProgramOptionEnd() return with failure"));
            Stop();
            return;
        }

        if (is_daemonize_)
        {
            if (daemon(1, 1) != 0)
            {
                ADK_LOG_ERROR_AC_TF("daemonize failed", "errno <{1}>, desc <{2}>", errno, strerror(errno));
                return;
            }
            // daemon函数执行后父进程退出，子进程继续运行，此时需要更新pid值，保证日志打印正确性
            ADK_LOG_FORK();
        }

        if (!GetApplicationName().empty())
        {
            std::string lock_dir_path, file_path;
            if (SetSingletonLockFileDirectory(lock_dir_path) == ErrorCode::kPassed)
            {
                //default lock_file in user home directory
                std::string login_user;
                struct passwd* pw = getpwuid(geteuid());
                if (pw == NULL)
                {
                    ADK_LOG_ERROR_AC_TF("get login user name failed", "errno <{1}>, desc <{2}>",
                                      errno, strerror(errno));
                    Stop();
                    return;
                }
                file_path = std::string(pw->pw_dir) + "/lock/" + GetApplicationName();
            }
            else
            {
                file_path = lock_dir_path + "/" + GetApplicationName();
            }

            singleton_process_ = (void*)(new adk::SingletonProcess(file_path));
            if (((adk::SingletonProcess*)singleton_process_)->Lock() != ErrorCode::kSuccess)
            {
                ADK_LOG_ERROR_AC_TF("application may be already running", "name: <{1}>", GetApplicationName());
                return;
            }
        }

        if (OnDaemonizeEnd() != ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("OnDaemonizeEnd() return with failure", "");
            Stop();
            return;
        }

        if (is_daemonize_)
            RedirectInputOutput();

        if (option_vm_.count("log-dir") && !GetApplicationName().empty())
        {
            log_dir_ = option_vm_["log-dir"].as<std::string>();
            log_file_ = GetApplicationName();
        }
        else if (OnLogInit(log_dir_, log_file_) == ErrorCode::kPassed)
        {
            if (!GetApplicationName().empty())
            {
                log_file_ = GetApplicationName();
            }

            char* env_var = std::getenv("AAF_LOG_DIR_PATH");
                if (env_var != NULL)
                    log_dir_ = env_var;
            
            if (log_dir_.empty())
                log_dir_ = "./";
        }

        if (!log_file_.empty())
        {
            ADK_LOG_INIT(log_dir_,
                     log_file_,
                     option_vm_.count("log-to-console"),
                     false,
                     false,
                     false,
                     option_vm_.count("async-log"),
                     option_vm_["log-rotate-size"].as<uint64_t>(),
                     !option_vm_.count("no-rotate-log-by-day"));
            ADK_LOG_SET_THRESHOLD(g_aaf_log_level);
        }
        
        if (g_proc_command_line != NULL)
        {
            ADK_LOG_INFO_TF(LogCode::kAppLaunched,
                            "Application is launched",
                            "command: <{1}>",
                            *g_proc_command_line);
        }

        if (SignalSafeLogger::s_logger_inst_->Init(app_name_, log_dir_, log_file_) != ErrorCode::kSuccess)
        {
            ADK_LOG_ERROR_AC_TF("Init signal safe logger failed", "");
            Stop();
        }

        if (option_vm_.count("log-to-console"))
        {
            SignalSafeLogger::s_logger_inst_->set_log_console(true);
        }
        // else default is false

        if (!InstallSignalHandler())
        {
            ADK_LOG_ERROR_AC_TF("install signal handler failed", "");
        }

        if (g_init_hook_logger_ready != nullptr)
        {
            if (g_init_hook_logger_ready(g_init_hook_data_logger_ready)
                != ErrorCode::kSuccess)
            {
                ADK_LOG_ERROR_AC_TF("the logger ready hook failed", "");
                Stop();
            }
        }

        if (is_running_)
        {
            ADK_LOG_INFO_AC_TF("application start to init", "");

            // init coredump configuration
            g_gen_tid_file_when_coredump = std::getenv("AAF_GEN_TID_FILE_COREDUMP");
            g_close_fd_when_coredump = std::getenv("AAF_CLOSE_FD_COREDUMP");

            if (OnInit() != ErrorCode::kSuccess)
            {
                ADK_LOG_ERROR_TF(LogCode::kAppInitFailed, "Application init failed", "");
                Stop();
            }
            else
            {
                ADK_LOG_INFO_TF(LogCode::kAppInitSuccess, "Application init successfully", "");
            }
        }

        int32_t ret;
        while (ACCESS_ONCE(is_running_))
        {
            // FIXME: add event loop!
            try 
            {
                ret = g_app_instance->OnRun();
            }
            catch (...)
            {
                ADK_LOG_ERROR_AC_TF("application throw exception in <OnRun>", "exception: <{1}>",
                                boost::current_exception_diagnostic_information());
                Stop();
                break;
            }
            
            if (ret == ErrorCode::kSuccess
                || ret == ErrorCode::kPassed)
            {
                g_app_instance->OnIdle();
                continue;
            }

            Stop();
        }

        ADK_RMB();
        if (g_is_signal_to_exit)
        {
            ADK_LOG_INFO_AC_TF("application is signaled to exit", "");
        }

        ADK_LOG_INFO_TF(LogCode::kAppToExit, "Application is going to exit", "");

        try 
        {
            g_app_instance->OnExit();
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("OnExit failed", "exception: <{1}>", boost::current_exception_diagnostic_information());
        }
    }
    catch (...)
    {
        // FIXME : log error info
        ADK_LOG_ERROR_AC_TF("Catch exception", "exception: <{1}>", boost::current_exception_diagnostic_information());
        try 
        {
            g_app_instance->OnExit();    
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("OnExit failed", "exception: <{1}>", boost::current_exception_diagnostic_information());
        }
    }
    ADK_LOG_FINISH();
}

void GenericApplication::CopyFrom(GenericApplication* inst)
{
    log_dir_ = inst->log_dir_;
    log_file_ = inst->log_file_;
    app_name_ = inst->app_name_;
    option_list_ = inst->option_list_;
    option_vm_ = inst->option_vm_;
    on_cfg_cbs_ = inst->on_cfg_cbs_;
    on_cfg_cbs_no_arg_ = inst->on_cfg_cbs_no_arg_;
    is_no_print_exit_log_ = inst->is_no_print_exit_log_;
    on_cfg_cbs_no_arg_ = inst->on_cfg_cbs_no_arg_;
}

int GenericApplication::Main(int argc, char const *argv[])
{
    if(argc <= 1)
    {
        lib_main();
        return 1;
    }
    
    ADK_LOG_LOCAL_AC("main", aaf::LogCodeBase::kGenericApplicationMain)
    if (aaf::g_app_instance == NULL)
    {
        ADK_LOG_ERROR_AC_TF("application does not instantiate", "");
        return 1;
    }

    aaf::g_proc_command_line = new std::string();
    aaf::g_proc_command_line->reserve(128);
    for (int32_t i = 0; i < argc; ++i)
    {
        aaf::g_proc_command_line->append(argv[i]);
        aaf::g_proc_command_line->append(" ");
    }

    aaf::g_app_instance->Run(argc, argv);

    aaf::ExitByEnv("AAF_EXIT_BEFORE_MAIN_RETURN", [&](){
        ADK_LOG_INFO_AC_TF("aaf do exit", "aaf _exit without return from main");
    });
    return 0;
}

} // aaf

int main(int argc, char const *argv[])
{
    return aaf::GenericApplication::Main(argc, argv);
}
