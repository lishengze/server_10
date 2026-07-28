#include "./src/config_default_value.h"
#include "./src/recorder.h"

#include <aaf.h>

#include <boost/format.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

namespace bf = boost::filesystem;
namespace bs = boost::system;

static void SignalParentProcess(char s, int pipe_fd)
{
    auto r = write(pipe_fd, &s, 1);
    close(pipe_fd);
}

class RecorderBinary : public aaf::GenericAmiApplication
{
    ADK_LOG_DECLARE_AC(300000);

public:
    virtual void SetAmiAppOption()
    {
        AddOptionWithAcceptor("recovery-mode",
                              "set is launch from recovery, a non-zero value means to recovery",
                              uint32_t(0),
                              recovery_mode_);

        AddOptionWithAcceptor("recorder-name",
                              "the recorder service name",
                              std::string(""),
                              recorder_name_);

        AddOptionWithAcceptor("pipe-fd",
                              "the parent process pipe fd",
                              -1,
                              pipe_fd_);
        
        AddOptionWithAcceptor("keepalive-fd",
                              "the parent keepalive pipe fd",
                              -1,
                              keepalive_fd_);

        AddOptionWithAcceptor("property-file",
                              "recorder property file",
                              std::string(""),
                              property_file_);
    }

    virtual void OnConfigureFramework(ami::Property& fw_props)
    {
        fw_props.SetValue(aaf::config::kEnableSingletonContext, false);
        fw_props.SetValue(aaf::config::kEnableHighAvailableContext, false);
        fw_props.SetValue(aaf::config::kEnableAppNameCheck, false);
    }

    virtual int32_t OnAmiInitBegin()
    {
        if (adk::EnableShareMemoryDump() == adk::ErrorCode::kSuccess)
        {
            #ifdef __AMI_RECORDER_TEST__
                ADK_LOG_INFO_AC_TF("set coredump_filter success", "");
            #endif
        }

        pid_t recorder_pid = getpid();
        ADK_LOG_INFO_AC_TF("launch the recorder", "recorder pid:{1}", recorder_pid);

        if (!boost::filesystem::exists(property_file_))
        {
            ADK_LOG_ERROR_AC_TF("property file no exists",
                                "file path <{1}>", property_file_);

            SignalParentProcess('f', pipe_fd_);
            return aaf::ErrorCode::kFailure;
        }

        std::ifstream property_file(property_file_);
        if (!property_file.good())
        {
            ADK_LOG_ERROR_AC_TF("open property file failed",
                               "file path <{1}>", property_file_);

            SignalParentProcess('f', pipe_fd_);
            return aaf::ErrorCode::kFailure;
        }

        std::string file_content;
        property_file >> file_content;
        property_file.close();
        boost::system::error_code ec;
        boost::filesystem::remove(property_file_, ec);

        ami::Property* recorder_props;
        try 
        {
            recorder_props = new ami::Property(file_content);
        }
        catch (...)
        {
            ADK_LOG_INFO_AC_TF("parse property file failed",
                               "file path <{1}>", property_file_);
            return aaf::ErrorCode::kFailure;
        }
        
        try 
        {
            std::string lock_path = (boost::format("%1%/lock/%2%")
                                     % recorder_props->GetValue(ami::config::context::recorder::kDataPath, ami::recorder::cdv::kDataPath)
                                     % recorder_name_).str();

            s_singleton_process_ = new adk::SingletonProcess(lock_path);
            if (s_singleton_process_->Lock() != adk::ErrorCode::kSuccess)
            {
                ADK_LOG_ERROR_AC_TF("try to lock path failed", "lock path:{1}", lock_path);
                SignalParentProcess('f', pipe_fd_);
                return aaf::ErrorCode::kFailure;
            }

            if (dup2(pipe_fd_, ami::recorder::cdv::kRecorderPipeFD) < 0)
            {                                
                ADK_LOG_ERROR_AC_TF("dup2 recorder pipe fd failed", "error:{1}", strerror(errno));
                SignalParentProcess('f', pipe_fd_);
                return aaf::ErrorCode::kFailure;
            }
            close(pipe_fd_);
            pipe_fd_ = ami::recorder::cdv::kRecorderPipeFD;

            if (dup2(keepalive_fd_, ami::recorder::cdv::kRecorderKeepalivePipeFD) < 0)
            {
                ADK_LOG_ERROR_AC_TF("dup2 recorder pipe fd failed", "error:{1}", strerror(errno));
                SignalParentProcess('f', pipe_fd_);
            }
            close(keepalive_fd_);
            keepalive_fd_ = ami::recorder::cdv::kRecorderKeepalivePipeFD;

            if (ami::Recorder::RecorderMain(recorder_pid,
                                            recovery_mode_,
                                            recorder_name_,
                                            recorder_data_path_,
                                            *recorder_props)
                != ami::ErrorCode::kSuccess)
            {
                return aaf::ErrorCode::kFailure;
            }
        }
        catch (...)
        {
            ADK_LOG_ERROR_AC_TF("exception cacthed",
                               "{1}", boost::current_exception_diagnostic_information());
            return aaf::ErrorCode::kFailure;
        }

        StopAmiApp();
        return aaf::ErrorCode::kSuccess;
    }

private:
    std::string recorder_data_path_;
    uint32_t recovery_mode_;
    std::string recorder_name_;
    int pipe_fd_ = -1;
    int keepalive_fd_ = -1;
    std::string property_file_;
    adk::SingletonProcess* s_singleton_process_ = nullptr;
} g_recorder_app_instance;

ADK_LOG_DEFINE(RecorderBinary);
