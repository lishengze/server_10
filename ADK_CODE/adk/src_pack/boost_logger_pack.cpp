#include <adk/boost_logger.h>
#include <adk_pack/boost_logger.h>

namespace adk
{

namespace log
{

BoostLogger::BoostLogger()
{
    Logger* base_this = static_cast<Logger*>(this);
    new ((void*)base_this) adk_impl::log::BoostLogger();
}

BoostLogger::~BoostLogger()
{
}

void BoostLogger::Init(const boost::filesystem::path& log_dir, const std::string& app_name, bool console_output,
    bool console_filter, bool brief, bool fork_new_file, bool async_sink, uint64_t rotate_size, bool rotate_by_day)
{
    adk_impl::log::BoostLogger::Init(log_dir, app_name, console_output, console_filter, brief,
        fork_new_file, async_sink, rotate_size, rotate_by_day);
}

void BoostLogger::Finish()
{
    adk_impl::log::BoostLogger::Finish();
}

void BoostLogger::Log(LogLevel level, LogCode code, const std::string& module_name, const std::string& function_name,
    uint32_t src_line, const std::string& title, const std::string& message)
{
}

void BoostLogger::Fork()
{
}

}

}