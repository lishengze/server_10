#ifndef SIGNAL_SAFE_LOGGER_H_
#define SIGNAL_SAFE_LOGGER_H_

#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <string>
#include <ctime>
#include <cstdlib>
#include <cstdio>

#include <boost/format.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <adk/arch/generic.h>
#include <aaf/error_code.h>
#include <adk/boost_logger.h>


namespace aaf
{

static uint32_t SignalSafeToString(uint32_t value, char* buf)
{
    static char convert_array[10] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint32_t index = 0;
    char local_buf[128];
    while (value > 0)
    {
        uint32_t remaind = value % 10;
        local_buf[index] = convert_array[remaind];
        ++index;
        value = value / 10;
    }
    uint32_t it = 0;
    uint32_t rit = index;
    while (rit != 0)
    {
        --rit;
        buf[it] = local_buf[rit];
        ++it;
    }
    return index;
}

class SignalSafeLogger
{
    const int32_t kLogBufferSize = 1024ul * 1024ul;
    #define MAX_DIGIT_WITH 20
public:
    SignalSafeLogger()
    {
        sig_file_path_ = nullptr;
    }
    std::tm GetLocalTime()
    {
        std::time_t now_t = time(nullptr);
        std::tm tm;
        localtime_r(&now_t, &tm);
        return tm;
    }

    int32_t Init(const std::string& app_name,
                        const std::string& log_dir,
                        const std::string& log_file)
    {
        SetLogFile(log_dir, log_file);
        aaf_siglog_prefix_ = 
            "@ " + to_simple_string(boost::posix_time::microsec_clock::local_time()) + " " 
            + app_name + " " + std::to_string(getpid()) + " " + std::to_string(syscall(SYS_gettid))
            + " 2 Info GenericApplication SignalHandler";

        log_buf_ = new char[kLogBufferSize * 2];
        user_buf_ = new char[kLogBufferSize];
        user_buf_len_ = 0;

        return ErrorCode::kSuccess;
    }

    void set_log_console(bool is_log_console)
    {
        is_log_console_ = is_log_console;
    }

    void SetLogFile(const std::string& log_dir,
                    const std::string& log_file)
    {
        std::tm tm = GetLocalTime();
        std::string file_name = (boost::format("log_%s_%04d-%02d-%02d.log")
                                 % log_file % (tm.tm_year + 1900) % (tm.tm_mon + 1) % tm.tm_mday)
                                 .str();
        std::string sig_file_path_str = log_dir + "/" + file_name;

        if (sig_file_path_ == nullptr)
        {
            sig_file_path_ = new char[PATH_MAX];
        }
        memset(sig_file_path_, 0, PATH_MAX);
        strcpy(sig_file_path_, sig_file_path_str.c_str());
    }

    #define AAF_SIG_FORMAT(var) \
        if (var > 0)    \
        {   \
            CopyToBuffer(" ", buf_index);   \
            CastToBuffer(var, log_buf_, buf_index);   \
        }

    static void Log(uint32_t src_line,
               const char* message,
               bool is_log_prefix = true)
    {
        if(s_logger_inst_)
        {
            s_logger_inst_->SignalLog(src_line, message, is_log_prefix);
        }
    }

    static inline void Append(const char* message)
    {
        if (s_logger_inst_)
        {
            s_logger_inst_->AppendUserLog(message);
        }
    }

    static inline void AppendInt(uint64_t val)
    {
        if (s_logger_inst_)
        {
            s_logger_inst_->AppendUserInt(val);
        }
    }

    void AppendUserLog(const char* message)
    {
        const char* src = message;
        while (src != NULL
               && *src != 0x00)
        {
            user_buf_[user_buf_len_] = *src;
            ++user_buf_len_;
            ++src;
        }
        user_buf_[user_buf_len_] = 0x00;
    }

    void AppendUserInt(uint64_t val)
    {
        CastToBuffer(val, user_buf_, user_buf_len_);

        user_buf_[user_buf_len_] = 0x00;
    }

    void SignalLog(uint32_t src_line, 
                    const char* message, 
                    bool is_log_prefix = true)
    {
        int32_t buf_index = 0;
        CopyToBuffer(aaf_siglog_prefix_.c_str(), buf_index);
        AAF_SIG_FORMAT(src_line);
        CopyToBuffer(" | ", buf_index);
        if (is_log_prefix)
        {
            CopyToBuffer("application receive signal ", buf_index);
        }

        CopyToBuffer(message, buf_index);
        if (user_buf_len_ > 0)
        {
            CopyToBuffer(", ", buf_index);
            user_buf_[user_buf_len_] = 0x00;
            CopyToBuffer(user_buf_, buf_index);
            log_buf_[buf_index] = '\n';
            user_buf_len_ = 0;
        }
        else
        {
            log_buf_[buf_index] = '\n';
        }

        if (is_log_console_)
        {
            write(STDERR_FILENO, log_buf_, buf_index + 1);
        }

        if (sig_file_path_ != nullptr)
        {
            app_log_fd_ = open(sig_file_path_, O_APPEND | O_WRONLY | O_CREAT, 0666);
        }
        else
        {
            const char* log_file_path = adk::log::GetCurrentLogFile();
            if ((log_file_path != nullptr) && (strlen(log_file_path) > 0))
            {
                // 获取成功使用最新的文件名
                app_log_fd_ = open(log_file_path, O_APPEND | O_WRONLY | O_CREAT, 0666);
            }
        }

        if (app_log_fd_ < 0)
        {
            return;// 打开文件失败
        }
        write(app_log_fd_, log_buf_, buf_index + 1);
        close(app_log_fd_);
    }

    void CopyToBuffer(const char* desc, int32_t& buf_index)
    {
        const char* desc_ptr = desc;
        while (desc_ptr != NULL
               && *desc_ptr != 0x00)
        {
            log_buf_[buf_index] = *desc_ptr;
            ++desc_ptr;
            ++buf_index;
        }
    }

    #define MAX_DIGIT_WITH 20
    void CastToBuffer(uint64_t num, char* buff, int32_t& buf_index)
    {
        int32_t valid_number = 0;
        int32_t index_end = buf_index + MAX_DIGIT_WITH;
        do {
            int remain = num % 10;
            num = num / 10;
            buff[index_end] = char_map_[remain];
            --index_end;
            ++valid_number;
            if (index_end < buf_index)
                break;
        } while (num != 0);

        memmove(&buff[buf_index],
                &buff[buf_index + MAX_DIGIT_WITH  + 1 - valid_number],
                valid_number);
        
        buf_index += valid_number;
    }


    static SignalSafeLogger* s_logger_inst_;
private:
    char* log_buf_ = nullptr;
    char* user_buf_ = nullptr;
    int32_t user_buf_len_ = 0;
    int app_log_fd_ = -1;
    bool is_log_console_ = false;
    std::string aaf_siglog_prefix_;
    char* sig_file_path_ = nullptr;
    const char* char_map_ = "0123456789";// 存储0-9的字符，用于数值类型转换为char类型
};

}   // end of namespace aaf

#endif  // SIGNAL_SAFE_LOGGER_H_