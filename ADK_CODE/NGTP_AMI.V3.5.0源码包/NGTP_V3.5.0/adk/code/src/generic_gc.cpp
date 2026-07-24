#include <boost/algorithm/string.hpp>

#include <adk/generic_gc.h>
#include <adk/scoped_lock.h>
#include <adk/entry_wrapper.h>
#include <adk/arch/synchronize.h>

namespace adk_impl
{

static Mutex s_lock = ADK_MUTEX_INITIALIZER;
std::map<std::string, GenericGC*>*  GenericGC::s_gc_map;

#ifdef __ADK_GC_TEST__
uint64_t GCAgent::nr_push_requests_ = 0;
#endif

GenericGC::GenericGC() 
    :   nr_gc_jobs_(0),
        nr_dedicate_channels_(0),
        nr_shared_channels_(0),
        nr_shared_channel_index_(ADK_GGC_SHARED_CHANNEL_BEGIN),
        is_running_(false),
        active_list_(NULL)
{
    memset(channel_info_, 0x00, sizeof(channel_info_));
    gc_params_.mini_gc_period_milli = 1;
}

GenericGC::~GenericGC() {}

void GenericGC::PushOnDedicateChannel(GCRequest* req, GCAgent::PrivateData& priv)
{
    SPSCQueue<GCRequest*>* channel = (SPSCQueue<GCRequest*>*)(priv.channel);
    int32_t ec;
    do {
        ec = channel->Push(req);
    } while (ec != ErrorCode::kSuccess
             && priv.gc->is_running_);
}

void GenericGC::PushOnSharedChannel(GCRequest* req, GCAgent::PrivateData& priv)
{
    MPSCQueue* channel = (MPSCQueue*)(priv.channel);
    int32_t ec;
    do {
        ec = channel->Push(req);
    } while (ec != ErrorCode::kSuccess
             && priv.gc->is_running_);
}

GCAgent* GenericGC::CreateGCAgent(bool using_dedicated_channel,
                                  bool immediate_start,
                                  uint32_t channel_depth)
{
    boost::mutex::scoped_lock lock_guard(gc_lock_);
    if (immediate_start)
        Start();

    if (using_dedicated_channel 
        && nr_dedicate_channels_ < ADK_GGC_SHARED_CHANNEL_BEGIN)
    {
        channel_info_[nr_dedicate_channels_].channel = SPSCQueue<GCRequest*>::Create(
                            "gc_dedi_channel_" + boost::lexical_cast<std::string>(nr_dedicate_channels_),
                            channel_depth);
        auto* ret_agent = new GCAgent();
        ret_agent->priv.gc = this;
        ret_agent->priv.channel = channel_info_[nr_dedicate_channels_].channel;
        ret_agent->send_req = &GenericGC::PushOnDedicateChannel;
        ADK_BARRIER();
        ++nr_dedicate_channels_;
        return ret_agent;
    }

    if (channel_info_[nr_shared_channel_index_].channel == NULL)
    {
        channel_info_[nr_shared_channel_index_].channel = SPSCQueue<GCRequest*>::Create(
                            "gc_share_channel_" + boost::lexical_cast<std::string>(nr_shared_channels_),
                            channel_depth);
        ADK_BARRIER();
        ++nr_shared_channels_;
    }

    auto* ret_agent = new GCAgent();
    ret_agent->priv.gc = this;
    ret_agent->priv.channel = channel_info_[nr_shared_channel_index_].channel;
    ret_agent->send_req = &GenericGC::PushOnSharedChannel;
    ++nr_shared_channel_index_;
    if (nr_shared_channel_index_ == ADK_GGC_MAX_CHANNELS)
    {
        nr_shared_channel_index_ = ADK_GGC_SHARED_CHANNEL_BEGIN;
    }
    return ret_agent;
}

GenericGC* GenericGC::GetGC(const std::string& gc_name)
{
    GenericGC* gc_obj;
    {
        ScopedLock lock_guard(s_lock);
        if (s_gc_map == nullptr)
            s_gc_map = new std::map<std::string, GenericGC*>();

        auto it = s_gc_map->find(gc_name);
        if (it == s_gc_map->end())
        {
            gc_obj = new GenericGC();
            s_gc_map->insert(std::make_pair(gc_name, gc_obj));
        }
        else
        {
            gc_obj = it->second;
        }    
    }
    return gc_obj;
}

GCAgent* GenericGC::CreateGCAgent(const std::string& gc_name,
                                  bool using_dedicated_channel,
                                  bool immediate_start,
                                  uint32_t channel_depth)
{
    return GetGC(gc_name)->CreateGCAgent(using_dedicated_channel, immediate_start, channel_depth);
}

void GenericGC::Start(const std::string& gc_name)
{
    auto* gc_obj = GetGC(gc_name);
    boost::mutex::scoped_lock lock_guard(gc_obj->gc_lock_);
    gc_obj->Start();
}

void GenericGC::Start()
{
    if (is_running_)
        return;

    is_running_ = true;
    gc_thread_ = boost_thread("adk-genericgc", "generic gc thread", std::bind(&GenericGC::Run, this));
}


void GenericGC::Finish(const std::string& gc_name)
{
    GetGC(gc_name)->Finish();
}

void GenericGC::Finish()
{
    {
        boost::mutex::scoped_lock lock_guard(gc_lock_);
        if (!is_running_)
            return;

        is_running_ = false;
    }
    
    if (gc_thread_.joinable())
        gc_thread_.join();
}

void GenericGC::DoGC(ChannelInfo* cinfo)
{
    GCRequest* gc_req;
    if (cinfo->channel->Pop(gc_req) == ErrorCode::kSuccess)
    {
        cinfo->next = active_list_;
        active_list_ = cinfo;
        gc_req->DoGC();
        ++nr_gc_jobs_;
    }
}

void GenericGC::ChangeParams(const std::string& gc_name,
                             GenericArg& arg1)
{
    if (arg1 == "MiniGCPeriodMilli")
    {
        uint32_t value = arg1.any_cast<uint32_t>();
        GetGC(gc_name)->gc_params_.mini_gc_period_milli = value;
    }
}

void GenericGC::Run()
{
    while (is_running_)
    {
        active_list_ = NULL;
        auto nr_dedi_chn = nr_dedicate_channels_;
        for (uint32_t i = 0; i < nr_dedi_chn; ++i)
        {
            DoGC(&channel_info_[i]);
        }

        auto nr_shared_chn = nr_shared_channels_ + ADK_GGC_SHARED_CHANNEL_BEGIN;
        for (uint32_t i = ADK_GGC_SHARED_CHANNEL_BEGIN; i < nr_shared_chn; ++i)
        {
            DoGC(&channel_info_[i]);
        }

        if (active_list_ != NULL)
        {
            constexpr uint32_t poll_active_budget = 128;
            uint32_t nr_jobs_ = 0;
            do {
                ChannelInfo*  active_list_temp_ = active_list_;
                active_list_ = NULL;

                do {
                    ChannelInfo* cifno = active_list_temp_;
                    active_list_temp_ = active_list_temp_->next;
                    DoGC(cifno);
                    ++nr_jobs_;
                } while (active_list_temp_ != NULL);    

            } while (active_list_ != NULL && nr_jobs_ < poll_active_budget);
        } 
        else
        {
            if (ACCESS_ONCE(gc_params_.mini_gc_period_milli) != 0)
            {
                usleep(gc_params_.mini_gc_period_milli * 1000);
            }
            else
            {
                for (uint32_t i = 64 ; i != 0; --i)
                ADK_PAUSE();
            }
        }
    }   
}

void GenericGC::Dump(const std::string& gc_name, boost::property_tree::ptree& ptree)
{
    auto* gc_obj = GetGC(gc_name);
    gc_obj->Dump(ptree);
}

void GenericGC::Dump(boost::property_tree::ptree& ptree)
{
    ptree.put("dedi_chn_num", nr_dedicate_channels_);
    ptree.put("shar_chn_num", nr_shared_channels_);
    ptree.put("gc_jobs_num", nr_gc_jobs_);
}

} // adk

