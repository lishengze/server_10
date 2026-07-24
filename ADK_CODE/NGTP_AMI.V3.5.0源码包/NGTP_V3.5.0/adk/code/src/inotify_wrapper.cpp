#include <fnmatch.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <iostream>

#include <adk/inotify_wrapper.h>
#include <adk/util.h>
#include <adk/entry_wrapper.h>

namespace adk_impl
{

// 最大事件数量为32
static int constexpr kMaxEventCount = 32;

Inotify::~Inotify()
{
    is_running_ = false;
    if (work_thread_.joinable())
    {
        work_thread_.join();
    }
    
    ::close(inotify_fd_);
    ::close(epoll_fd_);
}

ErrorCode_def Inotify::Init(const int watch_timeout_millsecond)
{   
    if (watch_timeout_millsecond < 0
        && watch_timeout_millsecond != -1)
    {
        std::cerr << "watch_timeout_millsecond value: " 
                  << watch_timeout_millsecond 
                  <<" was invalid" 
                  << std::endl;
        return ErrorCode::kInvalidParameters;    
    }

    watch_timeout_millsecond_ = watch_timeout_millsecond;
    if (watch_timeout_millsecond_ == 0) // 如果等待超时时间为零，当没有就绪事件时，为避免一直占用CPU，设置超时时间为1毫秒，
    {                                   // 主动让渡一下CPU
        watch_timeout_millsecond_ = 1;
    }

    inotify_fd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd_ < 0)
    {
        std::cerr << "inotify_init failed, errno: " << strerror(errno) << std::endl;
        return ErrorCode::kFailure;
    }
    
    epoll_fd_ = ::epoll_create(10);
    if (epoll_fd_ < 0)
    {
        std::cerr << "epoll_create failed, errno: " << strerror(errno) << std::endl;
        return ErrorCode::kFailure;
    }
    
    struct epoll_event ep_events;
    memset(&ep_events, 0, sizeof(ep_events));
    ep_events.events = EPOLLIN;
    ep_events.data.fd = inotify_fd_;
    
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, inotify_fd_, &ep_events) != 0)
    {
        std::cerr << "epoll add  inotify_fd failed, errno: " << strerror(errno) << std::endl;
        return ErrorCode::kFailure;
    }

    // 创建eventfd，用于后续控制epoll_wait在任何时候停止
    wake_up_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake_up_fd_ < 0)
    {
        std::cerr << "eventfd failed, errno: " << strerror(errno) << std::endl;
        return ErrorCode::kFailure;
    }

    struct epoll_event epfd_events;
    memset(&epfd_events, 0, sizeof(epfd_events));
    epfd_events.events = EPOLLIN;
    epfd_events.data.fd = wake_up_fd_;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wake_up_fd_, &ep_events) != 0)
    {
        std::cerr << "epoll add  wake_up_fd failed, errno: " << strerror(errno) << std::endl;
        return ErrorCode::kFailure;
    }

    return ErrorCode::kSuccess;
}

ErrorCode_def Inotify::Start()
{  
    is_running_ = true;
    work_thread_ = adk_impl::boost_thread("inotify files", "inotify files thread", boost::bind(&Inotify::Run, this));
    
    return ErrorCode::kSuccess;
}

ErrorCode_def Inotify::Stop()
{
    is_running_ = false;

    uint64_t wake_up_fd_val = 1;
    auto ret = ::write(wake_up_fd_, &wake_up_fd_val, sizeof(wake_up_fd_val));
    if (ret < 0)
    {
        std::cerr << "write wake up fd failed, errno: " << ::strerror(errno) << std::endl;
        return  ErrorCode::kFailure;  
    }

    if (work_thread_.joinable())
    {
        work_thread_.join();
    }
    
    ::close(inotify_fd_);
    ::close(epoll_fd_);
    
    return ErrorCode::kSuccess;
}

ErrorCode_def Inotify::handle_inotify_events(std::unordered_map<uint32_t,
                                                                std::unordered_map<uint32_t,
                                                                std::list<std::shared_ptr<watch_task>>>>& on_watch_cache_map)
{
    char buf[4096] __attribute__((aligned(alignof(struct inotify_event))));
    const struct inotify_event* i_event = nullptr;
    ssize_t length = 0;

    length = ::read(inotify_fd_, buf, sizeof(buf));
    if (length == -1 && errno != EAGAIN)
    {
        std::cerr << "read inotify_fd failed, errno: " << strerror(errno) << std::endl;
        return ErrorCode::kFailure;
    }
    
    if (length < 0)
    {
        return ErrorCode::kSuccess;
    }

    if (watch_is_update_)  // 如果发生变化，使用最新的watch更新缓存
    {
        boost::lock_guard<boost::mutex> lock_guard(inotify_mutex_);
        on_watch_cache_map = on_watch_map_;
        watch_is_update_ = false;
    }

    bool has_task_finished = false;
    for(char* ptr = buf; ptr < buf + length; ptr += sizeof(struct inotify_event) + i_event->len)
    {
        i_event = reinterpret_cast<const struct inotify_event*>(ptr);

        auto on_watch_iter = on_watch_cache_map.find(i_event->wd);
        if (on_watch_iter != on_watch_cache_map.end())
        {
            auto watch_event_iter = on_watch_iter->second.begin();
            while(watch_event_iter != on_watch_iter->second.end())
            {

                if (watch_event_iter->first & i_event->mask)
                {
                    auto event = static_cast<inotify_event_t>(watch_event_iter->first);
                    
                    for (auto watch_task: watch_event_iter->second)
                    {
                        if (!watch_task->is_finished_) // 说明该task已经完成，不应该再执行
                        {
                            if (watch_task->on_watch_(watch_task->path_name_, event, watch_task->watch_name_)
                                != ErrorCode::kSuccess)  // 如果调用失败，从watch中删除该任务
                            {
                                watch_task->is_finished_ = true;
                                has_task_finished = true;
                            }

                            if (watch_task->is_call_once_)  // 只调用一次
                            {
                                watch_task->is_finished_ = true;
                                has_task_finished = true;
                            }
                        }
                    }
                }

                ++watch_event_iter;
            }
        }
    }

    if (has_task_finished)  // 需要清理掉已经完成的task
    {
        watch_is_update_ = true;
        boost::lock_guard<boost::mutex> lock_guard(inotify_mutex_);
        auto on_watch_iter = on_watch_map_.begin();
        while (on_watch_iter != on_watch_map_.end())
        {
            auto watch_event_iter = on_watch_iter->second.begin();
            while(watch_event_iter != on_watch_iter->second.end())
            {
                auto iter = watch_event_iter->second.begin();
                while (iter != watch_event_iter->second.end())
                {
                    if ((*iter)->is_finished_) // 删除已执行完成的task
                    {
                        iter = watch_event_iter->second.erase(iter);
                    }
                    else
                    {
                        iter++;
                    }
                }

                if (watch_event_iter->second.empty())
                {
                    watch_event_iter = on_watch_iter->second.erase(watch_event_iter);
                }
                else
                {
                    watch_event_iter++;
                }
            }

            if (on_watch_iter->second.empty())
            {
                std::cout << "rm watch" << std::endl; 
                if (::inotify_rm_watch(inotify_fd_, i_event->wd) != 0)
                {
                    std::cerr << "inotify_rm_watch failed, errno: " << ::strerror(errno) 
                              << " inotify_fd: " << inotify_fd_
                              << " wd: " << i_event->wd
                              << std::endl;
                }

                on_watch_iter = on_watch_map_.erase(on_watch_iter);
                auto& path = watch_id_path_map_[i_event->wd];
                path_watch_id_map_.erase(path);
                path_events_map_.erase(path);
                watch_id_path_map_.erase(i_event->wd);
            }
            else
            {
                on_watch_iter++;
            }
        }
    }

    return ErrorCode::kSuccess;
}

void Inotify::Run()
{ 
    struct epoll_event ep_events[kMaxEventCount];

    std::unordered_map<uint32_t,
                       std::unordered_map<uint32_t,
                       std::list<std::shared_ptr<watch_task>>>> on_watch_cache_map;

    while (is_running_)
    {
        auto event_nums = ::epoll_wait(epoll_fd_, ep_events, kMaxEventCount, watch_timeout_millsecond_);

        if (event_nums <= 0)
        {
            continue;
        }

        for (int event_num = 0; event_num < event_nums; ++event_num)
        {
            if (ep_events[event_num].events & EPOLLIN)
            {
                handle_inotify_events(on_watch_cache_map);
            }
        }
    }
}

ErrorCode_def Inotify::AddWatch(const std::string& path, 
                           const inotify_event_t& inotify_event, 
                           const std::string& watch_name,
                           const OnWatch on_watch,
                           const bool is_call_once)
{
    uint32_t mask = static_cast<uint32_t>(inotify_event);
    
    boost::lock_guard<boost::mutex> lock_guard(inotify_mutex_);
    if (path_events_map_.count(path) == 0)
    {
        int wd = ::inotify_add_watch(inotify_fd_, path.c_str(),  mask);
        if (wd < 0)
        {
            std::cerr << "inotify_add_watch failed, errno: " << ::strerror(errno) << std::endl;
            return ErrorCode::kFailure;
        }  
        
        path_events_map_.insert({path, mask});
        path_watch_id_map_.insert({path, wd});
        watch_id_path_map_.insert({wd, path});
        
        std::list<std::shared_ptr<watch_task>> watch_task_list;
        watch_task_list.emplace_back(std::make_shared<watch_task>(path, watch_name, on_watch, is_call_once));
        
        std::unordered_map<uint32_t, std::list<std::shared_ptr<watch_task>>> watch_event_map;
        watch_event_map.insert({mask, watch_task_list});
        
        on_watch_map_.insert({wd, watch_event_map});
    }
    else if (!(path_events_map_[path] & mask))
    {
        int wd = ::inotify_add_watch(inotify_fd_, path.c_str(), path_events_map_[path] | mask);
        if (wd < 0)
        {
            std::cerr << "inotify_add_watch failed, errno: " << ::strerror(errno) << std::endl;
            return ErrorCode::kFailure;
        }  
        
        path_events_map_[path] |= mask;
        
        std::list<std::shared_ptr<watch_task>> watch_task_list;
        watch_task_list.emplace_back(std::make_shared<watch_task>(path, watch_name, on_watch, is_call_once)); 
        on_watch_map_[wd].insert({mask, watch_task_list});
    }
    else 
    {
        auto wd = path_watch_id_map_[path];
        for (auto watch_task: on_watch_map_[wd][mask])
        {
            if (watch_task->watch_name_ == watch_name)
            {
                return ErrorCode::kDuplicatedOption;
            }    
        }
        
        on_watch_map_[wd][mask].emplace_back(std::make_shared<watch_task>(path, watch_name, on_watch, is_call_once));
    }

    watch_is_update_ = true;

    return ErrorCode::kSuccess;
}

ErrorCode_def Inotify::RemoveWatch(const std::string& path, 
                              const inotify_event_t& inotify_event, 
                              const std::string& watch_name)
{
    uint32_t mask = static_cast<uint32_t>(inotify_event);
    bool has_remove_watch = false;
    
    boost::lock_guard<boost::mutex> lock_guard(inotify_mutex_);
    if (path_watch_id_map_.count(path) != 0)
    {
        auto wd = path_watch_id_map_[path];
        if (on_watch_map_.count(wd) != 0)
        {
            if (on_watch_map_[wd].count(mask) != 0)
            {
                auto& watch_task_list = on_watch_map_[wd][mask];
                auto watch_task_iter = watch_task_list.begin();
                while (watch_task_iter != watch_task_list.end())
                {
                    if ((*watch_task_iter)->watch_name_ == watch_name)
                    {
                        watch_task_iter = watch_task_list.erase(watch_task_iter);
                        if (watch_task_list.size() == 0)
                        {
                            on_watch_map_[wd].erase(mask);
                        }
                        
                        if (on_watch_map_[wd].size() == 0)
                        {
                            if (::inotify_rm_watch(inotify_fd_, wd) != 0)
                            {
                                std::cerr << "inotify_rm_watch failed, errno: " << ::strerror(errno) 
                                          << " path_name: " << path
                                          << " watch_name: " << watch_name
                                          << " inotify_fd: " << inotify_fd_
                                          << " wd: " << wd
                                          << std::endl;
                                          
                                return ErrorCode::kFailure;
                            }
                            
                            on_watch_map_.erase(wd);
                            auto path = watch_id_path_map_[wd];
                            watch_id_path_map_.erase(wd);
                            path_watch_id_map_.erase(path);
                            path_events_map_.erase(path);
                        }
                        
                        has_remove_watch = true;
                        break;
                    }
                    else 
                    {
                        watch_task_iter++;
                    }
                }
            }
            else 
            {
                return ErrorCode::kFailure;
            }
        }
        else 
        {
            return ErrorCode::kFailure;
        }
    }
    
    if (!has_remove_watch)
    {
        return ErrorCode::kFailure;
    }

    watch_is_update_ = true;

    return ErrorCode::kSuccess;
}
}