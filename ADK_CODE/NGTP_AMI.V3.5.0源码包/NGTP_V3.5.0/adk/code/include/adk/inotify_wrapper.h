#ifndef ADK_IMPL_INOTIFY_WRAPPER_H_
#define ADK_IMPL_INOTIFY_WRAPPER_H_

#include <sys/types.h>
#include <sys/inotify.h>

#include <string>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <list>
#include <memory>

#include <boost/thread/thread.hpp>

#include <adk/error_code.h>

namespace adk_impl
{


enum class inotify_event_t: uint32_t 
{
    in_modify = IN_MODIFY,
    in_delete = IN_DELETE_SELF | IN_DELETE,   
};

class Inotify
{
public:
    /**
     * @brief      事件回调处理函数
     * 
     * @param[in]          文件路径  
     * @param[in]          事件类型 
     * @param[in]          表示事件回调函数名
     *
     * @return     成功返回kSuccess, 失败返回kFailure，当返回kFailure时，移除此回调函数
     */ 
    using OnWatch = std::function<ErrorCode_def(const std::string&, const inotify_event_t&, const std::string&)>;
    
    Inotify() = default;
    ~Inotify();

    Inotify(const Inotify& other) = delete;
    Inotify& operator=(const Inotify& other) = delete;    

    /**
     * @brief      初始化
     * 
     * @param[in]  watch_timeout_millsecond     表示等待事件的超时时间，如果是-1，表示一直阻塞，直到有发生事件;
     *                                          如果大于零，表示等待watch_timeout_millsecond毫秒；
     *                                          如果是0，则表示立即返回(这种场景下epoll_wait会立即返回，为避免一直占用CPU，
     *                                          如果就绪事件等于零，内部接口会主动让出CPU)
     *
     * @return     成功返回kSuccess, 失败返回kFailure
     */ 
    ErrorCode_def Init(const int watch_timeout_millsecond = 1);

    ErrorCode_def Start();
    ErrorCode_def Stop();
    
    /**
     * @brief      注册事件回调函数
     * 
     * @param[in]  path          文件路径  
     * @param[in]  inotify_event 表示事件类型 
     * @param[in]  watch_name    标识符，用于区分之前注册的事件回调；
     *                           AddWatch可以调用多次，每次都会添加一个OnWatch事件回调，只要watch_name不同；
     * @param[in]  OnWatch       表示事件回调函数，当inotify检查到相应的事件发生时，调用此函数
     * @param[in]  is_call_once  表示事件回调函数是否只触发一次(默认为false)，如果是则在第一次调用回调函数后将回调函数删除
     *
     * @return     成功返回kSuccess, 失败返回kFailure
     */ 
    ErrorCode_def AddWatch(const std::string& path, 
                           const inotify_event_t& inotify_event, 
                           const std::string& watch_name,
                           const OnWatch on_watch,
                           const bool is_call_once = false);
    
    /**
     * @brief      移除事件回调函数
     *
     * @param[in]  path          文件路径  
     * @param[in]  inotify_event 表示事件类型 
     * @param[in]  watch_name    标识符，用于区分之前注册的事件回调；
     *                           RemoveWatch可以调用多次，每次都会移除一个OnWatch事件回调
     * 
     * @return     成功返回kSuccess, 失败返回kFailure
     */ 
    ErrorCode_def RemoveWatch(const std::string& path, 
                              const inotify_event_t& inotify_event, 
                              const std::string& watch_name);
    

private:
    
    struct watch_task
    {   
        std::string path_name_;
        std::string watch_name_;
        OnWatch on_watch_ = nullptr;  
        std::atomic<bool> is_call_once_ = {false};
        std::atomic<bool> is_finished_ = {false};

        watch_task(const std::string& path_name, 
                   const std::string& watch_name,
                   const OnWatch on_watch,
                   const bool is_call_once)
                   : path_name_(path_name),
                   watch_name_(watch_name),
                   on_watch_(on_watch),
                   is_call_once_(is_call_once)
        {
            
        }
    };
    
    
    ErrorCode_def handle_inotify_events(std::unordered_map<uint32_t,
                                                           std::unordered_map<uint32_t,
                                                           std::list<std::shared_ptr<watch_task>>>>& on_watch_cache_map);
    
    void Run();

private:    
    int inotify_fd_ = 0;
    int epoll_fd_ = 0;
    
    int watch_timeout_millsecond_ = 0;
    int wake_up_fd_ = 0;
    
    std::atomic<bool> is_running_ = {false};
    
    boost::mutex inotify_mutex_;
    
    boost::thread work_thread_;
    
    // <path, events>
    std::unordered_map<std::string, uint32_t> path_events_map_;
    
    // <path, watch_id>
    std::unordered_map<std::string, uint32_t> path_watch_id_map_;
    
    // <watch_id, <event, <watch_task>>
    std::unordered_map<uint32_t,
                       std::unordered_map<uint32_t,
                       std::list<std::shared_ptr<watch_task>>>> on_watch_map_;

    volatile bool watch_is_update_ = false;

    std::unordered_map<uint32_t, std::string> watch_id_path_map_;
};

}  // namespace

#endif  // ADK_IMPL_INOTIFY_WRAPPER_H_
