// Copyright (c) 2018 Archforce Financial Technology. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are not permitted.
//
// For more information about Archforce, welcome to archforce.cn.
 
#ifndef AAF_GENERIC_APPLICATION_H_
#define AAF_GENERIC_APPLICATION_H_

#include <signal.h>

#include <set>
#include <string>
#include <unordered_map>

#include <boost/any.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/program_options.hpp>
#include <boost/exception/diagnostic_information.hpp>

#include <adk/log.h>
#include <aaf/error_code.h>
#include <aaf/log_code_base.h>

namespace sharding
{
class ShardingProxy;
class ShardingAgent;

class FollowerProxy;
}

namespace aaf
{
namespace po = boost::program_options;

#ifndef AAF_SIGNAL_VALUE_EXIT
#define AAF_SIGNAL_VALUE_EXIT     1
#endif

#define AAF_SIGNAL_VALUE_LOG_TRACE     2
#define AAF_SIGNAL_VALUE_LOG_DEBUG     3
#define AAF_SIGNAL_VALUE_LOG_INFO      4

class GenericParser
{
public:
    int32_t    OnConfigureOption(const std::string& name, const void*);
    int32_t    ArgParser(const std::string& name, decltype(&GenericParser::OnConfigureOption)& func, void* this_ptr);
};
typedef decltype(&GenericParser::OnConfigureOption) GenericOptHandler;
typedef decltype(&GenericParser::ArgParser) GenericArgParser;

class GenericApplication;

template<typename ArgumentType>
class ProgramOptionAcceptor
{
public:
    ProgramOptionAcceptor(ArgumentType& arg)
        :   arg_(arg)
    {}
    
    int32_t DoAccept(const std::string& name, const ArgumentType& arg_var)
    {
        arg_ = arg_var;
        return ErrorCode::kSuccess;
    }

    int32_t DoAcceptNarg(const std::string& name)
    {
        arg_ = true;
        return ErrorCode::kSuccess;
    }

private:
    ArgumentType& arg_;
};

class GenericApplication
{
public:
    GenericApplication();

    GenericApplication(bool flag);

    virtual ~GenericApplication();

    /**
     * @brief         添加程序启动选项
     *    
     * @note          通过使用AddOption、AddOptionWithArgument、AddOptionWithAcceptor、
     *                AddOptionWithCallback 函数添加启动选项
     */
    virtual void SetProgramOption() {}

    /**
     * @brief         解析程序选项时，将依次传入各个参数名
     *
     * @note          函数体中一般通过使用GetOptionArgument获取对应选项的值
     *                  
     * @param[in]     程序选项名
     */
    virtual void OnProgramOption(const std::string& option_name) {}

    /**
     * @brief         通知解析程序选项完成
     *
     * @note          默认实现总是返回ErrorCode::kSuccess
     *                  
     * @return        成功时返回ErrorCode::kSuccess / 失败时返回相应的错误码 
     */
    virtual int32_t OnParseProgramOptionEnd() { return ErrorCode::kSuccess; };
    
    /**
     * @brief         SetSingletonLockFileDirectory用于设置锁文件的存放目录
     *
     * @note          默认实现总是返回ErrorCode::kPassed  
     *                  
     * @param[in]     path 锁文件存放目录
     *
     * @return        成功时返回ErrorCode::kPassed / 失败时返回相应的错误码
     */
    virtual int32_t SetSingletonLockFileDirectory(std::string& path) { return ErrorCode::kPassed; }

    /**
     * @brief         OnDaemonizeEnd在设置应用为后台进程完成时被调用
     *
     * @note          默认实现总是返回ErrorCode::kSuccess
     *
     * @return        成功时返回ErrorCode::kSuccess / 失败时返回相应的错误码
     */
    virtual int32_t OnDaemonizeEnd() { return ErrorCode::kSuccess; };

    /**
     * @brief         OnLogInit用于设置log文件的存放目录和文件名
     *
     * @note          默认实现总是ErrorCode::kPassed
     *                  
     * @param[in]     log_dir: log文件保存路径，log_name：log文件名
     *
     * @return        成功时返回ErrorCode::kPassed / 失败返回相应的错误码
     */
    virtual int32_t OnLogInit(std::string& log_dir, std::string& log_name) { return ErrorCode::kPassed; }

    /**
     * @brief         应用初始化
     *
     * @note          通知用户即将启动程序处理流程，所有的参数和配置必须在此初始化      
     *                  
     * @return        成功时返回ErrorCode::kSuccess / 失败返回相应的错误码
     */
    virtual int32_t OnInit() { return ErrorCode::kSuccess; }

    /**
     * @brief         启动程序处理流程
     *
     * @param[out]    成功时返回ErrorCode::kPassed / 失败返回相应的错误码
     */
    virtual int32_t OnRun() { return ErrorCode::kPassed; }
    
    /**
     * @brief         程序空闲态处理逻辑
     *
     * @note          默认实现为将当前线程睡眠1s
     */
    virtual void OnIdle() { sleep(1); }

    /**
     * @brief         应用级信号处理handler，用于处理SIGUSR1和SIGUSR2信号
     *
     * @note          aaf框架提供的处理SIGUSR1和SIGUSR2信号的处理接口，用户可调用该接口实现处理信号的逻辑，
     *                aaf默认提供了SIGUSR1值为1-4的处理方式。      
     *                  
     * @param[in]     sig_num：SIGUSR1或SIGUSR2， value： 信号值
     */
    virtual void OnSignal(int sig_num, int value) 
    {
        is_default_signalhdl_ = true;
    }

    /**
     * @brief         启动程序退出流程
     *
     * @note          该接口主要实现在程序退出前，清理用户资源
     */
    virtual void OnExit() {}

    /**
     * @brief         获取应用实例名称
     *
     * @return        返回不可修改的应用实例名称
     */
    virtual const std::string& GetApplicationName()
    {
        return app_name_;
    }

    /**
     * @brief         获取应用实例名称
     *
     * @return        返回可修改的应用实例名称
     */
    std::string& MutableApplicationName() 
    {
        return app_name_;
    }
    
    /**
     * @brief         添加程序启动选项
     *
     * @note          适用于不需要用户提供参数值的选项
     *                用户启动程序使用选项，不需要提供选项值。
     *
     * @param[in]     option_name：参数名称，option_desc：参数描述
     *
     * @return        成功时返回ErrorCode::kSuccess / 失败时返回相应的错误码
     */
    int32_t AddOption(const char * option_name, const char * option_desc)
    {
        int32_t ec;
        if ((ec = OptionCheck(option_name)) != ErrorCode::kSuccess)
        {
            return ec;
        }

        option_desc_.add_options()(option_name, option_desc);
        return ErrorCode::kSuccess;
    }

    /**
     * @brief         添加程序启动选项
     *
     * @note          适用于需要用户提供参数的选项
     *                若用户启动程序时使用了该选项，则必须提供参数，否则会抛出异常；
     *                若用户启动程序时没有使用该选项，则程序会认为该选项被使用且其值为默认值
     *
     * @param[in]     option_name：选项名称，option_desc：选项描述，default_value：默认选项值
     *
     * @return        成功时返回ErrorCode::kSuccess / 失败时返回相应的错误码
     */
    template<typename ArgumentType>
    int32_t AddOptionWithArgument(const char * option_name, const char * option_desc, ArgumentType default_value)
    {
        int32_t ec;
        if ((ec = OptionCheck(option_name)) != ErrorCode::kSuccess)
        {
            return ec;
        }

        option_desc_.add_options()
        (option_name, po::value<ArgumentType>()->default_value(default_value), option_desc);
        return ErrorCode::kSuccess;
    }

    /**
     * @brief         添加程序启动选项
     *
     * @note          适用于不需要用户提供参数值的选项，同AddOption相同。
     *                若用户启动程序使用该选项，无需提供选项值；
     *                若用户启动程序没有使用该选项，则程序不会默认使用该选项
     *
     * @param[in]     option_name：参数名称、
     *
     * @return        成功时返回ErrorCode::kSuccess / 失败时返回相应的错误码
     */
    template<typename ArgumentType>
    int32_t AddOptionWithArgument(const char * option_name, const char * option_desc)
    {
        int32_t ec;
        if ((ec = OptionCheck(option_name)) != ErrorCode::kSuccess)
        {
            return ec;
        }

        option_desc_.add_options()
        (option_name, po::value<ArgumentType>(), option_desc);
        return ErrorCode::kSuccess;
    }
    
    typedef boost::function<int32_t (const std::string&)> OnConfigNoArgCallbackType;
    
    /**
     * @brief         添加程序启动选项
     *
     * @note          适用于不需要用户提供参数的选项
     *                接口会回调this_ptr->handler(option_name)，handler仅接收一个参数，为启动选项名称.
     *                用户启动程序使用该选项，不需要提供选项值。     
     *
     * @param[in]     option_name：参数名称，option_desc：参数描述
     *                handler：成员函数指针
     *                this_ptr：对象this指针
     *
     * @return        成功时返回ErrorCode::kSuccess / 失败时返回相应的错误码
     */
    template<typename ClassName>
    int32_t AddOptionWithCallback(const char * option_name, const char * option_desc,
                                  int32_t (ClassName::*handler)(const std::string&), ClassName* this_ptr)
    {
        int32_t ec;
        if ((ec = OptionCheck(option_name)) != ErrorCode::kSuccess)
        {
            return ec;
        }

        on_cfg_cbs_no_arg_.insert(std::pair<std::string, OnConfigNoArgCallbackType>(
                                    option_name,
                                    boost::bind(handler, this_ptr, _1)));

        option_desc_.add_options()(option_name, option_desc);
        return ErrorCode::kSuccess;
    }

    template<typename ArgumentType>
    int32_t ArgParser(const std::string& name, GenericOptHandler& func, void* this_ptr)
    {
        return (reinterpret_cast<GenericParser*>(this_ptr)->*reinterpret_cast<GenericOptHandler>(func))(name, &(option_vm_[name].as<ArgumentType>()));
    }

    typedef boost::function<void (const std::string&, const boost::any& )> OnConfigCallbackType;

    struct CallbackInfo
    {
        GenericOptHandler   on_cfg_cb;
        GenericArgParser    arg_parser;
        void*               this_ptr;
    };

    /**
     * @brief         添加程序启动选项
     *
     * @note          适用于需要用户提供参数的选项
     *                该接口会回调函数this_ptr->handler(option_name, value)，handler接收两个参数，第一个为启动选项名，第二个为启动选项值；
     *                若用户启动程序使用该选项，必须提供选项值，否则会抛出异常；
     *                若用户启动程序没有使用该选项，则handler会得到选项的默认值
     *
     * @param[in]     option_name：选项名称，option_desc：选项描述
     *                handler：成员函数指针
     *                this_ptr：对象this指针
     *
     * @return        成功时返回ErrorCode::kPassed / 失败时返回相应的错误码
     */
    template<typename ArgumentType, typename ClassName>
    int32_t AddOptionWithCallback(const char * option_name, const char * option_desc, const ArgumentType& default_value,
                                  int32_t (ClassName::*handler)(const std::string&, const ArgumentType&), ClassName* this_ptr)
    {
        int32_t ec;
        if ((ec = OptionCheck(option_name)) != ErrorCode::kSuccess)
        {
            return ec;
        }

        GenericOptHandler local_handler;
        memcpy(&local_handler, &handler, sizeof(GenericOptHandler));
        on_cfg_cbs_.insert(std::pair<std::string, CallbackInfo>(
                            option_name,
                            {local_handler, reinterpret_cast<GenericArgParser>(&GenericApplication::ArgParser<ArgumentType>), this_ptr}));

        option_desc_.add_options()
        (option_name, po::value<ArgumentType>()->default_value(default_value), option_desc);
        return ErrorCode::kSuccess;
    }

    /**
     * @brief         添加程序启动选项，并根据选项值对acceptor_var赋值
     *
     * @note          适用于需要用户提供参数的选项
     *                该接口使用接收器Acceptor将选项值赋值给变量acceptor_var；
     *                若用户启动程序使用该选项，必须提供选项值，否则会抛出异常；
     *                若用户启动程序没有使用该选项，则接收器Acceptor会将默认选项值赋值给acceptor_var
     *
     * @param[in]     option_name：选项名称，option_desc：选项描述
     *                default_value：选项默认值，acceptor_var：需要被赋值的变量
     *
     * @return        成功时返回ErrorCode::kSuccess / 失败时返回相应的错误码
     */
    template<typename ArgumentType>
    int32_t AddOptionWithAcceptor(const char * option_name, const char * option_desc, const ArgumentType& default_value, ArgumentType& acceptor_var)
    {
        ProgramOptionAcceptor<ArgumentType>* acceptor = new ProgramOptionAcceptor<ArgumentType>(acceptor_var);
        return AddOptionWithCallback<ArgumentType>(option_name,
                                                   option_desc,
                                                   default_value,
                                                   &ProgramOptionAcceptor<ArgumentType>::DoAccept,
                                                   acceptor);
    }

    /**
     * @brief         添加程序启动选项，并根据选项值对acceptor_var赋值
     *
     * @note          适用于不需要用户提供参数的选项，仅适用于bool类型选项
     *                用户启动程序使用该选项，不需要提供选项值，接收器Acceptor会将acceptor_var默认为true。
     *
     * @param[in]     option_name：选项名称，option_desc：选项描述
     *                acceptor_var：需要被赋值的变量
     *
     * @return        成功时返回ErrorCode::kPassed / 失败时返回相应的错误码
     */
    template<typename ArgumentType>
    int32_t AddOptionWithAcceptor(const char * option_name, const char * option_desc, ArgumentType& acceptor_var)
    {
        ProgramOptionAcceptor<ArgumentType>* acceptor = new ProgramOptionAcceptor<ArgumentType>(acceptor_var);
        return AddOptionWithCallback(option_name,
                                     option_desc,
                                     &ProgramOptionAcceptor<ArgumentType>::DoAcceptNarg,
                                     acceptor);
    }

    void AddOptionExample(const char* example)
    {
        option_example_.append(example);
        option_example_.append("\n");
    }

    /**
     * @brief         获取程序启动选项值
     *
     * @note          该接口通过选项名option_name获取相应的选项值
     *
     * @param[in]     option_name：选项名称
     *
     * @return        成功时返回选项值 / 失败时抛出异常
     */
    template<typename ArgumentType>
    ArgumentType GetOptionArgument(const std::string& option_name)
    {
        try 
        {
            return option_vm_[option_name].as<ArgumentType>();
        }
        catch (...)
        {
            throw std::runtime_error(std::string("get program option failed, option name <" + option_name)
                                     + ">, boost diagnostic:\n" + boost::current_exception_diagnostic_information());
        }
    }

    /**
     * @brief         获取log保存路径
     */
    const std::string& GetLogDir()
    {
        return log_dir_;
    }

    /**
     * @brief         获取日志级别
     */
    adk::log::LogLevel GetLogLevel();

    /**
     * @brief         判断程序是否仍在运行
     *
     * @return        返回当前程序运行的状态
     */
    bool is_running() { return is_running_; }

    virtual bool IsAdvanceFollower() { return false; };

    void IgnoreAppExitLog()
    {
        is_no_print_exit_log_ = true;    
    }

    /**
     * @brief         停止程序的运行
     *
     * @note          调用该接口会导致程序进入退出流程
     */
    static void Stop();

    void Run(int argc, const char* argv[]);

    /**
     * @brief         AAF框架的Main函数
     *    
     * @note          默认在aaf的main()函数中调用本接口初始化aaf框架
     *                用户也可在自定义main()函数中调用本接口初始化aaf框架
     *                
     */
    static int Main(int argc, char const *argv[]);

private:
    int                             argc_;
    const char**                    argv_;
    static volatile bool            is_running_;
    bool                            is_daemonize_;
    std::string                     log_dir_;
    std::string                     log_file_;
    std::string                     app_name_;
    std::set<std::string>           option_list_;
    po::options_description         option_desc_;
    po::variables_map               option_vm_;
    std::map<std::string, CallbackInfo>                 on_cfg_cbs_;
    std::map<std::string, OnConfigNoArgCallbackType>    on_cfg_cbs_no_arg_;
    void*                           singleton_process_;
    std::string                     option_example_;
    bool                            is_no_print_exit_log_;
    static bool                     is_default_signalhdl_;
    static std::unordered_map<uint32_t, std::string> signal_list_;

    static void SignalHandler(int, siginfo_t *, void *);
    static bool InstallSignalHandler();
    int32_t OptionCheck(const char * option_name);
    bool ParseProgramOption();
    void RedirectInputOutput();
    void LogAppExit();
    int32_t ParseLogLevel(const std::string& log_level_str, adk::log::LogLevel& log_level);


    void CopyFrom(GenericApplication* inst);

    friend sharding::FollowerProxy;
    friend sharding::ShardingProxy;
    friend sharding::ShardingAgent;
};

#define AAF_ADDOPT_CALLBACK(opt, desc, val, callback) GenericApplication::AddOptionWithCallback(opt, desc, val, callback, this)
#define AAF_ADDOPT_CALLBACK_NARG(opt, desc, callback) GenericApplication::AddOptionWithCallback(opt, desc, callback, this)
#define AAF_ADDOPT_ACCEPTOR(opt, desc, def_val, var)  GenericApplication::AddOptionWithAcceptor(opt, desc, def_val, var)
#define AAF_ADDOPT_ACCEPTOR_NARG(opt, desc, var)  GenericApplication::AddOptionWithAcceptor(opt, desc, var)

#define DefaultGetApplicationName() aaf::GenericApplication::GetApplicationName()

} // aaf

#endif // AAF_GENERIC_APPLICATION_H_
