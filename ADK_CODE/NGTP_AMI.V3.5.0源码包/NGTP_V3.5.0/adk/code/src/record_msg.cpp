#include <adk/record_msg.h>

namespace adk_impl
{

ADK_LOG_DEFINE(RecordToAdkLog);

ADK_LOG_DEFINE(RecordToLocalFile);

ADK_LOG_DEFINE(RecordMsgBase) ;

std::string RecordToLocalFile::GetLocalTime()
{
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::chrono::time_point< std::chrono::system_clock, std::chrono::microseconds>  ms = std::chrono::time_point_cast<std::chrono::microseconds>(now);

    time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);

    char mbstr[64] = { 0 };
    std::strftime(mbstr, 64, "%Y-%m-%d %H:%M:%S.", &tm);
    std::stringstream ss;
    ss << mbstr;
    ss << std::setw(6) << std::setfill('0') << ms.time_since_epoch().count() % 1000000;

    return ss.str();
}

} // adk 