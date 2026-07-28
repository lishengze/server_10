#pragma once

#include <atomic>
#include <time.h>
#include <unistd.h>
#include <adk/entry_wrapper.h>

namespace aaf
{

void* g_tcp_direct_warm_data = nullptr;
void (*g_tcp_direct_warm_method)(void* data) = nullptr;

const int32_t kMsgHandlerIdMax = 16; 
const int32_t kInvalidMsgHandlerId = -1; 

struct SpinLock 
{
    std::atomic<bool> lock_ = {false};

    void lock() 
    { 
        while (lock_.exchange(true, std::memory_order_acquire)); 
    }

    void unlock() 
    { 
        lock_.store(false, std::memory_order_release); 
    }

    bool try_lock() 
    {
        return !lock_.exchange(true, std::memory_order_acquire); 
    }

    struct LockGuard
    {
        LockGuard(SpinLock& lock_obj)
            : lock_(lock_obj)
        {
            lock_.lock();
        }

        ~LockGuard()
        {
            lock_.unlock();
        }

        SpinLock& lock_;
    };
};

enum WorkState
{
    kUseAmi,             // 初始工作状态
    kLeaderBypassAmi,    // 主TE工作状态
    kDisasterBypassAmi,  // 热备TE工作状态
    kDisasterFailover,   // 暂未使用
    kQeBypassAmi,        // QE组件工作状态
};

struct AsyncAmiExecutor : public aaf::EndpointHandler
{
    ADK_LOG_DECLARE_AC(LogCodeBase::kAmiBypass)

    typedef int32_t HandlerIdType;

    void OnMessage(ami::Message* msg, int32_t hdl_id, bool is_tete_hook, ami::MessageHandler* handler)
    {
        while (ADK_UNLIKELY(ACCESS_ONCE(msg2_) == nullptr))
        {
            ADK_PAUSE();
        }

        if (work_state_ == WorkState::kLeaderBypassAmi)  // 主TE工作状态
        {
            if (is_recoverying_)
            {
                // 故障恢复只递交回环主题消息，回环主题上的消息带了头部，可以找到对应的msg_hdl
                // 原因：回环主题上的消息是定序了的
                if (is_tete_hook)
                {
                    // 剔除头部的 handler id，并递交到对应主题注册的的OnMessage回调
                    msg2_->app_data = (char*)msg->app_data + sizeof(HandlerIdType);
                    msg2_->app_data_len = msg->app_data_len - sizeof(HandlerIdType);
                    HandlerIdType hdl_id = *(HandlerIdType*)(msg->app_data);
                    handlers_[hdl_id]->OnMessage(msg2_);
                }
                else
                {
                    ++nr_recovery_drop_msgs_;
                }
                return;
            }
            // TeTe 这个主题不使用
            // TE主会把所有回环的消息(TeTe)，以及OnMesasge收到的消息，转发到 "TeTeHook" 主题上
            if (!is_tete_hook)
            {
                LoopBackSendMsg(msg, hdl_id); // 回环发送产生全局顺序
            }
            else
            {
                ++nr_master_drop_msgs_;
            }
        }
        else if (work_state_ == WorkState::kDisasterBypassAmi)  // 热备TE工作状态
        {
            if (is_tete_hook)  // 只处理主TE转发过来的属于回环主题上的消息
            {
                // 此主题上的消息内容包括： 
                // 1.TE主实例，应用通过RUDP/TCP收到的消息。应用收到消息后会通过回环主题自发自收，再转发给热备TE
                // 2.TE主实例，应用通过AMI总线收到的其它主题（非回环主题）上的消息。应用收到消息后会通过回环主题自发自收，再转发给热备TE
                
                // 剔除头部的 handler id，并递交到对应主题注册的的OnMessage回调
                msg2_->app_data = (char*)msg->app_data + sizeof(HandlerIdType);
                msg2_->app_data_len = msg->app_data_len - sizeof(HandlerIdType);
                HandlerIdType hdl_id = *(HandlerIdType*)(msg->app_data);
                handlers_[hdl_id]->OnMessage(msg2_);
            }
            else  // 主TE转发过来的其它主题上的消息，直接丢弃
            {
                // if       FW 主题上的非 TeTeHook 主题上的消息
                //             1. 不递交给应用
                // 
                // else     TE 热备，成为主之后，自己直接订阅主题上的消息
                //             handler->OnMessage(msg);
                ++nr_disaster_drop_msgs_;
            }
        }
        else if (work_state_ == WorkState::kQeBypassAmi)  // QE工作状态，QE以及QE热备工作流程一致
        {
            if (is_tete_hook)  // 只处理TE转发过来的属于回环主题上的消息
            {
                // 此主题上的消息内容包括： 
                // 1.TE实例，应用通过RUDP/TCP收到的消息。应用收到消息后会通过回环主题自发自收，再转发给QE
                // 2.TE实例，应用通过AMI总线收到的其它主题（非回环主题）上的消息。应用收到消息后会通过回环主题自发自收，再转发给QE

                // 剔除头部的 handler id，并递交到对应主题注册的的OnMessage回调
                msg2_->app_data = (char*)msg->app_data + sizeof(HandlerIdType);
                msg2_->app_data_len = msg->app_data_len - sizeof(HandlerIdType);
                HandlerIdType hdl_id = *(HandlerIdType*)(msg->app_data);
                handlers_[hdl_id]->OnMessage(msg2_);
            }
            else  // TE转发过来的其它主题上的消息，直接丢弃
            {
                // QE 自己主题上的消息

                // if       FW 主题上的消息
                //          丢弃
                //          
                // else     QE 直接订阅主题上的消息，没有被接管
                //          handler->OnMessage(msg);
                ++nr_slave_drop_msgs_;
            }
        }
    }

    // 使用场景：被动触发，开启了bypass后，主TE收到AMI总线的消息后，除回环主题外，其他主题的消息都会进行回环转发
    void LoopBackSendMsg(ami::Message* msg, int32_t hdl_id)
    {
        SpinLock::LockGuard guard(lock_);

        // 产生全局顺序
        os_buf_.buffer = msg->data();
        os_buf_.size  = msg->size();
        os_buf_.hdl_id = hdl_id;
        os_buf_.is_tete_task = false;
        os_buf_.is_done.store(false, std::memory_order_release);

        handlers_[hdl_id]->OnMessage(msg);

        // 内部回环，MDD,AGW,ORS,DXS发过来的消息
        // 检测是否已经完成拷贝: 入队
        while (!os_buf_.is_done.load(std::memory_order_acquire))
        {
            ADK_PAUSE();
        }
    }

    // 使用场景：TE 应用收到 RUDP/TCP 消息后主动调用，该接口仅作用在回环主题上
    int32_t SendMsg(const void* buffer, uint32_t size) override
    {
        // FIXME: 使用函数指针？
        while (ADK_UNLIKELY(ACCESS_ONCE(msg_) == nullptr
                            || ACCESS_ONCE(tete_hdl_id_) == kInvalidMsgHandlerId))
        {
            ADK_PAUSE();
        }

        SpinLock::LockGuard guard(lock_);

        // 产生全局顺序
        os_buf_.buffer = buffer;
        os_buf_.size  = size;
        os_buf_.hdl_id = tete_hdl_id_;
        os_buf_.is_tete_task = true;
        os_buf_.is_done.store(false, std::memory_order_release);

        msg_->app_data = (void*)buffer;
        msg_->app_data_len = size;
        handlers_[tete_hdl_id_]->OnMessage(msg_);

        // 内部回环，RUDP/TCP发过来的消息
        // 检测是否已经完成拷贝: 入队
        while (!os_buf_.is_done.load(std::memory_order_acquire))
        {
            ADK_PAUSE();
        }

        ++nr_app_loop_tx_msgs_;

        return ErrorCode::kSuccess;
    }

    int32_t SendMsg(const void* buffer, uint32_t size, int32_t partition_no) override
    {
        return SendMsg(buffer, size);
    }

    int32_t SendMsg(const void* data, uint32_t len, ami::TraceRecord record) override
    {
        return ErrorCode::kFailure;
    }

    int32_t SendMsg(const void* data, uint32_t len, int32_t partition_no, ami::TraceRecord record) override
    {
        return ErrorCode::kFailure;
    }

    int32_t SendMsg(ami::Message* const msg) override
    {
        return ErrorCode::kFailure;
    }

    int32_t SendMsg(ami::Message* const msg, ami::TraceRecord record) override
    {
        return ErrorCode::kFailure;
    }

    int32_t SendMsg(ami::Message* const msg, int32_t partition_no) override
    {
        return ErrorCode::kFailure;
    }

    int32_t SendMsg(ami::Message* const msg, int32_t partition_no, ami::TraceRecord record) override
    {
        return ErrorCode::kFailure;
    }

    int32_t SendMsg(const std::string& data_str) override
    {
        return ErrorCode::kFailure;
    }

    int32_t SendMsg(const std::string& data_str, ami::TraceRecord record) override
    {
        return ErrorCode::kFailure;
    }

    int32_t SendMsg(const std::string& data_str, int32_t partition_no) override
    {
        return ErrorCode::kFailure;
    }

    int32_t SendMsg(const std::string& data_str, int32_t partition_no, ami::TraceRecord record) override
    {
        return ErrorCode::kFailure;
    }

    // 1.拷贝应用消息到环形buffer，为应用消息增加头部DataHeader（包含应用消息大小和应用消息所属的HandlerIdType）
    // 2.读取环形buffer数据，调用回环主题发送消息
    void AsyncSendThreadMain()
    {
        ADK_LOG_INFO_AC_TF("async send thread", "async send ring-buffer-data thread start to run");
        #define AAF_ASYNC_BUF_SIZE (1024*1024*32)   // 32MB
        char* local_buf = new char[AAF_ASYNC_BUF_SIZE + 1024*1024]; 
        uint64_t pos_end = 0, pos_begin = 0;
        while (is_running_)
        {
            if (!os_buf_.is_done.load(std::memory_order_acquire)
                && (pos_end < (pos_begin + AAF_ASYNC_BUF_SIZE)))
            {
                // 有任务
                // [DataHeader|DATA]
                auto end_index = pos_end % AAF_ASYNC_BUF_SIZE;
                uint32_t local_buf_size = os_buf_.size;
                int32_t local_hdl_id = os_buf_.hdl_id;
                char* buf_write_begin = (&local_buf[end_index]);
                memcpy(buf_write_begin + sizeof(DataHeader), os_buf_.buffer, local_buf_size);
                ((DataHeader*)buf_write_begin)->hdl_id = local_hdl_id;
                bool is_tete_task = os_buf_.is_tete_task;
                os_buf_.is_done.store(true, std::memory_order_release);

                ((DataHeader*)buf_write_begin)->data_size = local_buf_size;
                pos_end += local_buf_size + sizeof(DataHeader);
                
                if (is_tete_task
                    && g_tcp_direct_warm_method != nullptr
                    && g_tcp_direct_warm_data != nullptr)
                {
                    g_tcp_direct_warm_method(g_tcp_direct_warm_data);
                }
            }
            else
            {
                // FIXME: 向 'TeTe' 发送buffer
                if (pos_end > pos_begin)
                {
                    auto begin_index = pos_begin % AAF_ASYNC_BUF_SIZE;
                    char* buf_read_begin = (&local_buf[begin_index]);
                    // 此处为一个 [hdl][data]

                    tete_endpoint_->SendMsg(&(((DataHeader*)buf_read_begin)->hdl_id),
                                            ((DataHeader*)buf_read_begin)->data_size + sizeof(HandlerIdType));
                    pos_begin += ((DataHeader*)buf_read_begin)->data_size + sizeof(DataHeader);
                }
            }
        }
        ADK_LOG_INFO_AC_TF("async send thread", "async send ring-buffer-data thread exit");
    }

    void Start()
    {
        if (is_running_)
        {
            return;
        }

        is_running_ = true;
        async_ami_executor_thread_ = adk::boost_thread(
            "async-ami-exe",
            "the async ami executor thread",
            boost::bind(&AsyncAmiExecutor::AsyncSendThreadMain, this));
    }

    void Stop()
    {
        is_running_ = false;
        if (async_ami_executor_thread_.joinable())
        {
            async_ami_executor_thread_.join();
        }
    }

    struct OutstandingCopyingBuffer
    {
        const void* buffer = nullptr;
        std::atomic<bool> is_done {true};
        bool          is_tete_task = false;
        HandlerIdType hdl_id = 0;
        uint32_t      size = 0;
    };

    struct DataHeader
    {
        uint32_t data_size;
        HandlerIdType hdl_id;
    };

    int32_t NewHandlerId(ami::MessageHandler* handler) 
    { 
        if (nr_handlers_ + 1 > kMsgHandlerIdMax)
        {
            return kInvalidMsgHandlerId;
        }
        auto ret = nr_handlers_;
        ++nr_handlers_;
        handlers_[ret] = handler;
        return ret;
    }

    void set_tete_hdl_id(HandlerIdType hdl_id) { tete_hdl_id_ = hdl_id; }

    SpinLock        lock_ __attribute__((aligned(64)));
    ami::Message*   msg_ = nullptr;
    HandlerIdType   tete_hdl_id_ = kInvalidMsgHandlerId;
    ami::Message*   msg2_ = nullptr;

    ami::MessageHandler* handlers_[kMsgHandlerIdMax] {nullptr};
    HandlerIdType        nr_handlers_ = 0;
    WorkState            work_state_ = WorkState::kUseAmi;
    bool                 is_recoverying_ = false;

    void OnDisasterFailover()
    {
        if (work_state_ == WorkState::kDisasterBypassAmi)
        {
            work_state_ = WorkState::kLeaderBypassAmi;
            ADK_LOG_INFO_AC_TF("work state switch",
                               "pre work state <{1}>, now work state <{2}>.",
                               WorkState::kDisasterBypassAmi,
                               work_state_);
        }
        else
        {
            ADK_LOG_INFO_AC_TF("work state don't need to switch", "now work state <{1}>.", work_state_);
        }
    }

    void OnRecoveryBegin()
    {
        is_recoverying_ = true;
        ADK_LOG_INFO_AC_TF("bypass ready to recovery", "recovery state <{1}>.", is_recoverying_);
    }

    void OnRecoverySuccess()
    {
        is_recoverying_ = false;
        ADK_LOG_INFO_AC_TF("bypass finish recovery", "recovery state <{1}>.", is_recoverying_);
    }

    bool OnCollection(boost::property_tree::ptree& indicator)
    {
        indicator.put("master_drop_msgs", nr_master_drop_msgs_);
        indicator.put("disaster_drop_msgs", nr_disaster_drop_msgs_);
        indicator.put("slave_drop_msgs", nr_slave_drop_msgs_);
        indicator.put("app_loop_tx_msgs", nr_app_loop_tx_msgs_);
        indicator.put("recovery_drop_msgs", nr_recovery_drop_msgs_);
        return true;
    }

    OutstandingCopyingBuffer os_buf_ __attribute__((aligned(64)));

    uint64_t padding_ __attribute__((aligned(64)));
    volatile bool           is_running_ = false;
    ami::TxEndpoint*        tete_endpoint_ = nullptr;
    boost::thread           async_ami_executor_thread_;
    uint64_t                nr_master_drop_msgs_ = 0;         // 记录主TE工作时丢弃的回环主题消息数
    uint64_t                nr_disaster_drop_msgs_ = 0;       // 记录热备TE工作时丢弃的普通主题消息数
    uint64_t                nr_slave_drop_msgs_ = 0;          // 记录QE组件工作时丢弃的普通主题消息数
    uint64_t                nr_app_loop_tx_msgs_ = 0;         // 记录应用主动调用回环主题转发的TCP/RUDP消息
    uint64_t                nr_recovery_drop_msgs_ = 0;       // 记录主TE故障恢复时丢弃的普通主题消息数
};

struct AsyncAmiExeMessageHandler : public ami::MessageHandler
{
    AsyncAmiExeMessageHandler()
    {}

    ~AsyncAmiExeMessageHandler() = default;

    void OnMessage(ami::Message* msg) override
    {
        async_ami_executor_->OnMessage(msg, hdl_id_, is_tete_hook_, handler_);
    }

    int32_t             hdl_id_ = kInvalidMsgHandlerId;  // 应用注册的回调函数对应的id
    bool                is_tete_hook_ = false;           // 标记当前hook的主题是否是回环主题
    ami::MessageHandler* handler_ = nullptr;             // 应用注册的回调函数
    AsyncAmiExecutor*    async_ami_executor_ = nullptr;  // 回环主题Tx方向的hook对象，用于定序并回环发送消息
    friend class GenericAmiApplicationImpl;
};

ADK_LOG_DEFINE(aaf::AsyncAmiExecutor)

}
