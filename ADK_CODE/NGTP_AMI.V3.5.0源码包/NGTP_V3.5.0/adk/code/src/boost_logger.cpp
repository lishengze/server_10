/**
 * @file boost_logger.cpp
 * @brief 使用boost log实现的日志记录器
 * @author Li Yunchong
 * @version 0.1
 * @date 2017-08-10
 */
#include <unistd.h>
#include <sys/syscall.h>

#include <mutex>
#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/exception_handler.hpp>
#include <boost/regex.hpp>

#include <adk/log.h>
#include <adk/boost_logger.h>
#include <adk/arch/generic.h>
using namespace boost::log;

namespace adk_impl
{

namespace log
{

static const int32_t kEventCodeMax = 9999;

enum Severity
{
    kTrace = ADK_LOG_LEVEL_TRACE,
    kDebug = ADK_LOG_LEVEL_DEBUG,
    kInfo = ADK_LOG_LEVEL_INFO,
    kWarn = ADK_LOG_LEVEL_WARN,
    kError = ADK_LOG_LEVEL_ERROR,
    kFatal = ADK_LOG_LEVEL_FATAL,
};

typedef sinks::synchronous_sink<sinks::text_file_backend> SyncFileSink;
typedef sinks::asynchronous_sink<sinks::text_file_backend> AsyncFileSink;
typedef sinks::synchronous_sink<sinks::text_ostream_backend> SyncConsoleSink;
typedef sinks::asynchronous_sink<sinks::text_ostream_backend> AsyncConsoleSink;

static boost::filesystem::path s_log_dir;
static std::string s_filename_num_pattern;
static std::string s_filename_pattern;
static std::string s_app_name;
static bool s_brief = false;
static bool s_console_filter = false;
static bool s_fork_new_file = false;
static bool s_async_sink = false;
static uint64_t s_rotate_size = 0;
static bool s_rotate_by_day = true;
static uint64_t s_log_file_num = 0;

static char* s_current_log_path = nullptr;


static sources::severity_logger_mt<Severity>* s_logger
            = new sources::severity_logger_mt<Severity>;

inline std::ostream& operator<<(std::ostream& strm, Severity level)
{
    // 应保证下标顺序与ADK_LOG_LEVEL_xxx定义一致
    static const char* strings[] = {"0 Trace", "1 Debug", "2 Info",
                                    "3 Warn", "4 Error", "5 Fatal", 
                                    "2 Info"};  // 最后的Info实际上是6:Event级，方便过滤
    if (static_cast<std::size_t>(level) < sizeof(strings) /sizeof(*strings))
    {
        strm << strings[level];
    }
    else
    {
        strm << static_cast<int>(level);
    }
    return strm;
}

BOOST_LOG_ATTRIBUTE_KEYWORD(_severity, "Severity", Severity)

// 用于信号日志获取当前的日志文件名称
const char* GetCurrentLogFile()
{
    return s_current_log_path;
}
// 获取格式化时间
inline std::string GetFormatLocalTime(const std::string& format)
{
    time_t time_point = time(0);
    struct tm* timeinfo;
    timeinfo = localtime(&time_point);

    char buffer[24];// _YYYY-mm-dd 设置24为超出format的长度
    strftime(buffer, 24, format.c_str(), timeinfo);
    return std::string(buffer);
}

// 自定义Boost Log的sinks::file::collector类，用以在日志文件切换时补充补充相关文件编号操作
class ADKFileCollector : public sinks::file::collector
{
public:
    ADKFileCollector()
    {
        boost_collector = sinks::file::make_collector(keywords::target=s_log_dir);
        path_buffers_[0] = new char[PATH_MAX];
        memset(path_buffers_[0], 0, PATH_MAX);

        path_buffers_[1] = new char[PATH_MAX];
        memset(path_buffers_[1], 0, PATH_MAX);
    }
    boost::shared_ptr< sinks::file::collector > boost_collector;
    // 文件发送切换时会调用，并传入写完的文件名
    void store_file(boost::filesystem::path const& src_path) override
    {
        boost_collector->store_file(src_path);
        // 不切割日志时不进行文件缓存
        if (s_rotate_size <= 0)
            return;
        RenameLogFile(src_path);

        char* other_buf = nullptr;
        if (s_current_log_path == path_buffers_[0])
        {
            other_buf = path_buffers_[1];
        }
        else
        {
            other_buf = path_buffers_[0];
        }

        strncpy(other_buf, src_path.string().c_str(), PATH_MAX);
        other_buf[PATH_MAX - 1] = '\0';
        ADK_BARRIER();
        s_current_log_path = other_buf;
    }

   uintmax_t scan_for_files(
        sinks::file::scan_method method, 
        boost::filesystem::path const& pattern = boost::filesystem::path(), 
        unsigned int* counter = 0) override
    {
        return boost_collector->scan_for_files(method, s_filename_num_pattern, counter);
    }
    // 对日志文件进行编号，仅在日志切割场合当不带编号的文件写满时，将其raname为带编号的文件
    inline void RenameLogFile(boost::filesystem::path const& src_path)
    {
        std::string src_path_str = src_path.string();
        auto insert_pos = src_path_str.find_last_of(".");
        std::string src_local_time = src_path_str.substr(insert_pos - 10, 10);// insert_pos-10代表YYYY-mm-dd

        boost::filesystem::path new_path(src_path_str.insert(insert_pos, "_" + std::to_string(s_log_file_num)));
        ++s_log_file_num;

        if (src_local_time != GetFormatLocalTime("%Y-%m-%d"))
        {
            s_log_file_num = 0;// 新的一天，重新计数
        }
        boost::system::error_code ec;
        boost::filesystem::rename(src_path, new_path, ec);
        if (ec)
        {
            if (ec.value() == boost::system::errc::cross_device_link)
            {
                // Attempt to manually move the file instead
                boost::filesystem::copy_file(src_path, new_path);
                boost::filesystem::remove(src_path);
            }
            else
            {
                BOOST_THROW_EXCEPTION(boost::filesystem::filesystem_error("failed to move file to another location", src_path, new_path, ec));
            }
        }
    }

private:
    char* path_buffers_[2] = {nullptr, nullptr};
};

template <typename T>
void InitFileLog()
{
    static boost::shared_ptr<T> *file_sink = NULL;

    std::string file_name;

    if (s_fork_new_file)
    {
        file_name = (boost::format("log_%1%_%2%_%3%")
                        % s_app_name % Logger::pid() % "%Y-%m-%d").str();
    }
    else
    {
        file_name = (boost::format("log_%1%_%2%") % s_app_name % "%Y-%m-%d").str();
    }
    s_filename_num_pattern = file_name + ((s_rotate_size > 0) ? "_%N.log" : ".log");
    file_name += ".log";
    s_filename_pattern = file_name;
    boost::shared_ptr<typename T::sink_backend_type> backend
        = boost::make_shared<typename T::sink_backend_type>();
    backend->set_file_name_pattern((s_log_dir / file_name).native());
    backend->auto_flush(true);
    backend->set_open_mode(std::ios_base::app | std::ios_base::out);
    boost::shared_ptr<ADKFileCollector> collector = boost::make_shared<ADKFileCollector>();
    backend->set_file_collector(collector);

    if (s_rotate_size > 0)
    {
        // 遍历当前目录，获取编号情况下的最新编号
        boost::filesystem::directory_iterator end_it; 
        // 日志文件的格式 name_YYYY-mm-dd_N.xxx，这里需要获取N的值
        boost::regex expression("log_" + s_app_name + GetFormatLocalTime("_%Y-%m-%d") + "(_([0-9]+))?.log" );
        boost::filesystem::path src_path;
        for (boost::filesystem::directory_iterator i(s_log_dir); i != end_it; ++i)
        {
            if(!boost::filesystem::is_regular_file(i->status()))
            {
                continue;
            }

            boost::cmatch what;
            std::string filename = i->path().filename().string();
            if (boost::regex_match(filename.c_str(), what, expression))
            {
                std::string number_str = what.str(2);
                if(number_str.empty())
                {
                    src_path = i->path();
                }
                else
                {
                    uint64_t number = atoi(number_str.c_str());
                    if (s_log_file_num <= number)
                        s_log_file_num = number + 1;
                }
                
            }
        }
        backend->set_rotation_size(s_rotate_size);
    }

    if (s_rotate_by_day)
    {
        backend->set_time_based_rotation(sinks::file::rotation_at_time_point(0, 0, 0));
    }
    backend->scan_for_files(sinks::file::scan_method::scan_matching, true);

    if (file_sink)
    {
        core::get()->remove_sink(*file_sink);
    }

    file_sink = new boost::shared_ptr<T>(boost::make_shared<T>(backend));

    if (s_brief)
    {
        (*file_sink)->set_formatter(
                expressions::stream
                        << expressions::format_date_time<boost::posix_time::ptime>(
                                "TimeStamp",
                                "@ %Y-%m-%d %H:%M:%S.%f") << ' '
                        << expressions::message);
    }
    else
    {
        (*file_sink)->set_formatter(
                expressions::stream
                        << expressions::format_date_time<boost::posix_time::ptime>(
                                "TimeStamp",
                                "@ %Y-%m-%d %H:%M:%S.%f") << ' '
                        << expressions::attr<std::string>("HostName") << ' '
                        << expressions::attr<std::string>("AppName") << ' '
                        << expressions::message);
    }

    core::get()->add_sink(*file_sink);
}

template <typename T>
void InitEventFileLog()
{
    static boost::shared_ptr<T> *event_file_sink = NULL;

    std::string file_name;

    if (s_fork_new_file)
    {
        file_name = (boost::format("evt_%1%_%2%_%3%")
                        % s_app_name % Logger::pid() % "%Y-%m-%d").str();
    }
    else
    {
        file_name = (boost::format("evt_%1%_%2%") % s_app_name % "%Y-%m-%d").str();
    }
    // file_name += (s_rotate_size > 0) ? "_%H-%M-%S_%N.log" : ".log";
    file_name += ".log";
    boost::shared_ptr<typename T::sink_backend_type> backend
        = boost::make_shared<typename T::sink_backend_type>();
    backend->set_file_name_pattern((s_log_dir / file_name).native());
    backend->auto_flush(true);
    backend->set_open_mode(std::ios_base::app | std::ios_base::out);

    // if (s_rotate_size > 0)
    // {
    //     backend->set_rotation_size(s_rotate_size);
    // }

    if (s_rotate_by_day)
    {
        backend->set_time_based_rotation(sinks::file::rotation_at_time_point(0, 0, 0));
    }

    if (event_file_sink)
    {
        core::get()->remove_sink(*event_file_sink);
    }

    event_file_sink = new boost::shared_ptr<T>(boost::make_shared<T>(backend));

    if (s_brief)
    {
        (*event_file_sink)->set_formatter(
                expressions::stream
                        << expressions::format_date_time<boost::posix_time::ptime>(
                                "TimeStamp",
                                "@ %Y-%m-%d %H:%M:%S.%f") << ' '
                        << expressions::message);
    }
    else
    {
        (*event_file_sink)->set_formatter(
                expressions::stream
                        << expressions::format_date_time<boost::posix_time::ptime>(
                                "TimeStamp",
                                "@ %Y-%m-%d %H:%M:%S.%f") << ' '
                        << expressions::attr<std::string>("HostName") << ' '
                        << expressions::attr<std::string>("AppName") << ' '
                        << expressions::message);
    }

    (*event_file_sink)->set_filter(_severity >= Severity::kWarn);
    core::get()->add_sink(*event_file_sink);
}


template <typename T>
void InitConsoleLog()
{
    static boost::shared_ptr<T> *console_sink = NULL;

    boost::shared_ptr<typename T::sink_backend_type> backend
        = boost::make_shared<typename T::sink_backend_type>();
    backend->add_stream(
            boost::shared_ptr<std::ostream>(&std::clog, boost::null_deleter()));
    backend->auto_flush(true);

    if (console_sink)
    {
        core::get()->remove_sink(*console_sink);
    }

    console_sink = new boost::shared_ptr<T>(boost::make_shared<T>(backend));

    if (s_brief)
    {
        (*console_sink)->set_formatter(
                expressions::stream
                        << "@ "
                        << expressions::format_date_time<boost::posix_time::ptime>(
                                "TimeStamp",
                                "%Y-%m-%d %H:%M:%S.%f") << ' '
                        << expressions::message);
    }
    else
    {
        (*console_sink)->set_formatter(
                expressions::stream
                        << "@ "
                        << expressions::format_date_time<boost::posix_time::ptime>(
                                "TimeStamp",
                                "%Y-%m-%d %H:%M:%S.%f") << ' '
                        << expressions::attr<std::string>("AppName") << ' '
                        << expressions::message);
    }

    if (s_console_filter)
    {
        (*console_sink)->set_filter(_severity >= Severity::kWarn);
    }
    core::get()->add_sink(*console_sink);
}


BoostLogger::BoostLogger()
{
}

BoostLogger::~BoostLogger()
{
}

void BoostLogger::Init(const boost::filesystem::path& log_dir,
                       const std::string& app_name,
                       bool console_output,
                       bool console_filter,
                       bool brief,
                       bool fork_new_file,
                       bool async_sink,
                       uint64_t rotate_size,
                       bool rotate_by_day)
{
    
    if (rotate_size == 0)
    {
        char* env_var = std::getenv("ADK_LOG_ROTATE_SIZE");
        if (nullptr != env_var)
        {
            try{
                rotate_size = atoi(env_var);
            }
            catch(...)
            {
                rotate_size = 0;
            }
        }
    }
    
    boost::system::error_code ec;
    std::string host_name = boost::asio::ip::host_name(ec);

    core::get()->set_exception_handler(make_exception_suppressor());
    core::get()->add_global_attribute("TimeStamp", attributes::local_clock());
    core::get()->add_global_attribute("HostName",
                                      attributes::constant<std::string>(host_name));
    core::get()->add_global_attribute("AppName",
                                      attributes::constant<std::string>(app_name));

    s_log_dir = log_dir;
    s_app_name = app_name;
    s_console_filter = console_filter;
    s_brief = brief;
    s_fork_new_file = fork_new_file;
    s_async_sink = async_sink;
    s_rotate_size = rotate_size;
    s_rotate_by_day = rotate_by_day;

    if (s_async_sink)
    {
        InitFileLog<AsyncFileSink>();
        InitEventFileLog<AsyncFileSink>();
        if (console_output)
        {
            InitConsoleLog<AsyncConsoleSink>();
        }
    }
    else
    {
        InitFileLog<SyncFileSink>();
        InitEventFileLog<SyncFileSink>();
        if (console_output)
        {
            InitConsoleLog<SyncConsoleSink>();
        }
    }
}

void BoostLogger::Log(LogLevel level,
                      LogCode code,
                      const std::string& module_name,
                      const std::string& function_name,
                      uint32_t src_line,
                      const std::string& title,
                      const std::string& message)
{
    if (code < kEventCodeMax)
    {
        level = ADK_LOG_LEVEL_FATAL + 1;
    }
    BOOST_LOG_SEV(*s_logger, static_cast<Severity>(level))
          << pid() << ' ' << tid() << ' ' << static_cast<Severity>(level) << ' '
          << module_name << ' ' << function_name << ' ' << src_line << ' '
          << code << " | " << title << " | " << message;
}


void BoostLogger::Log(pid_t pid,
                      pid_t tid,
                      LogLevel level,
                      LogCode code,
                      const std::string& module_name,
                      const std::string& function_name,
                      uint32_t src_line,
                      const std::string& title,
                      const std::string& message)
{
    if (code < kEventCodeMax)
    {
        level = ADK_LOG_LEVEL_FATAL + 1;
    }
    BOOST_LOG_SEV(*s_logger, static_cast<Severity>(level))
          << pid << ' ' << tid << ' ' << static_cast<Severity>(level) << ' '
          << module_name << ' ' << function_name << ' ' << src_line << ' '
          << code << " | " << title << " | " << message;
}

void BoostLogger::Fork()
{
    if (s_async_sink)
    {
        InitFileLog<AsyncFileSink>();
        InitEventFileLog<AsyncFileSink>();
        InitConsoleLog<AsyncConsoleSink>();
    }
    else
    {
        if (s_fork_new_file)
        {
            InitFileLog<SyncFileSink>();
            InitEventFileLog<SyncFileSink>();
        }
    }
}

void BoostLogger::Finish()
{
    core::get()->flush();
}

} // namespace log
} // namespace adk_impl
