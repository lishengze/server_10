#include <sys/types.h>

#include <vector>

#include <boost/thread/mutex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread/thread.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <adk/util.h>
#include <adk/arch/generic.h>
#include <adk/monitor/monitor.h>
#if defined(__GNUC__)
#include <unistd.h>
#include <adk/lock_free_msg_queue.h>
#elif defined(_MSC_VER)
#include <queue>
#include <mutex>
#include <atomic>
#include <iostream>
#include <adk/error_code.h>
#endif

namespace adk_impl
{

enum RequestType
{
    kFunctor,
    kFunction,
    kPtree,
};

struct MonitorRequest
{
    IMonitorSinker::Type     req_from;
    RequestType              req_type;
    int32_t                  query_type;
    uint64_t                 query_key;
    void*                    func;
    void*                    user_data;
};

#if defined(_MSC_VER)

class MPSCQueue
{
public:
    explicit MPSCQueue(const char* name, size_t capacity) : name_(name), capacity_(capacity) {};

    template <typename T>
    static MPSCQueue* Create(const char* name, size_t capacity)
    {
        MPSCQueue* queue = new MPSCQueue(name, capacity);
        if (queue == nullptr)
        {
            return nullptr;
        }
        return queue;
    }

    int32_t Push(MonitorRequest value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.size() >= capacity_)
        {
            return kQueueFull; 
        }
        queue_.push(std::move(value));
        return adk::ErrorCode::kSuccess;
    }

    int32_t Pop(MonitorRequest& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty())
        {
            return kQueueEmpty;     
        }
        value = std::move(queue_.front());
        queue_.pop();
        return adk::ErrorCode::kSuccess;
    }

private:
    std::queue<MonitorRequest> queue_;
    const size_t capacity_;
    std::mutex mutex_;
    std::string name_;
};

#endif

static std::ofstream* debug_log_output_file = nullptr;
static boost::mutex*  debug_log_mutex = nullptr;

static bool InitDebugLock()
{
    debug_log_mutex = new boost::mutex();    
    return false;
}

static void InitDebugLogger()
{
    static bool s_is_init = InitDebugLock();

    boost::mutex::scoped_lock lock_guard(*debug_log_mutex);
    if (s_is_init)
    {
        return;
    }
    s_is_init = true;

    const char* log_dir_cstr = std::getenv("ADK_MONITOR_DEBUG_LOG_DIR");
    if (log_dir_cstr == nullptr)
    {
        return;
    }
    std::string log_dir = log_dir_cstr;

    std::string log_file_name;
    const char* log_file_name_cstr = std::getenv("ADK_MONITOR_DEBUG_LOG_FILE_NAME");
    if (log_file_name_cstr == nullptr)
    {
        pid_t this_pid = ADK_GET_PID;
        log_file_name = boost::lexical_cast<std::string>(this_pid);
    }
    else
    {
        log_file_name = log_file_name_cstr;
    }
    
    boost::gregorian::date current_date(boost::gregorian::day_clock::local_day());

    try 
    {
        boost::system::error_code ec;
        if (!boost::filesystem::exists(log_dir, ec))
        {
            boost::filesystem::create_directories(log_dir, ec);
        }

        debug_log_output_file = new std::ofstream(
                        (boost::format("%1%/debug_monitor_%2%_%3%.log")
                                % log_dir
                                % log_file_name
                                % boost::gregorian::to_iso_extended_string(current_date)).str(),
                        std::ios_base::out | std::ios_base::app);

        if (!(debug_log_output_file->good()))
        {
            delete debug_log_output_file;
            debug_log_output_file = nullptr;
        }
    } 
    catch (...) 
    {
        if (debug_log_output_file)
        {
            delete debug_log_output_file;
            debug_log_output_file = nullptr;
        }
    }

    if (debug_log_output_file == nullptr)
    {
        debug_log_output_file = new std::ofstream(std::string("./log_context_anonymous_")
                                           + boost::gregorian::to_iso_extended_string(current_date) + ".log");
        if (!(debug_log_output_file->good()))
        {
            delete debug_log_output_file;
            debug_log_output_file = nullptr;
            return;
        }
    }
}

#define MONITOR_DEBUG_LOG(msg) do   \
{   \
    if (debug_log_mutex != nullptr && debug_log_output_file != nullptr) \
    {   \
        boost::mutex::scoped_lock lock_guard(*debug_log_mutex);     \
        (*debug_log_output_file) << "@ " << boost::posix_time::ptime(boost::posix_time::microsec_clock::local_time()) << " "    \
                                         << __FUNCTION__ << " " \
                                         << __LINE__ << " | " \
                                         << msg \
                                         << std::endl;  \
    }   \
} while (false);

MonitorOps::MonitorOps()
{
    is_collection_indicator = false;
    static const char* s_env_mon_interval = std::getenv("ADK_MONITOR_INTERVAL_MILLI");
    if (s_env_mon_interval != NULL)
    {
        try 
        {
            collection_interval_milli = boost::lexical_cast<int32_t>(s_env_mon_interval);
            return;
        }
        catch(...)
        {
            MONITOR_DEBUG_LOG("monitor interval is invalid, "
                              << boost::current_exception_diagnostic_information());
        }
    }
    collection_interval_milli = ADK_DEFAULT_COLLECTION_INTERVAL_MILLI;
}

class MonitorService
{
public:
    MonitorService() 
        : service_(),
          service_work_(service_)
    {
        is_running_ = false;
        sinker_ = NULL;
        new_sinker_ = NULL;
        detach_sinker_ = NULL;
        nr_indicators_ = 0;
        sinkers_.reserve(4);
        detach_ok_ = false;
        detach_fail_ = false;
        nr_query_excepts_ = 0;
        nr_event_excepts_ = 0;
        nr_sink_excepts_ = 0;
        nr_other_excepts_ = 0;
    }

    ~MonitorService()
    {}

    void Init()
    {
        query_channel_ = MPSCQueue::Create<MonitorRequest>("MonitorRequestQueue", 1024);
    }

    void Run()
    {
        is_running_ = true;
        is_suspended_ = false;
        is_waitting_suspend_ = false;

#if defined(__GNUC__)
        monitor_thread_ = boost_thread("adk-monitor", "monitor thread", boost::bind(&MonitorService::MonitorMain, this));
#elif defined(_MSC_VER)
        monitor_thread_ = std::thread(boost::bind(&MonitorService::MonitorMain, this));
#endif
    }

    void Stop()
    {
        is_running_ = false;
        if (monitor_thread_.joinable())
            monitor_thread_.join();
    }

    int32_t Suspend()
    {
        if (!is_running_)
        {
            return ErrorCode::kFailure;
        }
        assert(!is_waitting_suspend_);
     
        boost::mutex::scoped_lock lock_guard(monitor_mutex_);
        is_waitting_suspend_ = true;

        return ErrorCode::kSuccess;
        // when call return, MonitorMain thread in ExIdle usleep
    }

    void Resume()
    {
        boost::mutex::scoped_lock lock_guard(monitor_mutex_);
        is_suspended_ = false;
    }

    void Idle(boost::mutex::scoped_lock& lock_guard) 
    {
        if (new_sinker_ != sinker_)
        {
            sinker_ = new_sinker_;
            sinkers_.push_back(sinker_);
        }

        if (detach_sinker_ != NULL)
        {
            auto it = sinkers_.begin();
            while (it != sinkers_.end())
            {
                if (*it == detach_sinker_)
                {
                    sinkers_.erase(it);
                    detach_sinker_ = NULL;
                    ADK_BARRIER();
                    detach_ok_ = true;
                    break;
                }
                ++it;
            }

            if (detach_sinker_ != NULL)
            {
                detach_sinker_ = NULL;
                ADK_BARRIER();
                detach_fail_ = true;
            }
        }

        // if (new_service_ != service_)
        // {
        //     service_ = new_service_;
        // }
        ExIdle(lock_guard);
    }

    inline void CheckSuspend(boost::mutex::scoped_lock& lock_guard)
    {
        if (ADK_UNLIKELY(is_waitting_suspend_))
        {
            is_suspended_ = true;
            lock_guard.unlock();
            while (is_suspended_)
            {
                usleep(100);
            }
            ADK_BARRIER();
            is_waitting_suspend_ = false;
            lock_guard.lock();
        }
    }

    virtual void ExIdle(boost::mutex::scoped_lock& lock_guard) 
    {
        lock_guard.unlock();
        usleep(200);
        lock_guard.lock();
        CheckSuspend(lock_guard);
    }

    void SetShardingIndex(int32_t sharding_index, int32_t sharding_number)
    {
        if (sharding_index < 0)
        {
            MONITOR_DEBUG_LOG("sharding index <" << sharding_index << "> is illegal");
            return;
        }
        sharding_index_ = sharding_index;
        sharding_number_ = sharding_number;
    }

private:
    volatile bool     is_running_;
    volatile bool     is_suspended_;
    volatile bool     is_waitting_suspend_;
    MPSCQueue*        query_channel_;
#if defined(__GNUC__)
    boost::thread monitor_thread_;
#elif defined(_MSC_VER)
    std::thread monitor_thread_;
#endif
    uint64_t         nr_indicators_;
    IMonitorSinker* sinker_;
    IMonitorSinker* new_sinker_;
    IMonitorSinker* detach_sinker_;
    bool            detach_ok_;
    bool            detach_fail_;
    std::vector<IMonitorSinker*>     sinkers_;
    boost::asio::io_service            service_;
    boost::system::error_code           service_ec_;
    boost::asio::io_service::work     service_work_;
    boost::property_tree::ptree     collection_tree_;
    std::vector<std::string>         splits_;
    std::vector<std::string>         splits2_;
    std::vector<std::string>         splits3_;
    boost::mutex                    monitor_mutex_;
    boost::mutex                    plugin_mutex_;

    uint32_t                     collection_interval_milli_;
    MonitorClassMap                class_map_;
    MonitorObjectMap            object_flat_map_;
    uint64_t                    nr_query_excepts_;
    uint64_t                    nr_event_excepts_;
    uint64_t                    nr_sink_excepts_;
    uint64_t                    nr_other_excepts_;

    int32_t                     sharding_index_ = kInvalidShardingIndex;
    int32_t                     sharding_number_ = 0;

    void MonitorMain();
    bool RunIoService();
    bool ProcessRequest(const string& url, const int32_t query_type, const boost::property_tree::ptree& query_condition, boost::property_tree::ptree& reply);
    void DoIndicatorCollection(const boost::system::error_code& ec, const std::string* class_name, ClassNode* class_node);

    // helper
    void inline SetupTimer(const std::string* class_name, ClassNode* class_node)
    {
        class_node->timer->expires_from_now(std::chrono::milliseconds(class_node->collection_interval_milli));
        class_node->timer->async_wait(boost::bind(&MonitorService::DoIndicatorCollection,
                                                      this, _1, class_name, class_node));
    }

    void Receive(IMonitorSinker::Type type, uint64_t query_key, const boost::property_tree::ptree& content)
    {
        for (auto& sinker : sinkers_)
        {
            sinker->Receive(type, query_key, content);
        }
    }

    friend class Monitor;
};

void MonitorService::MonitorMain()
{
    MONITOR_DEBUG_LOG("adk monitor thread is running");

    boost::property_tree::ptree ptree;
    boost::property_tree::ptree reply;
    boost::property_tree::ptree* reply_ptr = NULL;
    boost::property_tree::ptree* condition_ptr = NULL;
    MonitorRequest                 req;
    std::string                 url;
    int32_t                     query_type;
    bool                         is_success;
    uint64_t                    query_key;

    try 
    {
        while (is_running_)
        {
            boost::mutex::scoped_lock lock_guard(monitor_mutex_);

            is_success = false;
            if (query_channel_->Pop(req) == ErrorCode::kSuccess)
            {
                ptree.clear();
                reply.clear();
                url.clear();
                reply_ptr = &reply;
                condition_ptr = &ptree;

                if (req.req_from == IMonitorSinker::kEvent 
                    || req.req_from == IMonitorSinker::kIndicator)
                {
                    try 
                    {
                        query_key = 0;
                        switch(req.req_type)
                        {
                        case RequestType::kPtree:
                            is_success = true;
                            *reply_ptr = *(reinterpret_cast<boost::property_tree::ptree*>(req.user_data));
                            delete (boost::property_tree::ptree*)req.user_data;
                            break;
                        case RequestType::kFunction:
                            is_success = (reinterpret_cast<EventFunctionType>(req.func))(req.user_data, *reply_ptr);
                            break;
                        case RequestType::kFunctor:
                            is_success = (*reinterpret_cast<EventFunctorType*>(req.func))(*reply_ptr);    
                        }    
                    }
                    catch (...)
                    {
                        MONITOR_DEBUG_LOG("receive invalid event, "
                                          << boost::current_exception_diagnostic_information());
                        ++nr_event_excepts_;
                        is_success = false;
                    }
                }
                else
                {
                    try 
                    {
                        switch(req.req_type)
                        {
                        assert(req.req_from == IMonitorSinker::kAmiQuery || req.req_from == IMonitorSinker::kHttpQuery);
                        case RequestType::kPtree:
                            condition_ptr = reinterpret_cast<boost::property_tree::ptree*>(req.user_data);
                            url = *reinterpret_cast<std::string*>(req.func);
                            query_type = req.query_type;
                            query_key = req.query_key;
                            is_success = true;
                            break;
                        case RequestType::kFunction:
                            is_success = (reinterpret_cast<QueryFunctionType>(req.func))(req.user_data, query_key, url, *condition_ptr, query_type);
                            break;
                        case RequestType::kFunctor:
                            is_success = (*reinterpret_cast<QueryFunctorType*>(req.func))(query_key, url, *condition_ptr, query_type);
                        }

                        if (is_success == true)
                        {
                            is_success = ProcessRequest(url, query_type, *condition_ptr, *reply_ptr);
                        }
                    }
                    catch (...)
                    {
                        MONITOR_DEBUG_LOG("receive invalid query, "
                                          << boost::current_exception_diagnostic_information());
                        ++nr_query_excepts_;
                        is_success = false;
                    }
                }

                try 
                {
                    if (sinker_ != NULL)
                    {
                        if (!is_success)
                        {
                            (*reply_ptr).clear();
                            (*reply_ptr).put("url", url);
                            (*reply_ptr).put("status", "failure");
                        }
                        // sinker_->Receive(req.req_from, query_key, *reply_ptr);
                        // FIXME: release lock before sink?
                        Receive(req.req_from, query_key, *reply_ptr);
                    }
                }
                catch (...)
                {
                    MONITOR_DEBUG_LOG("receive event fail with exception, " 
                                      << boost::current_exception_diagnostic_information());
                    ++nr_sink_excepts_;
                }
                continue;
            }

            if (!RunIoService() && !is_success)
                Idle(lock_guard);
        }
    }
    catch (...)
    {
        MONITOR_DEBUG_LOG("monitor loop fail with exception, " 
                           << boost::current_exception_diagnostic_information());
        is_running_ = false;
        ++nr_other_excepts_;
    }

    MONITOR_DEBUG_LOG("monitor loop is stopped");
}

bool MonitorService::RunIoService()
{
    return (service_.poll_one(service_ec_) != 0);
}

void MonitorService::DoIndicatorCollection(const boost::system::error_code& ec, const std::string* class_name, ClassNode* class_node)
{
    // ADD CODE
    
    do {
        if (!(class_node->is_collection_indicator))
            break;

        collection_tree_.clear();
        collection_tree_.put("class_name", *class_name);
        if (sharding_index_ != kInvalidShardingIndex)
        {
            collection_tree_.put("sharding_index", sharding_index_);
            collection_tree_.put("sharding_number", sharding_number_);
        }
        
        boost::property_tree::ptree& objects_tree = collection_tree_.add_child("class_objects", boost::property_tree::ptree());

        ObjectNodeMap& obj_node_map = class_node->object_map;
        if (obj_node_map.empty())
            break;

        for (auto it = obj_node_map.begin(); it != obj_node_map.end(); ++it)
        {
            ObjectNode* obj_node = it->second;
            if (obj_node->monitor_ops.is_collection_indicator)
            {
                boost::property_tree::ptree& obj_tree = objects_tree.push_back(
                    boost::property_tree::ptree::value_type("", boost::property_tree::ptree()))->second;
                obj_tree.put("object_name", it->first);
                auto& on_collection_indicator = obj_node->monitor_ops.on_collection_indicator;
                bool status = false;
                if (on_collection_indicator)
                {
                    try
                    {
                        status = on_collection_indicator(obj_tree);    
                    }
                    catch(...)
                    {
                        MONITOR_DEBUG_LOG("collector " << it->first
                                          << " exception, "
                                          << boost::current_exception_diagnostic_information());
                    }
                }

                if (status)
                {
                    if (obj_tree.size() == 1)
                    {
                        objects_tree.pop_back();
                    }
                }
                else
                {
                    obj_tree.put("object_collection_status", "failure");
                }
            }
        }

        ++nr_indicators_;

        if (sinker_ != NULL)
        {
            // sinker_->Receive(IMonitorSinker::kIndicator, nr_indicators_, collection_tree_);
            try 
            {
                Receive(IMonitorSinker::kIndicator, nr_indicators_, collection_tree_);    
            }
            catch(...)
            {
                MONITOR_DEBUG_LOG("receive indicator fail with exception, "
                                  << boost::current_exception_diagnostic_information());
            }
        }
            
    } while (false);

    // reset timer
    SetupTimer(class_name, class_node);
}

bool MonitorService::ProcessRequest(const string& url, const int32_t query_type, const boost::property_tree::ptree& query_condition, boost::property_tree::ptree& reply)
{
    splits_.clear();
    boost::split(splits_, url, boost::is_any_of(": "), boost::token_compress_on);
    if (splits_.size() < 2)
    {
        return false;
    }

    if (splits_[0] == "heir")
    {
        splits2_.clear();
        boost::split(splits2_, splits_[1], boost::is_any_of("/ "), boost::token_compress_on);
        if (splits2_.size() == 0)
            return false;

        splits_.clear();
        MonitorClassMap* class_map_ptr = &class_map_;
        for (uint32_t i = 0; i < splits2_.size(); ++i)
        {
            boost::split(splits_, splits2_[i], boost::is_any_of("@ "), boost::token_compress_on);
            if (splits_.size() != 2)
                return false;

            auto it = class_map_ptr->find(splits_[0]);
            if (it == class_map_ptr->end())
                return false;

            auto obj_it = it->second->object_map.find(splits_[1]);
            if (obj_it == it->second->object_map.end())
                return false;

            if (i == splits2_.size() - 1)
            {
                reply.clear();
                reply.put("url", url);
                reply.put("class_name", it->first);
                reply.put("object_name", obj_it->first);
                boost::property_tree::ptree& result_tree = reply.add_child("result", boost::property_tree::ptree());
                auto& on_query = obj_it->second->monitor_ops.on_query;
                bool status = false;
                if (on_query)
                {
                    status = on_query(query_type, query_condition, result_tree);
                }
                
                reply.put("status", (status ? "success": "failure"));
                return true;
            }

            class_map_ptr = obj_it->second->childs;
            if (class_map_ptr == NULL)
                return false;
        }
    }
    else if (splits_[0] == "flat")
    {
        // FIXME: add log info if query failed
        splits2_.clear();
        boost::split(splits2_, splits_[1], boost::is_any_of(", "), boost::token_compress_on);
        if (splits2_.size() == 0)
            return false;

        reply.clear();
        reply.put("url", url);
        boost::property_tree::ptree& objects_tree = reply.add_child("objects", boost::property_tree::ptree());
        bool status = false;

        for (uint32_t i = 0; i < splits2_.size(); ++i)
        {
            boost::property_tree::ptree& result_tree = objects_tree.push_back(
                    boost::property_tree::ptree::value_type("", boost::property_tree::ptree()))->second;

            result_tree.put("object_name", splits2_[i]);
            boost::property_tree::ptree& result_vector_tree = result_tree.add_child("result", boost::property_tree::ptree());

            auto flat_it = object_flat_map_.find(splits2_[i]);
            if (flat_it == object_flat_map_.end())
                continue;

            auto flat_class_name_it = flat_it->second.begin();
            while (flat_class_name_it != flat_it->second.end())
            {
                boost::property_tree::ptree& obj_class_tree = result_vector_tree.push_back(
                    boost::property_tree::ptree::value_type("", boost::property_tree::ptree()))->second;
                obj_class_tree.put("class_name", flat_class_name_it->first);

                auto& on_query = (*(flat_class_name_it->second)).monitor_ops.on_query;
                if (on_query)
                {
                    status = (on_query(query_type, query_condition, obj_class_tree) || status);
                }
                ++flat_class_name_it;
            }
        }
        reply.put("status", (status ? "success": "failure"));
        return true;
    }
    else if (splits_[0] == "cfg")
    {
        splits2_.clear();
        boost::split(splits2_, splits_[1], boost::is_any_of("/ "), boost::token_compress_on);
        if (splits2_.size() != 2)
            return false;

        splits3_.clear();
        boost::split(splits3_, splits2_[0], boost::is_any_of("@ "), boost::token_compress_on);
        if (splits3_.size() == 1)
        {
            // search in class or object map;
            return true;
        }
        else if (splits3_.size() == 2)
        {
            // search in class map;
            splits_.clear();
            boost::split(splits_, splits2_[1], boost::is_any_of("= "), boost::token_compress_on);
            if (splits_.size() != 2)
                return false;

            auto it = class_map_.find(splits3_[0]);
            if (it == class_map_.end())
                return false;

            auto obj_it = it->second->object_map.find(splits3_[1]);
            if (obj_it == it->second->object_map.end())
                return false;

            bool status = false;
            reply.clear();
            reply.put("url", url);

            if (splits_[0] == "is_collection_indicator")
            {
                (obj_it->second)->monitor_ops.is_collection_indicator = boost::lexical_cast<bool>(splits_[1]);
                it->second->is_collection_indicator = false;
                for (auto obj_it2 = it->second->object_map.begin(); obj_it2 != it->second->object_map.end(); ++obj_it2)
                {
                    it->second->is_collection_indicator = (obj_it2->second)->monitor_ops.is_collection_indicator;
                    if (it->second->is_collection_indicator == true)
                        break;
                }
                status = true;
            }
            else if (splits_[0] == "collection_interval_milli")
            {
                (obj_it->second)->monitor_ops.collection_interval_milli = boost::lexical_cast<uint32_t>(splits_[1]);
                it->second->collection_interval_milli = (obj_it->second)->monitor_ops.collection_interval_milli;
                status = true;
            }

            reply.put("status", (status ? "success": "failure"));
            return true;
        }
    }
    return false;
}


MonitorService* g_monitor_service = NULL;
static uint64_t g_ref_counter = 0;
static volatile bool g_is_monitor_running = false;

int32_t Monitor::Start()
{
    atomic_inc(g_ref_counter);
    conditional_call_once([]() { return !g_is_monitor_running; },
                          []() {
                                  InitDebugLogger();
                                  g_monitor_service = new MonitorService();
                                  g_monitor_service->Init();
                                  g_monitor_service->Run(); 
                                  g_is_monitor_running = true; });

    return ErrorCode::kSuccess;
}

int32_t Monitor::Stop()
{
    if (atomic_dec(g_ref_counter) != 1)
    {
        return ErrorCode::kSuccess;
    }

    conditional_call_once([]() { return g_is_monitor_running; },
                          []() { 
                                  g_monitor_service->Stop();
                                  g_is_monitor_running = false; });

    return ErrorCode::kSuccess;
}

int32_t Monitor::Suspend()
{
    if (!g_is_monitor_running)
    {
        return g_monitor_service->Suspend();
    }
    return ErrorCode::kFailure;
}

void Monitor::Resume()
{
    if (g_monitor_service)
    {
        g_monitor_service->Resume();
    }
}

void Monitor::SetShardingIndex(int32_t sharding_index, int32_t sharding_number)
{
    g_monitor_service->SetShardingIndex(sharding_index, sharding_number);
}

EventChannel* Monitor::RegisterObject(const std::string& class_name, const std::string& object_name, MonitorOps* monitor_ops,
                                      const std::string& parent_class, const std::string& parent_name)
{
    if (g_monitor_service == NULL)
    {
        Monitor::Start();
    }

    boost::mutex::scoped_lock lock_guard(g_monitor_service->monitor_mutex_);

    auto it = g_monitor_service->class_map_.find(class_name);
    if (it == g_monitor_service->class_map_.end())
    {
        it = g_monitor_service->class_map_.insert(MonitorClassMap::value_type(class_name, new ClassNode())).first;
        ClassNode* class_node = it->second;
        class_node->collection_interval_milli = monitor_ops->collection_interval_milli;
        class_node->is_collection_indicator = monitor_ops->is_collection_indicator;
        class_node->timer = new boost::asio::steady_timer(g_monitor_service->service_);
        g_monitor_service->SetupTimer(&(it->first), class_node);
    }
    else 
    {
        (it->second)->collection_interval_milli = monitor_ops->collection_interval_milli;
        (it->second)->is_collection_indicator = ((it->second)->is_collection_indicator || monitor_ops->is_collection_indicator);
    }

    ObjectNodeMap* obj_map = &((it->second)->object_map);
    auto obj_it = obj_map->find(object_name);

    ObjectNode* obj_node;
    if (obj_it == obj_map->end())
    {
        obj_node = new ObjectNode();
        obj_it = obj_map->insert(ObjectNodeMap::value_type(object_name, obj_node)).first;
        obj_node->monitor_ops = *monitor_ops;

        obj_node->channel = new EventChannel();
        obj_node->channel->event_sinker_ = (void*)(g_monitor_service->query_channel_);
    }
    else // return prev create event channel.
    {
        obj_node = obj_it->second;
    }

    if (!parent_class.empty() && !parent_name.empty())
    {
        // FIXME: ADD parent and childs relationship
    }

    auto flat_it = g_monitor_service->object_flat_map_.find(object_name);
    if (flat_it == g_monitor_service->object_flat_map_.end())
    {
        flat_it = g_monitor_service->object_flat_map_.insert(MonitorObjectMap::value_type(object_name, ObjectNodeMap())).first;
    }

    auto flat_class_name_it = flat_it->second.find(class_name);
    if (flat_class_name_it == flat_it->second.end())
    {
        flat_class_name_it = flat_it->second.insert(ObjectNodeMap::value_type(class_name, obj_node)).first;
    }

    return obj_node->channel;
}

int32_t Monitor::UnregisterObject(const std::string& class_name, const std::string& object_name, 
                                  const std::string& parent_class, const std::string& parent_name)
{
    if (g_monitor_service == NULL)
        return ErrorCode::kInvalidInvoke;

    boost::mutex::scoped_lock lock_guard(g_monitor_service->monitor_mutex_);
    auto it = g_monitor_service->class_map_.find(class_name);
    if (it == g_monitor_service->class_map_.end())
    {
        return ErrorCode::kInvalidInvoke;
    }
    
    ObjectNodeMap* obj_map = &((it->second)->object_map);
    auto obj_it = obj_map->find(object_name);
    if (obj_it == obj_map->end())
    {
        return ErrorCode::kInvalidInvoke;
    }

    // FIXME : shall we delete obj_it->second?    
    obj_map->erase(obj_it);

    auto flat_it = g_monitor_service->object_flat_map_.find(object_name);
    if (flat_it == g_monitor_service->object_flat_map_.end())
    {
        return ErrorCode::kFailure;    // FIXME : bug on?
    }

    auto flat_class_name_it = flat_it->second.find(class_name);
    if (flat_class_name_it == flat_it->second.end())
    {
        return ErrorCode::kFailure;    // FIXME : bug on?
    }

    flat_it->second.erase(flat_class_name_it);

    // FIXME : shall we delete empty ClassNode and stop timer?    

    return ErrorCode::kSuccess;
}

// FIXME: multithread scenario!
int32_t Monitor::PluginSinker(IMonitorSinker* sinker)
{
    if (g_monitor_service == NULL || sinker == NULL)
        return ErrorCode::kInvalidInvoke;

    boost::mutex::scoped_lock lock_guard(g_monitor_service->plugin_mutex_);

    // IMonitorSinker* prev_sinker = g_monitor_service->sinker_;
    g_monitor_service->new_sinker_ = sinker;

    while (/*prev_sinker != NULL &&*/ g_monitor_service->new_sinker_ != g_monitor_service->sinker_)
    {
        if (!(g_monitor_service->is_running_))
            return ErrorCode::kFailure;
        lock_guard.unlock();
        usleep(1000);
        lock_guard.lock();
    }
    return ErrorCode::kSuccess;
}

static int32_t detach_counter = 0;
int32_t Monitor::PlugoutSinker(IMonitorSinker* sinker)
{
    if (g_monitor_service == NULL || sinker == NULL)
        return ErrorCode::kFailure;

    boost::mutex::scoped_lock lock_guard(g_monitor_service->monitor_mutex_);    // FIXME: urgly code! fix with MailBox
    ++detach_counter;
    while (detach_counter != 1)
    {
        lock_guard.unlock();
        usleep(1000);
        lock_guard.lock();
    }

    g_monitor_service->detach_fail_ = false;
    g_monitor_service->detach_ok_ = false;
    g_monitor_service->detach_sinker_ = sinker;

    while (!(g_monitor_service->detach_ok_) 
           && g_monitor_service->is_running_)
    {
        if (g_monitor_service->detach_fail_)
        {
            --detach_counter;
            return ErrorCode::kFailure;            
        }

        lock_guard.unlock();
        usleep(1000);
        lock_guard.lock();
    }

    --detach_counter;
    return ErrorCode::kSuccess;    
}

int32_t Monitor::SubmitRequest(IMonitorSinker::Type from, QueryFunctionType func, void* user)
{
    if (g_monitor_service == NULL)
        return ErrorCode::kInvalidInvoke;

    MonitorRequest request = {from, RequestType::kFunction, 0, 0, reinterpret_cast<void*>(func), user};
    return g_monitor_service->query_channel_->Push(request);
}

int32_t Monitor::SubmitRequest(QueryFunctionType func, void* user)
{
    return Monitor::SubmitRequest(IMonitorSinker::kAmiQuery, func, user);
}

int32_t Monitor::SubmitRequest(IMonitorSinker::Type from, const QueryFunctorType& func)
{
    if (g_monitor_service == NULL)
        return ErrorCode::kInvalidInvoke;

    QueryFunctorType* new_func = new QueryFunctorType();
    *new_func = func;
    MonitorRequest request = {from, RequestType::kFunctor, 0, 0, reinterpret_cast<void*>(new_func), NULL};
    return g_monitor_service->query_channel_->Push(request);
}

int32_t Monitor::SubmitRequest(const QueryFunctorType& func)
{
    return Monitor::SubmitRequest(IMonitorSinker::kAmiQuery, func);
}

int32_t Monitor::SubmitRequest(IMonitorSinker::Type from, const uint64_t query_key, const std::string& url, const boost::property_tree::ptree& query_condition, const int32_t query_type)
{
    std::string* new_url = new std::string();
    *new_url = url;
    boost::property_tree::ptree* new_ptree = new boost::property_tree::ptree();
    *new_ptree = query_condition;
    MonitorRequest request = {from, RequestType::kPtree, query_type, query_key, reinterpret_cast<void*>(new_url), reinterpret_cast<void*>(new_ptree)};
    return g_monitor_service->query_channel_->Push(request);
}

int32_t Monitor::SubmitRequest(const uint64_t query_key, const std::string& url, const boost::property_tree::ptree& query_condition, const int32_t query_type)
{
    return Monitor::SubmitRequest(IMonitorSinker::kAmiQuery, query_key, url, query_condition, query_type);
}

int32_t Monitor::ChangeCollectionInterval(const std::string& class_name, uint32_t milli)
{
    return ErrorCode::kSuccess;
}

int32_t Monitor::EnableCollection(const std::string& class_name, const std::string& object_name, uint32_t milli)
{
    return ErrorCode::kSuccess;
}

int32_t EventChannel::PushEvent(EventFunctionType func, void* user)
{
    if (g_monitor_service == NULL)
        return ErrorCode::kInvalidInvoke;

    MonitorRequest request = {IMonitorSinker::kEvent, RequestType::kFunction, 0, 0, reinterpret_cast<void*>(func), user};
    return reinterpret_cast<MPSCQueue*>(event_sinker_)->Push(request);
}

int32_t EventChannel::PushEvent(const EventFunctorType& func)
{
    if (g_monitor_service == NULL)
        return ErrorCode::kInvalidInvoke;

    EventFunctorType* new_func = new EventFunctorType();
    *new_func = func;
    MonitorRequest request = {IMonitorSinker::kEvent, RequestType::kFunctor, 0, 0, reinterpret_cast<void*>(new_func), NULL};
    return reinterpret_cast<MPSCQueue*>(event_sinker_)->Push(request);
}

int32_t EventChannel::PushEvent(const boost::property_tree::ptree& ptree)
{
    if (g_monitor_service == NULL)
        return ErrorCode::kInvalidInvoke;

    boost::property_tree::ptree* new_ptree = new boost::property_tree::ptree();
    *new_ptree = ptree;
    MonitorRequest request = {IMonitorSinker::kEvent, RequestType::kPtree, 0, 0, NULL, reinterpret_cast<void*>(new_ptree)};
    return reinterpret_cast<MPSCQueue*>(event_sinker_)->Push(request);
}

int32_t EventChannel::PushIndicator(const boost::property_tree::ptree& ptree)
{
    if (g_monitor_service == NULL)
        return ErrorCode::kInvalidInvoke;

    boost::property_tree::ptree* new_ptree = new boost::property_tree::ptree();
    *new_ptree = ptree;
    MonitorRequest request = {IMonitorSinker::kIndicator, RequestType::kPtree, 0, 0, NULL, reinterpret_cast<void*>(new_ptree)};
    return reinterpret_cast<MPSCQueue*>(event_sinker_)->Push(request);
}

// int32_t Monitor::set_io_service(boost::asio::io_service* service)
// {
//     if (g_monitor_service == NULL)
//         return ErrorCode::kInvalidInvoke;

//     g_monitor_service->new_service_ = service;
//     return ErrorCode::kSuccess;
// }
} // adk

