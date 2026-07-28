#include <iostream>
#include <atomic>

#include <adk/error_code.h>
#include <adk/inotify_wrapper.h>

static std::atomic<bool> g_is_running = {true}; 

adk_impl::ErrorCode_def OnWatch(const std::string& path, const adk_impl::inotify_event_t& event, const std::string& watch_name)
{
    if (event == adk_impl::inotify_event_t::in_modify)
    {
        std::cout << "modify file: " << path << " watch_name:" << watch_name << std::endl; 
    }
    else if (event == adk_impl::inotify_event_t::in_delete)
    {
        std::cout << "delete file: " << path << " watch_name:" << watch_name << std::endl;
        g_is_running = false; 
    }
    
    return adk_impl::ErrorCode::kSuccess;
}

int main(int argc, char const *argv[])
{
    auto test_func = [](int wait_timeout_second){

        std::cout << "wait_timeout_second: " << wait_timeout_second << std::endl;
        adk_impl::Inotify inotify;

        system("touch test.txt");
        inotify.Init(wait_timeout_second);
        inotify.Start();
        
        g_is_running = true;
        
        if (inotify.AddWatch("test.txt", adk_impl::inotify_event_t::in_modify, "test_watch", OnWatch) != adk_impl::ErrorCode::kSuccess)
        {
            return -1;
        }
        if (inotify.AddWatch("test.txt", adk_impl::inotify_event_t::in_delete, "test_watch", OnWatch) != adk_impl::ErrorCode::kSuccess)
        {
            return -1;
        }
        
        if (inotify.AddWatch("test.txt", adk_impl::inotify_event_t::in_modify, "cat_watch", OnWatch) != adk_impl::ErrorCode::kSuccess)
        {
            return -1;
        }
        
        if (inotify.AddWatch("test.txt", adk_impl::inotify_event_t::in_modify, "cat_watch_once", OnWatch, true) != adk_impl::ErrorCode::kSuccess)
        {
            return -1;
        }
        
        if (inotify.AddWatch("test.txt", adk_impl::inotify_event_t::in_modify, "cat_watch", OnWatch) != adk_impl::ErrorCode::kDuplicatedOption)
        {
            return -1;
        }
        
        for (int i = 0; i < 100; ++i)
        {
            system("echo test >> test.txt");
        }
        
        if (inotify.RemoveWatch("test.txt", adk_impl::inotify_event_t::in_modify, "cat_watch") != adk_impl::ErrorCode::kSuccess)
        {
            std::cout << "RemoveWatch error" << std::endl;
            return -1;
        }
        
        system("echo test > test.txt");
        
        system("rm test.txt");

        int sleep_count = 0;
        while (g_is_running)
        {
            ++sleep_count;
            std::cout << "sleep count: " << sleep_count << std::endl;
            sleep(1);
        }
        
        inotify.Stop();
        
        return 0;
    };

    test_func(-1);
    test_func(10);
    test_func(0);

    return 0;
}