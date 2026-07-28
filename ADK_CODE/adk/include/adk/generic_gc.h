/**
 * @file
 * @brief      a generic implementation of garbage collector
 * @author     zhaonan, zhaonan@archforce.com.cn
 * @date       2018/03/27
 */
#ifndef ADK_IMPL_GENERIC_GC_H_
#define ADK_IMPL_GENERIC_GC_H_

#include "util.h"
#include "generic_arg.h"

#include "lock_free_msg_queue.h"

#include <map>
#include <string>
#include <sstream>

#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace adk_impl
{

#ifndef ADK_GGC_MAX_CHANNELS
#define ADK_GGC_MAX_CHANNELS 12
#endif // ADK_GGC_MAX_CHANNELS

#if ADK_GGC_MAX_CHANNELS < 3
#error ADK_GGC_MAX_CHANNELS should larger than 3
#endif

#define ADK_GGC_MAX_SHARED_CHANNELS     (ADK_GGC_MAX_CHANNELS / 3)
#define ADK_GGC_SHARED_CHANNEL_BEGIN    (ADK_GGC_MAX_CHANNELS - ADK_GGC_MAX_SHARED_CHANNELS)

#define ADK_GGC_CHANNEL_DEPTH (8192)

namespace gc
{
static GenericArg MiniGCPeriodMilli("MiniGCPeriodMilli");

static GenericArg place_holder("place_holder");

constexpr bool kDedicatedChannel = true;
constexpr bool kSharedChannel = false;

constexpr bool kImmediateStart = true;
constexpr bool kDelayStart = false;
} // gc

class GCRequest
{
public:
    virtual void DoGC() = 0;
};

class GenericGC;
class GCAgent
{
public:
    struct PrivateData
    {
        GenericGC* gc;
        SPSCQueue<GCRequest*>* channel;
    };

    GCAgent()
    {
        send_req = NULL;
        priv.gc = NULL;
        priv.channel = NULL;
    }

    inline void PushGCRequest(GCRequest* req)
    {
        #ifdef __ADK_GC_TEST__
        atomic_inc(nr_push_requests_);
        #endif

        send_req(req, priv);
    }

    #ifdef __ADK_GC_TEST__
    static uint64_t nr_push_requests() { return nr_push_requests_; }
    #endif

private:
    void    (*send_req)(GCRequest* req, GCAgent::PrivateData& priv);
    PrivateData priv;

    #ifdef __ADK_GC_TEST__
    static uint64_t nr_push_requests_;
    #endif

    friend class GenericGC;
};

class GenericGC
{
public:   
    static GCAgent* CreateGCAgent(const std::string& gc_name,
                                  bool using_dedicated_channel = false,
                                  bool immediate_start = gc::kImmediateStart,
                                  uint32_t channel_depth = ADK_GGC_CHANNEL_DEPTH);

    static GCAgent* CreateGCAgent(const char* gc_name,
                                  bool using_dedicated_channel = false,
                                  bool immediate_start = gc::kImmediateStart,
                                  uint32_t channel_depth = ADK_GGC_CHANNEL_DEPTH)
    {
        return CreateGCAgent(std::string(gc_name), using_dedicated_channel,
                             immediate_start, channel_depth);
    }

    static void Start(const std::string& gc_name);

    static void Finish(const std::string& gc_name);

    static void Dump(const std::string& gc_name, boost::property_tree::ptree& ptree);

    static std::string Dump(const std::string& gc_name, bool is_pretty = false)
    {
        boost::property_tree::ptree ptree;
        std::ostringstream oss;
        Dump(gc_name, ptree);
        boost::property_tree::json_parser::write_json(oss, ptree, is_pretty);
        return oss.str();
    }

    static void ChangeParams(const std::string& gc_name,
                             GenericArg& arg1 = gc::place_holder);

private:
    GenericGC();

    ~GenericGC();

    struct ChannelInfo
    {
        SPSCQueue<GCRequest*>* channel;
        ChannelInfo*                next;
    };

    uint64_t nr_gc_jobs_;

    uint32_t nr_dedicate_channels_;
    uint32_t nr_shared_channels_;
    uint32_t nr_shared_channel_index_;
    volatile bool   is_running_;
    ChannelInfo*    active_list_;
    ChannelInfo     channel_info_[ADK_GGC_MAX_CHANNELS];
    boost::mutex    gc_lock_;
    boost::thread   gc_thread_;

    struct GCParameters
    {
        uint32_t mini_gc_period_milli;
    };

    GCParameters    gc_params_;

    void Run();
    void DoGC(ChannelInfo* cinfo);
    GCAgent* CreateGCAgent(bool using_dedicated_channel,
                           bool immediate_start,
                           uint32_t channel_depth);

    void Start();
    void Finish();
    void Dump(boost::property_tree::ptree& ptree);

    static GenericGC* GetGC(const std::string& gc_name);
    static void PushOnDedicateChannel(GCRequest* req, GCAgent::PrivateData& priv);
    static void PushOnSharedChannel(GCRequest* req, GCAgent::PrivateData& priv);
    static std::map<std::string, GenericGC*>* s_gc_map;
    friend class GCAgent;
};
} // adk

#endif // ADK_GENERIC_GC_H_
