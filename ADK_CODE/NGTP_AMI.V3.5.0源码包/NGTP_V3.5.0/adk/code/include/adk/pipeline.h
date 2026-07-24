/**
 * @file
 * @brief      a message processing pipeline implement
 * @author     nzhao, nzhao@archforce.com.cn
 * @date       2017/01/19
 */

#ifndef ADK_IMPL_PIPELINE_H_
#define ADK_IMPL_PIPELINE_H_

#include "event.h"
#include "arch/generic.h"
#include "entry_wrapper.h"
#include "lock_free_msg_queue.h"
#include "pipeline_checkpoint.h"

#include <set>
#include <map>
#include <mutex>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <sys/syscall.h>

#include <boost/lexical_cast.hpp>
#include <boost/thread/thread.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/assign/list_inserter.hpp>
#include <boost/utility/identity_type.hpp>
#include <boost/preprocessor/repetition.hpp>
#include <boost/preprocessor/iteration/iterate.hpp>
#include <boost/exception/diagnostic_information.hpp>

namespace adk_impl
{

using std::set;
using std::map;
using std::vector;
using std::string;

#define ADK_PL_MODE_LOADBALANCE     0
#define ADK_PL_MODE_ASSEMBLE        1
#define ADK_PL_MODE_SEQUENCIAL      2
#define ADK_PL_MODE_OUT_OF_ORDER    3
#define ADK_PL_MODE_REORDER         4
#define ADK_PL_MODE_DEFAULT         ADK_PL_MODE_LOADBALANCE

class StageNameMaker
{
public:
    StageNameMaker() 
    {
        id_ = 0;
        prefix_ = "pipeline_stage_";
    }

    ~StageNameMaker() {}

    string GenName()
    {
        return prefix_ + boost::lexical_cast<string>(++id_);
    }

private:
    uint32_t    id_;
    string      prefix_;
};

extern StageNameMaker g_stage_name_maker;

#define ADK_MAX_PIPELINE_PARALLEL           4
#define ADK_MAX_OUTPUT_MESSAGE_TYPE         4

extern void DoChangeToRealtime(int32_t priority, int32_t policy);
extern void DoRestoreToOther();

extern std::string DoChangeCpuAffinity(const std::string& core_list);
extern void DoRestoreCpuAffinity(const std::string& core_list);

class IPrevStageWorker
{
public:
    IPrevStageWorker()
    {
        is_init_ = false;
        worker_thread_ = NULL;
        is_running_ = false;
        tid_ = 0;
        thread_name_ = "adk-pipeline";
    }

    void set_worker_thread(boost::thread* worker_thread) { worker_thread_ = worker_thread; }

    void WrapRun()
    {
        tid_ = syscall(SYS_gettid);
        ADK_BARRIER();
        is_running_ = true;
        Run();
    }

    virtual ~IPrevStageWorker() {}

    virtual void Run() = 0;

    virtual bool OnInit()
    {
        return true;
    }

    virtual void OnExit()
    {}

    void Shutdown()
    {
        is_running_ = false;
    }

    void Wait() 
    { 
        if (worker_thread_ != NULL && worker_thread_->joinable())
        {
            worker_thread_->join();
        }
    }

    virtual bool InitThunk()
    {
        if (OnInit())
        {
            is_init_ = true;
            return true;
        }
        return false;
    }

    virtual void ExitThunk()
    {
        if (is_init_)
            OnExit();
    }

    virtual void GetStats(std::vector<QueueStats>& stats) { stats.clear(); }

    void SetRealtime(int32_t priority, int32_t policy = SCHED_FIFO)
    {
        priority_ = priority;
        policy_ = policy;
    }

    void ChangeToRealtime()
    {
        DoChangeToRealtime(priority_, policy_);
    }

    void SetCpuAffinity(const std::string& cpu_core_list)
    {
        core_list_ = new std::string(cpu_core_list);
    }
    
    std::string ChangeCpuAffinity()
    {
        if (core_list_ != nullptr)
        {
            return DoChangeCpuAffinity(*core_list_);
        }
        return std::string();
    }

    void SetTotalOrderSqn(uint64_t tot_sqn)
    {
        if (shm_chk_point_)
        {
            assert(tot_sqn > 0);
            shm_chk_point_->total_order_sqn = tot_sqn;
        }
    }

    void* business_info()
    {
        assert(shm_chk_point_);
        return (void*)(shm_chk_point_->business_info);
    }

    uint32_t business_info_size()
    {
        assert(shm_chk_point_);
        return shm_chk_point_->business_info_size;
    }

    /**
     * @brief      设置stage的线程名
     *
     * @param[in]  name 线程名
     */
    void set_thread_name(const std::string& name)
    {
        thread_name_ = name;
    }

    const std::string& get_thread_name() const
    {
        return thread_name_;
    }

    bool is_inplace()
    {
        return is_inplace_;
    }

    void set_is_inplace(bool is_inplace)
    {
        is_inplace_ = is_inplace;
    }

    bool    is_init_;
    bool    is_running_ = false;
    bool    is_inplace_ = false;
    int32_t priority_ = 0;
    int32_t policy_ = SCHED_FIFO;
    pid_t tid_;
    std::string  thread_name_;
    ShmCheckPoint* shm_chk_point_ = nullptr;
    map<string, IPrevStageWorker*>  prev_stage_;
    map<string, IPrevStageWorker*>  next_stage_;   
    boost::thread*                  worker_thread_;
    std::string*                    core_list_ = nullptr;
};

template<int assemble, typename InputMessageType>
class PrevStageWorkerBasic : public IPrevStageWorker
{
public:
    typedef InputMessageType InputType;

    PrevStageWorkerBasic()
    {
        assemble_connector_ = NULL;
        rx_msgs_ = 0;
        saved_rx_msgs_ = 0;
        orig_seq_ = 0;
		prev_event_manager_ = nullptr;
    }

    void set_connector(Connector<InputMessageType, assemble>* connector)
    {
        assemble_connector_ = connector;
    }

    void set_connector(void* connector)
    {
        assert(false);
        assemble_connector_ = reinterpret_cast<Connector<InputMessageType, assemble>*>(connector);
    }

    void set_prev_stage_name(const string& name)
    {
        prev_stage_name_ = name;
    }

    string prev_stage_name()
    {
        return prev_stage_name_;
    }

    virtual void GetStats(std::vector<QueueStats>& stats) 
    { 
        stats.clear();
        if (assemble_connector_ != NULL)
        {
            assemble_connector_->GetStats(stats);     
        }
    }

    virtual int worker_mode() { return ADK_PL_MODE_DEFAULT; }

    inline uint64_t receive_messages() { return rx_msgs_; }

    inline void inc_receive_messages() { ++rx_msgs_; }

    inline bool IsReceiveMessage()
    {
        const uint64_t temp_rx_msgs = ACCESS_ONCE(rx_msgs_);
        const bool ret = (saved_rx_msgs_ != temp_rx_msgs);
        saved_rx_msgs_ = temp_rx_msgs;
        return ret;
    }

    inline void set_prev_event_manager(SimpleEventManager* prev_event_manager)
    {
        assert(prev_event_manager);
        prev_event_manager_ = prev_event_manager ;
    }

    virtual Backoff* back_off() = 0;

    inline uint64_t orig_seq() { return orig_seq_; }
    inline void set_orig_seq(uint64_t orig_seq) { orig_seq_ = orig_seq; }

    // interface define

    virtual void OnMessage(InputMessageType& message, short dim, short idx) = 0;

    uint64_t                                    orig_seq_;
    uint64_t                                    rx_msgs_;
    uint64_t                                    saved_rx_msgs_;
    Connector<InputMessageType, assemble>*      assemble_connector_;
    string                                      prev_stage_name_;
    SimpleEventManager*                         prev_event_manager_;
};

template<int assemble, int mode, typename InputMessageType>
class PrevStageWorker : public PrevStageWorkerBasic<assemble, InputMessageType>
{
public:
	virtual int worker_mode() { return mode; }

    virtual Backoff* back_off() = 0 ;

    inline int32_t Receive(InputMessageType& message)
    {
        if (mode == ADK_PL_MODE_LOADBALANCE)
        {
            return PrevStageWorkerBasic<assemble, InputMessageType>::assemble_connector_->Receive(message);
        }

        if (mode == ADK_PL_MODE_SEQUENCIAL)
        {
            return PrevStageWorkerBasic<assemble, InputMessageType>::assemble_connector_->SequencialReceive(message);
        }

        if (mode == ADK_PL_MODE_OUT_OF_ORDER)
        {
            Task<InputMessageType> task;
            int32_t ec = PrevStageWorkerBasic<assemble, InputMessageType>::assemble_connector_->ReceiveTask(task);
            if (ADK_UNLIKELY(ec != ErrorCode::kSuccess))
            {
                return ec;
            }

            message = task.element;
            char* seq_ptr = (char*)&task.seq;
            PrevStageWorkerBasic<assemble, InputMessageType>::set_orig_seq(*((uint64_t*)seq_ptr));
            return ec;
        }

        if (mode == ADK_PL_MODE_REORDER)
        {
            return PrevStageWorkerBasic<assemble, InputMessageType>::assemble_connector_->ReorderReceive(message);
        }

        return PrevStageWorkerBasic<assemble, InputMessageType>::assemble_connector_->Assemble(message);
    }

    short dim() { return PrevStageWorkerBasic<assemble, InputMessageType>::assemble_connector_->dim(); }
    short idx() { return PrevStageWorkerBasic<assemble, InputMessageType>::assemble_connector_->idx(); }
};

struct GenericNextStageWorker
{
    int32_t NextEntryPoint(void*, short, short) { return ErrorCode::kFailure; }
    typedef decltype(&GenericNextStageWorker::NextEntryPoint) NextEntryPointType;
};

template<int fanout, typename OutputMessageType>
class NextStageWorker
{
public:
    NextStageWorker()
        : next_stage_(nullptr),
          fanout_connector_(NULL)
    {
        memset(&next_entry_point_, 0x00, sizeof(next_entry_point_));
    }

    void set_connector(Connector<OutputMessageType, fanout>* connector)
    {
        fanout_connector_ = connector;
    }

    void set_entry_point(GenericNextStageWorker::NextEntryPointType entry_point, 
                         void* next_stage,
                         uint64_t enable_inplace = 0,
                         ShmCheckPoint** shm_chk_point_ptr = nullptr)
    {
        next_entry_point_ = entry_point;
        next_stage_ = (void*)((uint64_t)next_stage | enable_inplace);
        shm_chk_point_ptr_ = shm_chk_point_ptr;
    }

    inline int32_t DoForward(OutputMessageType& message)
    {
        if (!((uint64_t)next_stage_ & 0x1ul))
        {
            int32_t ret = fanout_connector_->Forward(message);
            next_event_manager_->Notify([](){ return ErrorCode::kSuccess; });
            return ret;    
        }

        if (*shm_chk_point_ptr_)
        {
            if (ADK_UNLIKELY(thread_id_ == 0))
            {
                thread_id_ = syscall(SYS_gettid);
                (*shm_chk_point_ptr_)->thread_id = thread_id_;
            }
            (*shm_chk_point_ptr_)->total_order_sqn = g_pipeline_total_order_seq_num;
        }
        (((GenericNextStageWorker*)((uint64_t)next_stage_ & (~0x1ul)))->*next_entry_point_)((void*)&message, 1, 1);
        return ErrorCode::kSuccess;
    }

    inline int32_t DoForward(OutputMessageType& message, int partition)
    {
        return fanout_connector_->Forward(message, partition);
    }

    inline int32_t DoForward(OutputMessageType& message, short dim, short idx)
    {
        return fanout_connector_->Forward(message, dim, idx);
    }

    inline int32_t DoFanout(OutputMessageType& message, short dim)
    {
        return fanout_connector_->Fanout(message, dim);
    }

    inline int32_t DoReorderForward(OutputMessageType& message, uint64_t seq)
    {
        assert(fanout == 1);
        return fanout_connector_->ReorderForward(message, seq);
    }

    void set_next_stage_name(const string& name)
    {
        next_stage_name_ = name;
    }

    string next_stage_name()
    {
        return next_stage_name_;
    }

    void set_next_event_manager(SimpleEventManager* next_event_manager)
    {
        assert(next_event_manager);
        next_event_manager_ = next_event_manager;
    }

    GenericNextStageWorker::NextEntryPointType      next_entry_point_;
    void*                                    next_stage_;
    Connector<OutputMessageType, fanout>*    fanout_connector_;
    string                                   next_stage_name_;
    SimpleEventManager*                      next_event_manager_;
    ShmCheckPoint** shm_chk_point_ptr_ = nullptr;
    pid_t thread_id_ = 0;
};

#define ADK_PREV_1(MessageType)                      1, ADK_PL_MODE_DEFAULT, MessageType
#define ADK_PREV_2(fanout, MessageType)              fanout, ADK_PL_MODE_DEFAULT, MessageType
#define ADK_PREV_3(fanout, MessageType, Policy)      fanout, Policy, MessageType

#define ADK_PREV_(N, ...) ADK_CONCATENATE(ADK_PREV_, N)(__VA_ARGS__)
#define ADK_INPUT(...) ADK_PREV_(ADK_GET_NARG(__VA_ARGS__), __VA_ARGS__)
#define ADK_RO_INPUT(MessageType)   1, ADK_PL_MODE_REORDER, MessageType

#define ADK_OOO_INPUT_1(MessageType)   1, ADK_PL_MODE_OUT_OF_ORDER, MessageType
#define ADK_OOO_INPUT_2(fanout, MessageType)   fanout, ADK_PL_MODE_OUT_OF_ORDER, MessageType
#define ADK_OOO_INPUT_(N, ...) ADK_CONCATENATE(ADK_OOO_INPUT_, N)(__VA_ARGS__)
#define ADK_OOO_INPUT(...)   ADK_OOO_INPUT_(ADK_GET_NARG(__VA_ARGS__), __VA_ARGS__)

#define ADK_NEXT_1(MessageType)                      1, MessageType
#define ADK_NEXT_2(fanout, MessageType)              fanout, MessageType

#define ADK_NEXT_(N, ...) ADK_CONCATENATE(ADK_NEXT_, N)(__VA_ARGS__)
#define ADK_OUTPUT(...) ADK_NEXT_(ADK_GET_NARG(__VA_ARGS__), __VA_ARGS__)

#define ADK_OUTPUT_1(MessageType) ADK_OUTPUT(MessageType)
#define ADK_OUTPUT_2(MessageType, ...) ADK_OUTPUT(MessageType), ADK_OUTPUT_1(__VA_ARGS__)
#define ADK_OUTPUT_3(MessageType, ...) ADK_OUTPUT(MessageType), ADK_OUTPUT_2(__VA_ARGS__)
#define ADK_OUTPUT_4(MessageType, ...) ADK_OUTPUT(MessageType), ADK_OUTPUT_3(__VA_ARGS__)
#define ADK_OUTPUT_5(MessageType, ...) ADK_OUTPUT(MessageType), ADK_OUTPUT_4(__VA_ARGS__)
#define ADK_OUTPUT_6(MessageType, ...) ADK_OUTPUT(MessageType), ADK_OUTPUT_5(__VA_ARGS__)
#define ADK_OUTPUT_7(MessageType, ...) ADK_OUTPUT(MessageType), ADK_OUTPUT_6(__VA_ARGS__)
#define ADK_OUTPUT_8(MessageType, ...) ADK_OUTPUT(MessageType), ADK_OUTPUT_7(__VA_ARGS__)
#define ADK_OUTPUT_(N, ...) ADK_CONCATENATE(ADK_OUTPUT_, N)(__VA_ARGS__)
#define ADK_OUTPUT_N(...) ADK_OUTPUT_(ADK_GET_NARG(__VA_ARGS__), __VA_ARGS__)

#define ADK_IO_1(IMessageType)    ADK_INPUT(IMessageType)
#define ADK_IO_2(IMessageType, ...)     ADK_INPUT(IMessageType), ADK_OUTPUT_N(__VA_ARGS__)
#define ADK_IO_3(...)   ADK_IO_2(__VA_ARGS__)
#define ADK_IO_4(...)   ADK_IO_2(__VA_ARGS__)
#define ADK_IO_5(...)   ADK_IO_2(__VA_ARGS__)
#define ADK_IO_6(...)   ADK_IO_2(__VA_ARGS__)
#define ADK_IO_7(...)   ADK_IO_2(__VA_ARGS__)
#define ADK_IO_8(...)   ADK_IO_2(__VA_ARGS__)

#define ADK_IO_(N, ...) ADK_CONCATENATE(ADK_IO_, N)(__VA_ARGS__)
#define ADK_IO(...) ADK_IO_(ADK_GET_NARG(__VA_ARGS__), __VA_ARGS__)

#define default_output_type(z, n, unused)   \
    typedef class {} DefaultOutputType##n;

BOOST_PP_REPEAT(ADK_MAX_OUTPUT_MESSAGE_TYPE, default_output_type, ~)

#undef default_output_type


#define enum_output_type(z, n, unused)   \
    , int fanout##n = 1, typename OutputMessageType##n = DefaultOutputType##n

#define enum_base_class(z, n, unused)   \
    , public NextStageWorker<fanout##n, OutputMessageType##n>

template<int assemble, int mode, typename InputMessageType
        BOOST_PP_REPEAT(ADK_MAX_OUTPUT_MESSAGE_TYPE, enum_output_type, ~)
        >
class StageWorker : public PrevStageWorker<assemble, mode, InputMessageType>
                    BOOST_PP_REPEAT(ADK_MAX_OUTPUT_MESSAGE_TYPE, enum_base_class, ~)
{
public:
    typedef InputMessageType prev_type;

    #define enum_assert_fanout(z, n, unused) \
    assert(fanout##n <= ADK_MAX_PIPELINE_PARALLEL);

    StageWorker()
    {
        assert(assemble <= ADK_MAX_PIPELINE_PARALLEL);
        BOOST_PP_REPEAT(ADK_MAX_OUTPUT_MESSAGE_TYPE, enum_assert_fanout, ~);

        string auto_gen_name = g_stage_name_maker.GenName();
        set_name(auto_gen_name);
        policy::Delay::Init(backoff_);
    }

    StageWorker(const string& stage_name)
    {
        assert(assemble <= ADK_MAX_PIPELINE_PARALLEL);
        BOOST_PP_REPEAT(ADK_MAX_OUTPUT_MESSAGE_TYPE, enum_assert_fanout, ~);
        set_name(stage_name);
        policy::Delay::Init(backoff_);
    }

    #undef enum_assert_fanout

    virtual ~StageWorker() {}

    #define enum_set_name(z, n, unused) \
    NextStageWorker<fanout##n, OutputMessageType##n>::set_next_stage_name(name);

    void set_name(const string& name)
    {
        PrevStageWorker<assemble, mode, InputMessageType>::set_prev_stage_name(name);

        BOOST_PP_REPEAT(ADK_MAX_OUTPUT_MESSAGE_TYPE, enum_set_name, ~)
    }
    #undef enum_set_name

    string name()
    {
        return PrevStageWorker<assemble, mode, InputMessageType>::prev_stage_name_;
    }

    #define enum_next_stage(z, n, unused) \
        if (std::is_same<OutputMessageType, OutputMessageType##n>::value)   \
        {   \
            return static_cast<NextStageWorker<fanout##n, OutputMessageType##n>* >(this);   \
        }

    template<typename OutputMessageType>
    NextStageWorker<1, OutputMessageType>* Next()
    {
        // BOOST_PP_REPEAT(ADK_MAX_OUTPUT_MESSAGE_TYPE, enum_next_stage, ~)
        return static_cast<NextStageWorker<1, OutputMessageType>* >(this);
    }

    template<int fanout, typename OutputMessageType>
    NextStageWorker<fanout, OutputMessageType>* GetNext()
    {
        // BOOST_PP_REPEAT(ADK_MAX_OUTPUT_MESSAGE_TYPE, enum_next_stage, ~)
        return static_cast<NextStageWorker<fanout, OutputMessageType>* >(this);
    }

    #define enum_next(z, n, unused) \
        NextStageWorker<fanout##n, OutputMessageType##n>* Next##n()    \
        {   \
            return static_cast<NextStageWorker<fanout##n, OutputMessageType##n>* >(this);   \
        }

    BOOST_PP_REPEAT(ADK_MAX_OUTPUT_MESSAGE_TYPE, enum_next, ~);

    #undef enum_next

    #undef enum_next_stage

    PrevStageWorkerBasic<assemble, InputMessageType>* Prev()
    {
        return static_cast<PrevStageWorkerBasic<assemble, InputMessageType>* >(this);
    }

    // helper function define

    #define enum_forward_func(z, n, unused) \
    inline int32_t Forward(OutputMessageType##n& message)   \
    {   \
        return NextStageWorker<fanout##n, OutputMessageType##n>::DoForward(message); \
    }

    BOOST_PP_REPEAT(ADK_MAX_OUTPUT_MESSAGE_TYPE, enum_forward_func, ~)

    #undef enum_forward_func

    #define enum_forward_func(z, n, unused) \
    inline int32_t Forward(OutputMessageType##n& message, int partition)   \
    {   \
        return NextStageWorker<fanout##n, OutputMessageType##n>::DoForward(message, partition); \
    }

    BOOST_PP_REPEAT(ADK_MAX_OUTPUT_MESSAGE_TYPE, enum_forward_func, ~)

    #undef enum_forward_func

    #define enum_forward_func(z, n, unused) \
    inline int32_t Forward(OutputMessageType##n& message, short dim, short idx)   \
    {   \
        return NextStageWorker<fanout##n, OutputMessageType##n>::DoForward(message, dim, idx); \
    }

    BOOST_PP_REPEAT(ADK_MAX_OUTPUT_MESSAGE_TYPE, enum_forward_func, ~)

    #undef enum_forward_func

    #define enum_forward_func(z, n, unused) \
    inline int32_t ReorderForward(OutputMessageType##n& message)   \
    {   \
        return NextStageWorker<fanout##n, OutputMessageType##n>::DoReorderForward(message,  \
            PrevStageWorker<assemble, mode, InputMessageType>::orig_seq()); \
    }

    BOOST_PP_REPEAT(ADK_MAX_OUTPUT_MESSAGE_TYPE, enum_forward_func, ~)

    #undef enum_forward_func

    #define enum_fanout_func(z, n, unused) \
    inline int32_t Fanout(OutputMessageType##n& message, short dim)   \
    {   \
        return NextStageWorker<fanout##n, OutputMessageType##n>::DoFanout(message, dim); \
    }

    BOOST_PP_REPEAT(ADK_MAX_OUTPUT_MESSAGE_TYPE, enum_fanout_func, ~)

    #undef enum_fanout_func

    template<typename PolicyType>
    void SetBackoffPolicy()
    {
        if (adk_impl::IsEnvSetLowUtilization())
        {
            adk_impl::policy::Event::Init(backoff_);
        }
        else
        {
            PolicyType::Init(backoff_);
        }

        if (backoff_.IsEvent())
        {
            assert(mode == ADK_PL_MODE_LOADBALANCE);
        }
    }

    template<typename PolicyType>
    void SetBackoffPolicy(uint64_t max_delay_micro)
    {
        if (adk_impl::IsEnvSetLowUtilization())
        {
            adk_impl::policy::Event::Init(backoff_);
        }
        else
        {
            PolicyType::Init(backoff_);
        }

        max_delay_micro_ = max_delay_micro;
        if (backoff_.IsEvent())
        {
            assert(mode == ADK_PL_MODE_LOADBALANCE);
        }
    }

    int32_t ConfigBackoffPolicy(uint32_t type, void* val, uint32_t len)
    {
        return backoff_.Config(type, val, len);
    }

    int32_t WaitForMessage(InputMessageType& msg)
    {
        return PrevStageWorkerBasic<assemble, InputMessageType>::prev_event_manager_->Wait( 
            [&](){return PrevStageWorkerBasic<assemble, InputMessageType>::assemble_connector_->Receive(msg);}, 
            max_delay_micro_);
    }

    virtual void Idle()
    {
        if (backoff_.IsEvent())
        {
            InputMessageType message;
            if (WaitForMessage(message) == ErrorCode::kSuccess)
            {
                this->inc_receive_messages();
                this->SetTotalOrderSqn(g_pipeline_total_order_seq_num);
                this->OnMessage(message, this->dim(), this->idx());
            }
        }
        else
        {
            if (this->IsReceiveMessage())
            {
                backoff_.Reset();
            }

            backoff_.Run();
        }
    }

    virtual void OnError(const std::string& desc)
    {
        std::cerr << desc << std::endl;
    }

    virtual void OnMessageDrop(uint64_t num_drop_msgs)
    {
        // FIXME: add stage name
        std::cerr << "number messages dropped by this stage : " << num_drop_msgs << std::endl;
    }

    void Run()
    {
        try {
            InputMessageType message;
            while (ACCESS_ONCE(this->is_running_))
            {
                if (this->Receive(message) == ErrorCode::kSuccess)          // FIXME : using AllocEntry to reduce memory copy!
                {
                    this->inc_receive_messages();
                    this->SetTotalOrderSqn(g_pipeline_total_order_seq_num);
                    this->OnMessage(message, this->dim(), this->idx());           // FIXME : add statistics
                }
                else
                {
                    Idle();
                }
            }
        }
        catch (...)
        {
            OnError(boost::current_exception_diagnostic_information());

            uint64_t num_drop_msgs = 0;
            InputMessageType message;
            while (ACCESS_ONCE(this->is_running_))
            {
                if (this->Receive(message) == ErrorCode::kSuccess)
                {
                    if ((num_drop_msgs & (512UL - 1UL)) == 0)
                    {
                        OnMessageDrop((++num_drop_msgs));
                    }
                }
            }
        }
    }

    Backoff* back_off()
    {
        return &backoff_;
    }

private:
    Backoff backoff_;
    uint64_t max_delay_micro_{ 1000000 }; // ns
};

template<typename MessageType, int fanout>
class PipelineEntrance
{
public:
    PipelineEntrance() {}
    ~PipelineEntrance() {}

    inline int32_t Forward(MessageType& message)
    {
        assert(fanout == 1);
        
        if (!((uint64_t)next_stage_[0] & 0x1ul))        
        {
            int32_t ret = entrance_connector_->Forward(message);
            next_event_manager_->Notify([&]() { return ErrorCode::kSuccess; });
            return ret;    
        }
        else
        {
            if (*shm_chk_point_ptr_)
            {
                if (ADK_UNLIKELY(thread_id_ == 0))
                {
                    thread_id_ = syscall(SYS_gettid);
                    (*shm_chk_point_ptr_)->thread_id = thread_id_;
                }
                (*shm_chk_point_ptr_)->total_order_sqn = g_pipeline_total_order_seq_num;
            }
            (((GenericNextStageWorker*)((uint64_t)next_stage_[0] & (~0x1ul)))->*next_entry_point_[0])((void*)&message, 1, 1);
            return ErrorCode::kSuccess;
        }
    }

    inline int32_t Forward(MessageType& message, int32_t partition)
    {
        return entrance_connector_->Forward(message, partition);
    }

    inline int32_t LoadBalanceForward(MessageType& message)
    {
        return entrance_connector_->LoadBalanceForward(message);
    }

    inline int32_t SequencialForward(MessageType& message)
    {
        return entrance_connector_->SequencialForward(message);
    }

    inline int32_t Fanout(MessageType& message, short dim)
    {
        assert(fanout > 1);
        return entrance_connector_->Fanout(message, dim);
    }

    void set_connector(Connector<MessageType, fanout>* connector)
    {
        entrance_connector_ = connector;
    }

    void set_entry_point(GenericNextStageWorker::NextEntryPointType entry_point,
                         void* next_stage,
                         int index,
                         uint64_t enable_inplace = 0,
                         ShmCheckPoint** shm_chk_point_ptr = nullptr)
    {
        next_entry_point_[index] = entry_point;
        next_stage_[index] = (void*)((uint64_t)next_stage | enable_inplace);
        shm_chk_point_ptr_ = shm_chk_point_ptr;
    }

    void set_next_event_manager(SimpleEventManager* next_event_manager)
    {
        assert(next_event_manager);
        next_event_manager_ = next_event_manager;
    }

    void set_back_off(Backoff* backoff)
    {
        assert(backoff);
        backoff_ = backoff;
    }

    void set_worker_mode(int worker_mode)
    {
        worker_mode_ = worker_mode;
    }

    void ChangeThisThreadToRealtime(int32_t priority, int32_t policy = SCHED_FIFO)
    {
        DoChangeToRealtime(priority, policy);
    }

    void ChangeThisThreadCpuAffinity(const std::string& core_list)
    {
        DoChangeCpuAffinity(core_list);
    }

private:
    GenericNextStageWorker::NextEntryPointType  next_entry_point_[fanout];
    void*                                next_stage_[fanout];
    Connector<MessageType, fanout>* entrance_connector_;
    SimpleEventManager*             next_event_manager_;
    Backoff*                        backoff_;
    ShmCheckPoint** shm_chk_point_ptr_ = nullptr;
    int                             worker_mode_;
    pid_t                           thread_id_ = 0;
};

template<int size, typename StageType>
class StageList
{
public:
    StageList() {};

    StageType* storage_[size];
};


namespace pipeline
{
#define enum_assign_func(z, n, unused)  tmp->storage_[n] = a##n;

#define _DEFINE_LIST_OF(N)   \
template<typename StageType>    \
inline StageList<N,StageType>* list_of(BOOST_PP_ENUM_PARAMS(N, StageType* a))   \
{   \
    StageList<N,StageType>* tmp = new StageList<N,StageType>();     \
    BOOST_PP_REPEAT(N, enum_assign_func, ~);    \
    return tmp; \
}

#define enum_assign_func2(z, n, unused)  tmp->storage_[n] = &a##n;
#define _DEFINE_LIST_OF2(N)   \
template<typename StageType>    \
inline StageList<N,StageType>* list_of(BOOST_PP_ENUM_PARAMS(N, StageType& a))   \
{   \
    StageList<N,StageType>* tmp = new StageList<N,StageType>();     \
    BOOST_PP_REPEAT(N, enum_assign_func2, ~);    \
    return tmp; \
}

#define DEFINE_LIST_OF(z, n, unused) _DEFINE_LIST_OF(BOOST_PP_ADD(n, 1));
BOOST_PP_REPEAT(8, DEFINE_LIST_OF, ~);

#define DEFINE_LIST_OF2(z, n, unused) _DEFINE_LIST_OF2(BOOST_PP_ADD(n, 1));
BOOST_PP_REPEAT(8, DEFINE_LIST_OF2, ~);

#undef _DEFINE_LIST_OF
#undef DEFINE_LIST_OF
#undef enum_assign_func
#undef _DEFINE_LIST_OF2
#undef DEFINE_LIST_OF2
#undef enum_assign_func2
} // pipeline


class Pipeline
{
public:
    Pipeline() {};

    Pipeline(const std::string& department,
             const std::string& pipeline_name,
             uint32_t bussiness_size);

    ~Pipeline() {};

    static constexpr bool kInplace = true;
    static constexpr bool kMessaging = false;

    void SetEventManagerPollingTime(uint64_t polling_nano)
    {
        polling_nano_ = polling_nano;
    }

    // Init();
    template<bool inplace, typename MessageType, int assemble>
    PipelineEntrance<MessageType, 1>* CreateEntrance(PrevStageWorkerBasic<assemble, MessageType>* prev)
    {
        return CreateEntrance<inplace, MessageType, assemble>(prev, 0, 1024);
    }

    template<bool inplace, typename MessageType, int assemble>
    PipelineEntrance<MessageType, 1>* CreateEntrance(PrevStageWorkerBasic<assemble, MessageType>* prev,
                                                     int index)
    {
        return CreateEntrance<inplace, MessageType, assemble>(prev, index, 1024);
    }

    template<bool inplace, typename MessageType, int assemble>
    PipelineEntrance<MessageType, 1>* CreateEntrance(PrevStageWorkerBasic<assemble, MessageType>* prev,
                                                     int index,
                                                     uint32_t depth)
    {
        PipelineEntrance<MessageType, 1>* entrance = new PipelineEntrance<MessageType, 1>();
        Connector<MessageType, assemble>* connector;
        SimpleEventManager* event_manager = new SimpleEventManager(polling_nano_, 64);

        if (prev->assemble_connector_ == NULL)
        {
            connector = new Connector<MessageType, assemble>();
            connector->Init("pipeline_belt", depth);
        }
        else
        {
            connector = prev->assemble_connector_;
        }

        entrance->set_entry_point(
            (GenericNextStageWorker::NextEntryPointType)&PrevStageWorkerBasic<assemble, MessageType>::OnMessage,
            prev, 0, (inplace ? 1 : 0), &(prev->shm_chk_point_));
        prev->set_is_inplace(inplace);

        prev->set_connector(connector);
        entrance->set_connector(connector->GetConnectorByIndex(index));

        prev->set_prev_event_manager(event_manager);
        entrance->set_next_event_manager(event_manager);

        entrance->set_worker_mode(prev->worker_mode());
        entrance->set_back_off(prev->back_off());

        AddStage(prev->prev_stage_name(), prev);
        return entrance;
    }

    template< bool inplace, typename MessageType, int assemble>
    PipelineEntrance<MessageType, 1>* CreateEntrance(PrevStageWorkerBasic<assemble, MessageType>& prev)
    {
        return CreateEntrance<inplace, MessageType, assemble>(&prev, 0, 1024);
    }

    template<bool inplace, typename MessageType, int assemble>
    PipelineEntrance<MessageType, 1>* CreateEntrance(PrevStageWorkerBasic<assemble, MessageType>& prev,
                                                     int index)
    {
        return CreateEntrance<inplace, MessageType, assemble>(&prev, index, 1024);
    }

    template<bool inplace, typename MessageType, int assemble>
    PipelineEntrance<MessageType, 1>* CreateEntrance(PrevStageWorkerBasic<assemble, MessageType>& prev,
                                                     int index,
                                                     uint32_t depth)
    {
        return CreateEntrance<inplace, MessageType, assemble>(&prev, index, depth);
    }

    template<bool inplace, int assemble, typename MessageType>
    PipelineEntrance<MessageType, assemble>* CreateEntrance(StageList<assemble, PrevStageWorkerBasic<1, MessageType>>* prevs_list,
                                                            uint32_t depth = 1024)
    {
        boost::assign_detail::generic_list<PrevStageWorkerBasic<1, MessageType>* > glist;
        for (int32_t i = 0; i < assemble; ++i)
            glist(prevs_list->storage_[i]);

        return CreateEntrance<inplace, assemble>(glist, depth);
    }

    #define CREATE_ENTRY_ARG_TYPE _CREATE_ENTRY_ARG_TYPE
    #define _CREATE_ENTRY_ARG_TYPE __CREATE_ENTRY_ARG_TYPE
    #define __CREATE_ENTRY_ARG_TYPE  PrevStageWorkerBasic<1, MessageType>&

    #define enum_assign_func(z, n, unused)  tmp->storage_[n] = &a##n;
    #define enum_param_func(z, n, unused)  PrevStageWorkerBasic<1, MessageType>& a##n,

    #define _DEFINE_CREATE_ENTRANCE(N)   \
    template<bool inplace, typename MessageType>    \
    PipelineEntrance<MessageType,N>* CreateEntrance(BOOST_PP_REPEAT(N, enum_param_func, ~) \
            uint32_t depth = 1024)   \
    {   \
        StageList<N, PrevStageWorkerBasic<1, MessageType>>* tmp =    \
                            new StageList<N, PrevStageWorkerBasic<1, MessageType>>();    \
        BOOST_PP_REPEAT(N, enum_assign_func, ~);    \
        return CreateEntrance<inplace, N, MessageType>(tmp, depth); \
    }

    #define DEFINE_CREATE_ENTRANCE(z, n, unused) _DEFINE_CREATE_ENTRANCE(BOOST_PP_ADD(n, 2));

    BOOST_PP_REPEAT(7, DEFINE_CREATE_ENTRANCE, ~);

    #undef DEFINE_CREATE_ENTRANCE
    #undef _DEFINE_CREATE_ENTRANCE
    #undef enum_assign_func

    template<bool inplace, int assemble, typename MessageType>
    PipelineEntrance<MessageType, assemble>* CreateEntrance(const boost::assign_detail::generic_list<PrevStageWorkerBasic<1, MessageType>* >& prevs_list,
                                                            uint32_t depth = 1024)
    {
        vector<PrevStageWorkerBasic<1, MessageType>* > prevs = prevs_list;

        PipelineEntrance<MessageType, assemble>* entrance = new PipelineEntrance<MessageType, assemble>();

        Connector<MessageType, assemble>* connector = new Connector<MessageType, assemble>();
        connector->Init("pipeline_belt", depth);

        entrance->set_connector(connector);

        SimpleEventManager* event_manager = new SimpleEventManager(polling_nano_, 64); // FIXME config the parameters
        entrance->set_next_event_manager(event_manager);

        entrance->set_worker_mode((*prevs.begin())->worker_mode());
        entrance->set_back_off((*prevs.begin())->back_off());

        int i = 0;
        for (auto it = prevs.begin(); it != prevs.end(); ++it, ++i)
        {
            entrance->set_entry_point(
                (GenericNextStageWorker::NextEntryPointType)&PrevStageWorkerBasic<1, MessageType>::OnMessage,
                *it, i, (inplace ? 1 : 0));
            Connector<MessageType, 1>* prev_connector = connector->GetConnectorByIndex(i);
            (*it)->set_connector(prev_connector);
            (*it)->set_is_inplace(inplace);

            AddStage((*it)->prev_stage_name(), (*it));

            (*it)->set_prev_event_manager(event_manager);
        }
        return entrance;
    }

    template<bool inplace, typename MessageType>
    Pipeline& Connect(NextStageWorker<1, MessageType>& next, PrevStageWorkerBasic<1, MessageType>& prev,
                      uint32_t depth = 1024)
    {
        // FIXME: validation check!
        Connector<MessageType, 1>* connector = new Connector<MessageType, 1>();
        connector->Init("pipeline_belt", depth);      // FIXME: make connector name, config connector depth

        SimpleEventManager* event_manager = new SimpleEventManager(polling_nano_, 64);

        next.set_entry_point(
            (GenericNextStageWorker::NextEntryPointType)&PrevStageWorkerBasic<1, MessageType>::OnMessage,
            &prev, (inplace? 1 : 0), &(prev.shm_chk_point_));
        next.set_connector(connector);
        prev.set_connector(connector);
        prev.set_is_inplace(inplace);

        next.set_next_event_manager(event_manager);
        prev.set_prev_event_manager(event_manager);

        AddStage(next.next_stage_name(), prev.prev_stage_name(), &prev);
        return *this;
    }

    template<typename MessageType, int fanout>
    Pipeline& ConnectOneToMany(NextStageWorker<fanout, MessageType>& next,
                               StageList<fanout, PrevStageWorkerBasic<1, MessageType>>* prevs_list,
                               uint32_t depth = 1024)
    {
        boost::assign_detail::generic_list<PrevStageWorkerBasic<1, MessageType>* > glist;
        for (int32_t i = 0; i < fanout; ++i)
            glist(prevs_list->storage_[i]);

        return ConnectOneToMany(next, glist, depth);
    }

    template<typename MessageType, int fanout>
    Pipeline& ConnectOneToMany(NextStageWorker<fanout, MessageType>* next,
                               StageList<fanout, PrevStageWorkerBasic<1, MessageType>>* prevs_list,
                               uint32_t depth = 1024)
    {
        return ConnectOneToMany(*next, prevs_list, depth);
    }

    template<typename MessageType, int fanout, typename PrevStageWorker>
    Pipeline& ConnectOneToMany(PrevStageWorker& prev_stage_worker,
                               StageList<fanout, PrevStageWorkerBasic<1, MessageType>>* prevs_list,
                               uint32_t depth = 1024)
    {
        return ConnectOneToMany(prev_stage_worker.template GetNext<fanout, MessageType>(),
                                prevs_list, depth);
    }

    template<int fanout, typename PrevStageWorker, typename SeccStageWorker>
    Pipeline& ConnectOneToMany(PrevStageWorker& prev_stage_worker,
                               StageList<fanout, SeccStageWorker>* succ_stage_worker_list,
                               uint32_t depth = 1024)
    {
        StageList<fanout, PrevStageWorkerBasic<1, typename SeccStageWorker::prev_type>> prevs_list;
        for (int32_t i = 0; i < fanout; ++i)
        {
            prevs_list.storage_[i] = succ_stage_worker_list->storage_[i]->Prev();
        }

        return ConnectOneToMany(prev_stage_worker.template GetNext<fanout, typename SeccStageWorker::prev_type>(),
                                &prevs_list, depth);
    }

    template<typename MessageType, int fanout>
    Pipeline& ConnectOneToMany(NextStageWorker<fanout, MessageType>& next,
                               const boost::assign_detail::generic_list<PrevStageWorkerBasic<1, MessageType>* >& prevs_list,
                               uint32_t depth = 1024)
    {
        vector<PrevStageWorkerBasic<1, MessageType>* > prevs = prevs_list;

        assert(prevs.size() == fanout);

        Connector<MessageType, fanout>* connector = new Connector<MessageType, fanout>();
        connector->Init("pipeline_belt", depth);      // FIXME: make connector name, config connector depth
        next.set_connector(connector);

        SimpleEventManager* event_manager = new SimpleEventManager(polling_nano_, 64); // FIXME config the parameters
        next.set_next_event_manager(event_manager);

        int i = 0;
        for (auto it = prevs.begin(); it != prevs.end(); ++it, ++i)
        {
            Connector<MessageType, 1>* prev_connector = connector->GetConnectorByIndex(i);
            (*it)->set_connector(prev_connector);
            AddStage(next.next_stage_name(), (*it)->prev_stage_name(), (*it));

            (*it)->set_prev_event_manager(event_manager);
        }
        return *this;
    }

    template<typename MessageType, int fanout>
    Pipeline& ConnectOneToMany(NextStageWorker<fanout, MessageType>* next,
                               const boost::assign_detail::generic_list<PrevStageWorkerBasic<1, MessageType>* >& prevs_list,
                               uint32_t depth = 1024)
    {
        return ConnectOneToMany<MessageType, fanout>(*next, prevs_list, depth);
    }

    template<int fanout, typename PrevStageWorker, typename SeccStageWorker>
    Pipeline& ConnectManyToOne(StageList<fanout, PrevStageWorker>* prev_stage_worker_list,
                               SeccStageWorker& succ_stage_worker,
                               uint32_t depth = 1024)
    {
        boost::assign_detail::generic_list<NextStageWorker<1, typename SeccStageWorker::prev_type>* > glist;
        for (int32_t i = 0; i < fanout; ++i)
        {
            glist(prev_stage_worker_list->storage_[i]->template GetNext<1, typename SeccStageWorker::prev_type>());
        }

        return ConnectManyToOne(glist, succ_stage_worker.Prev(), depth);
    }

    // Note : the stages order in nexts_list is important, it should be the same as in prevs_list
    template<typename MessageType, int assemble>
    Pipeline& ConnectManyToOne(const boost::assign_detail::generic_list<NextStageWorker<1, MessageType>* >& nexts_list,
                               PrevStageWorkerBasic<assemble, MessageType>& prev,
                               uint32_t depth = 1024)
    {
        SimpleEventManager *event_manager = new SimpleEventManager(polling_nano_, 64);
        vector<NextStageWorker<1, MessageType>* > nexts = nexts_list;
        if (prev.worker_mode() == ADK_PL_MODE_REORDER)
        {
            assert(1 == assemble);
            Connector<MessageType, 1>* connector = new Connector<MessageType, 1>();
            connector->Init("pipeline_belt", depth);
            prev.set_connector(connector);

            prev.set_prev_event_manager(event_manager);

            Connector<MessageType, 1>* prev_connector = connector->GetConnectorByIndex(0);
            for (auto it = nexts.begin(); it != nexts.end(); ++it)
            {
                (*it)->set_connector(prev_connector);
                (*it)->set_next_event_manager(event_manager);
                AddStage((*it)->next_stage_name(), prev.prev_stage_name(), &prev);
            }
            return *this;
        }

        assert(nexts.size() == assemble);
        Connector<MessageType, assemble>* connector = new Connector<MessageType, assemble>();
        connector->Init("pipeline_belt", depth);      // FIXME: make connector name, config connector depth
        prev.set_connector(connector);

        prev.set_prev_event_manager(event_manager);

        int i = 0;
        for (auto it = nexts.begin(); it != nexts.end(); ++it, ++i)
        {
            Connector<MessageType, 1>* prev_connector = connector->GetConnectorByIndex(i);
            (*it)->set_connector(prev_connector);
            AddStage((*it)->next_stage_name(), prev.prev_stage_name(), &prev);

            (*it)->set_next_event_manager(event_manager);
        }

        return *this;
    }

    template<typename MessageType, int assemble>
    Pipeline& ConnectManyToOne(const boost::assign_detail::generic_list<NextStageWorker<1, MessageType>* >& nexts_list,
                               PrevStageWorkerBasic<assemble, MessageType>* prev,
                               uint32_t depth = 1024)
    {
        return ConnectManyToOne<MessageType, assemble>(nexts_list, *prev, depth);
    }

    void AddStage(const string& prev_name, const string& next_name, IPrevStageWorker* stage_worker)
    {
        auto it = stage_workers_.find(next_name);
        if (it == stage_workers_.end())
        {
            std::pair<map<string, IPrevStageWorker*>::iterator, bool> result = stage_workers_.insert(
                std::pair<string, IPrevStageWorker*>(next_name, stage_worker));
            it = result.first;
            HandlePendingList(next_name);
        }

        auto it_prev = stage_workers_.find(prev_name);
        if (it_prev == stage_workers_.end())
        {
            it->second->prev_stage_.insert(std::pair<string, IPrevStageWorker*>(prev_name, NULL));
            pending_list_.insert(prev_name + "," + next_name);
            return;
        }

        LinkStages(it_prev, it);
    }

    void AddStage(const string& stage_name, IPrevStageWorker* stage_worker)
    {
        auto it = stage_workers_.find(stage_name);
        if (it == stage_workers_.end())
        {
            stage_workers_.insert(std::pair<string, IPrevStageWorker*>(stage_name, stage_worker));
            HandlePendingList(stage_name);
        }
    }

    void HandlePendingList(const string& stage_name)
    {
        set<string> delete_list;
        for (auto it = pending_list_.begin(); it != pending_list_.end(); ++it)
        {
            vector<string> splits;
            boost::split(splits, *it, boost::is_any_of(","));

            if (splits[0] == stage_name || splits[1] == stage_name)
            {
                auto it_prev = stage_workers_.find(splits[0]);
                if (it_prev != stage_workers_.end())
                {
                    auto it_next = stage_workers_.find(splits[1]);
                    if (it_next != stage_workers_.end())
                    {
                        LinkStages(it_prev, it_next);
                        delete_list.insert(*it);
                    }
                }
            }
        }

        for (auto it = delete_list.begin(); it != delete_list.end(); ++it)
        {
            pending_list_.erase(*it);
        }
    }

    void LinkStages(map<string, IPrevStageWorker*>::iterator& it_prev, map<string, IPrevStageWorker*>::iterator& it_next)
    {
        it_next->second->prev_stage_.insert(std::pair<string, IPrevStageWorker*>(it_prev->first, it_prev->second));
        it_prev->second->next_stage_.insert(std::pair<string, IPrevStageWorker*>(it_next->first, it_next->second));
    }

    bool Start();

    // Note : do not call this method inside the stage worker!
    bool Stop()
    {
        set<string> stages[2];
        int32_t index = 0;
        stages[index] = entrance_stages_;
        std::vector<IPrevStageWorker*> stage_worker_vec;
        while (!stages[index].empty())
        {
            for (auto it = stages[index].begin(); it != stages[index].end(); ++it)
            {
                auto stage_worker_it = stage_workers_.find(*it);
                assert(stage_worker_it != stage_workers_.end());

                stage_worker_it->second->Shutdown();
                stage_worker_it->second->Wait();
                stage_worker_vec.push_back(stage_worker_it->second);

                ////std::cout << "shutdown stage : " << stage_worker_it->first << " finished"<< std::endl;

                for (auto next_it = stage_worker_it->second->next_stage_.begin();
                     next_it != stage_worker_it->second->next_stage_.end(); ++next_it)
                {
                    stages[!index].insert(next_it->first);
                }
            }
            stages[index].clear();
            index = !index;
        }

        for (auto stage_worker : stage_worker_vec)
        {
            stage_worker->ExitThunk();
        }

        // FIXME: clean job
        return true;
    }

    void Dump() {}  // for compatible reason

    void Dump(std::ostringstream& oss)         // FIXME: using osstream
    {
        oss.clear();
        oss.str("");

        oss << "entrance stages: ";
        for (auto it = entrance_stages_.begin(); it != entrance_stages_.end(); ++it)
        {
            oss << *it << " ";
        }
        oss << std::endl;
        oss << std::endl;

        set<string> stages[2];
        int32_t index = 0;
        stages[index] = entrance_stages_;
        while (!stages[index].empty())
        {
            for (auto it = stages[index].begin(); it != stages[index].end(); ++it)
            {
                auto stage_worker_it = stage_workers_.find(*it);
                assert(stage_worker_it != stage_workers_.end());

                IPrevStageWorker* stage_worker = stage_worker_it->second;
                oss << "stage_worker_name : " << stage_worker_it->first << std::endl << "prev stages : ";
                for (auto it_prev = stage_worker->prev_stage_.begin(); it_prev != stage_worker->prev_stage_.end(); ++it_prev)
                {
                    oss << it_prev->first << " ";
                }

                oss << std::endl << "next stages : ";
                for (auto it_next = stage_worker->next_stage_.begin(); it_next != stage_worker->next_stage_.end(); ++it_next)
                {
                    oss << it_next->first << " ";
                    stages[!index].insert(it_next->first);
                }
                oss << std::endl;
                oss << std::endl;
            }
            stages[index].clear();
            index = !index;
        }
    }

    void GetStats(boost::property_tree::ptree& stats_ptree)
    {
        int32_t index = 0;
        stats_stages_[0].clear();
        stats_stages_[1].clear();
        stats_stages_[index] = entrance_stages_;

        boost::property_tree::ptree& sws_indi_tree = stats_ptree.add_child("stage_workers", boost::property_tree::ptree());

        while (!stats_stages_[index].empty())
        {
            auto it_stats_stages_end = stats_stages_[index].end();
            for (auto it = stats_stages_[index].begin(); it != it_stats_stages_end; ++it)
            {
                auto stage_worker_it = stage_workers_.find(*it);
                assert(stage_worker_it != stage_workers_.end());

                stage_worker_it->second->GetStats(pipe_stats_);
                boost::property_tree::ptree& sw_indi_tree = sws_indi_tree.add_child(stage_worker_it->first, boost::property_tree::ptree());
                for (auto& stats : pipe_stats_)
                {
                    boost::property_tree::ptree& one_indi_tree = sw_indi_tree.push_back(
                        boost::property_tree::ptree::value_type("", boost::property_tree::ptree()))->second;
                    one_indi_tree.put("fwd_fail", stats.nr_forward_fail);
                    one_indi_tree.put("max_qlen", stats.max_queue_length);
                }

                auto end_it = stage_worker_it->second->next_stage_.end();
                for (auto next_it = stage_worker_it->second->next_stage_.begin(); next_it != end_it; ++next_it)
                {
                    stats_stages_[!index].insert(next_it->first);
                }
            }
            stats_stages_[index].clear();
            index = !index;
        }
    }

    const std::string& GetLastError();

private:
    void SetLastError(const std::string& err);

    int32_t InitPipelineShm();

    int32_t BackupCheckPoint(const std::string& record_path, const std::string& pipeline_shm_name);

private:
    uint64_t                            polling_nano_{ 1000ul };
    uint32_t connecotr_depth_;
    map<string, IPrevStageWorker*>      stage_workers_;
    set<string>                         pending_list_;
    set<string>                         entrance_stages_;
    set<string>                         stages_list_;
    std::vector<QueueStats>             pipe_stats_;
    set<string>                         stats_stages_[2];

    bool                                use_pipeline_shm_ = false;
    uint32_t                            nr_checkpoints_ = 0;
    uint32_t                            business_info_size_= 0;
    ShmCheckPointHeader*                shm_checkpoint_hdr_ = nullptr;
    std::string department_;
    std::string pipeline_name_;
    std::string last_err_;
    static std::mutex* s_checkpoint_mut_;

};

} // adk

#endif // ADK_PIPELINE_H_
