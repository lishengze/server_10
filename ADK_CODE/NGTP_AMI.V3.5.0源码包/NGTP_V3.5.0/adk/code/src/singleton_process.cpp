#include <boost/exception/diagnostic_information.hpp>

#include <adk/error_code.h>
#include <adk/singleton_process.h>

namespace adk_impl
{

int32_t SingletonProcess::Lock()
{
    try {
        boost::filesystem::path file_path = file_path_;
        boost::filesystem::path dir_path = file_path.parent_path();

        if (file_lock_ != NULL)
        {
            return ErrorCode::kInvalidInvoke;
        }

        boost::system::error_code ec;
        if (!boost::filesystem::exists(dir_path))
        {
            if (!boost::filesystem::create_directories(dir_path)
                && !boost::filesystem::exists(dir_path))
            {
                return ErrorCode::kCreateLockFileDirFaild;
            }
        }

        if (!boost::filesystem::exists(file_path))
        {
            std::ofstream pid_file_stream(file_path_.c_str());
        }

        file_lock_ = new boost::interprocess::file_lock(file_path_.c_str());

        if (!(file_lock_->try_lock()))
        {
            return ErrorCode::kFailure;
        }

        pid_file_stream_.open(file_path_.c_str());
        pid_file_stream_ << boost::interprocess::ipcdetail::get_current_process_id() << std::endl;
        pid_file_stream_.flush();

        is_locked_ = true;
        return ErrorCode::kSuccess;
    }
    catch(...)
    {
        std::cerr << boost::current_exception_diagnostic_information() << std::endl;    // FIXME: log information
        return ErrorCode::kFailure;
    }
}

SingletonProcess::~SingletonProcess()
{
        if (file_lock_ != NULL && is_locked_)
        {
            file_lock_->unlock();
        }

        delete file_lock_;
}

} // adk
