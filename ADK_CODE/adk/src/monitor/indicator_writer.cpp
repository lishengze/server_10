#include <boost/format.hpp>
#include <boost/function.hpp>
#include <boost/filesystem.hpp>
#include <boost/regex.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <adk/monitor/indicator_writer.h>
#include <adk/arch/synchronize.h>
#include <iostream>
#include <map>

namespace adk_impl
{

#define INDICATOR_FILE_FORMAT	"ind_%1%_%2%.ind"
#define INDICATOR_DELIMIT	" "


struct FileCollectStatus
{
    uint32_t file_found;
    uint32_t backoff;
    uint32_t counter;
};

#define ADK_INDI_COLLECT_MAX_BACKOFF 	120

static void CollectIndicatorFiles(const std::string& app_name,
                                  const std::string& file_dir_path,
                                  std::map<std::string, std::string>& app_map)
{
    static FileCollectStatus status = {0, 1, 0};

    ++status.counter;
    if (status.counter >= status.backoff)
    {
        status.counter = 0;

        uint32_t file_found = 0;
        try
        {
            boost::filesystem::directory_iterator end_it; 
            boost::regex expression(std::string("ind_") + app_name + "_([0-9]+-[0-9]+-[0-9]+)\\.ind");
            for (boost::filesystem::directory_iterator i(file_dir_path); i != end_it; ++i)
            {
                if(!boost::filesystem::is_regular_file(i->status()))
                {
                    continue;
                }

                boost::cmatch what;
                std::string filename = i->path().leaf().string();
                if (boost::regex_match(filename.c_str(), what, expression))
                {
                    ++file_found;
                    app_map.emplace(std::string(what[1].first, what[1].second),
                                    i->path().string());
                }
            }
        }
        catch(...) {}

        if (file_found == status.file_found)
        {
            status.backoff *= 2;
            if (status.backoff >= ADK_INDI_COLLECT_MAX_BACKOFF)
                status.backoff = ADK_INDI_COLLECT_MAX_BACKOFF;
        }
        else
        {
            status.backoff = 1;	
        }

        status.file_found = file_found;
    }
}

// yymmdd, file_path
static void HandleIndicatorFiles(
                                const std::string& app_name,
                                const std::string& yymmdd, 
                                const std::string& file_path,
                                const std::string& file_dir_path,
                                const boost::function<void(const std::string&)>& handler,
                                uint32_t file_cnt = 0)
{
    if (app_name.empty())
        return;

    static std::map<std::string, std::map<std::string, std::string>>* 
            s_indicator_files_map = new std::map<std::string, std::map<std::string, std::string>>();
    static boost::mutex* s_mutex = new boost::mutex();
    boost::mutex::scoped_lock lock_guard(*s_mutex);

    try {

    if (!file_path.empty()
        && !yymmdd.empty())
    {
        (*s_indicator_files_map)[app_name][yymmdd] = file_path;
    }

    if (!yymmdd.empty())
    {
        handler((*s_indicator_files_map)[app_name][yymmdd]);
    }
    else
    {
        std::map<std::string, std::string>& app_map = (*s_indicator_files_map)[app_name];
        CollectIndicatorFiles(app_name, file_dir_path, app_map);
        // std::vector<std::string> erase_vec;
        auto it = app_map.begin();
        auto it_end = app_map.end();
        for ( ; it != it_end; )
        {
            if (file_cnt >= app_map.size()) //( - erase_vec.size()))
            {
                break;
            }

            handler(it->second);
            it = app_map.erase(it);
            // erase_vec.push_back(it->first);
            // ++it;
        }

        // for (auto& key : erase_vec)
        // {
        // 	s_indicator_files_map->erase(key);
        // }
    }

    } catch(...) {}
}

void IndicatorWriter::ClearIndicatorFiles(uint32_t threshold)
{
    ++threshold;
    HandleIndicatorFiles(
            app_name_,
            "",
            "", 
            file_dir_path_.string(),
            [](const std::string& file_path){
                boost::system::error_code ec;
                boost::filesystem::remove(file_path, ec);
            },
            threshold);
}

int32_t IndicatorWriter::Init(const boost::filesystem::path& dir_path, const std::string& app_name)
{
    try {
        if (!boost::filesystem::exists(dir_path))
            boost::filesystem::create_directories(dir_path);
    }catch(...){}

    file_dir_path_ = dir_path;
    app_name_ = app_name;
    boost::gregorian::date current_date(boost::gregorian::day_clock::local_day());
    std::string file_name = (boost::format(INDICATOR_FILE_FORMAT)
                             % app_name % boost::gregorian::to_iso_extended_string(current_date)).str();

    HandleIndicatorFiles(
                app_name_,
                boost::gregorian::to_iso_extended_string(current_date),
                (file_dir_path_ / file_name).string(),
                "",
                [](const std::string& file_path){ 
                     // save indicator file
                });
    if (ChangeFile(file_name) != 0)
    {
        return ErrorCode::kFailure;
    }

    file_day_ = current_date.day();
    return ErrorCode::kSuccess;
}

int IndicatorWriter::ChangeFile(const std::string file_name)
{
    if (file_.is_open())
    {
        file_.close();
    }
    file_.open((file_dir_path_ / file_name).string().c_str(),
        std::ios_base::out | std::ios_base::app);
    if (!file_.is_open())
    {
        return -1;
    }
#ifdef __GNUC__
    if (0 != chmod((file_dir_path_ / file_name).string().c_str(), 0664))
    {
        return -1;
    }
#endif
    return 0;
}

int32_t IndicatorWriter::Write(const std::string& key, const std::string& desc, const boost::property_tree::ptree& ptree)
{
    boost::mutex::scoped_lock lock_guard(lock_);
    if (!file_.is_open())
    {
        return ErrorCode::kFailure;
    }

    boost::posix_time::ptime time_now_ = boost::posix_time::microsec_clock::local_time();
    if (file_day_ != time_now_.date().day())
    {
        std::string new_file_name = (boost::format(INDICATOR_FILE_FORMAT)
                % app_name_ % boost::gregorian::to_iso_extended_string(time_now_.date())).str();

        HandleIndicatorFiles(
                        app_name_,
                        boost::gregorian::to_iso_extended_string(time_now_.date()),
                        (file_dir_path_ / new_file_name).string(),
                        "",
                        [](const std::string& file_path){ 
                             // save indicator file
                        });
        if (ChangeFile(new_file_name) == 0)
            file_day_ = time_now_.date().day();
    }

    std::string time_str_ = boost::posix_time::to_iso_extended_string(time_now_);
    time_str_.replace(10, 1, 1, ' ');		// replace week day with ' '

    oss_.clear();
    oss_.str("");

    boost::property_tree::json_parser::write_json(oss_, ptree, false);
    
    buffer_str_ = "@ ";
    buffer_str_ += time_str_ + INDICATOR_DELIMIT + key + INDICATOR_DELIMIT + desc + INDICATOR_DELIMIT + oss_.str();

    if (!file_.write(buffer_str_.c_str(), buffer_str_.size()))
    {
        return ErrorCode::kFailure;
    }
    file_.flush();
    return ErrorCode::kSuccess;
}
} // adk
