#ifndef AMI_IMPL_INDICATOR_WRITER_H_
#define AMI_IMPL_INDICATOR_WRITER_H_

#include "../error_code.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>
#include <sstream>

#include <boost/thread/mutex.hpp>
#include <boost/filesystem.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/property_tree/ptree.hpp>

#include <adk/libadk.h>

namespace adk_impl
{

class IndicatorWriter
{
public:
    IndicatorWriter()
        : file_day_(1)
    {}

    ~IndicatorWriter()
    {
    }

    ADK_API int32_t Init(const boost::filesystem::path& dir_path, const std::string& app_name);

    ADK_API int32_t Write(const std::string& key, const std::string& desc, const boost::property_tree::ptree& ptree);

    ADK_API void ClearIndicatorFiles(uint32_t threshold = 1);

private:
    boost::mutex                lock_;
    std::ofstream               file_;
    std::string                 time_str_;            // for performance
    std::ostringstream          oss_;
    std::string                 buffer_str_;

    boost::gregorian::greg_day  file_day_;
    boost::posix_time::ptime    time_now_;
    std::string                 app_name_;
    boost::filesystem::path     file_dir_path_;

    ADK_API int ChangeFile(const std::string file_name);
};
} // adk

#endif // AMI_INDICATOR_WRITER_H_
