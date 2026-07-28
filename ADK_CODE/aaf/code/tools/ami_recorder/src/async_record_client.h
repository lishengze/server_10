/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */

#ifndef AMI_ASYNC_RECORD_CLIENT_H_
#define AMI_ASYNC_RECORD_CLIENT_H_

///< cpp std
#include <map>
#include <mutex>

///< boost
#include <boost/filesystem.hpp>

///< ami public

///< ami impl
#include "../log.h"
#include "../util.h"

///< impl
#include "control_client.h"
#include "control_message_key.h"
#include "record_channel.h"
#include "record_client.h"
#include "record_reader.h"
#include "recorder_base.h"

namespace ami
{

/**
 * @brief 异步持久化客户端类（进程单子）
 * 
 * @note 本类的对象线程安全
 */
class AsyncRecordClient : public RecordClient
{
public:
    AsyncRecordClient();
    virtual ~AsyncRecordClient();
    AsyncRecordClient(const AsyncRecordClient&) = delete;
    AsyncRecordClient& operator=(const AsyncRecordClient&) = delete;

    virtual ErrorCode_def Init(const RecorderId& recorder_id, const Property& ctx_props,
                               const RecordAgent::OnRcdNotExistType& on_recorder_ne =
                                   RecordAgent::OnRcdNotExistType(),
                               boost::asio::io_service* ios = nullptr);

    virtual ErrorCode_def Start(std::string& data_path,
                                const std::string& ctx_name,
                                bool is_recovery);

    virtual std::vector<RxRecordChannel*>
    CreateMergeChannels(const std::string& context_name,
                        const Property& chnl_prop);

    virtual TxRecordChannel* CreateMessageChannel(
        const std::string& context_name, const std::string& transport_name,
        const Property& chnl_prop);

    virtual StRecordChannel*
    CreateStatusChannel(const std::string& context_name,
                        const Property& chnl_prop);

    virtual void Stop();

    virtual void DetachSharedMemory() override
    {
        if (mq_manager_)
        {   
            mq_manager_->Detach(mq_manager_name_);
        }
    }
    
    int32_t IncreSyncIdMaps(Property& id_objects) override;

private:
    typedef std::map<std::string, RecordAgent::OnRcdNotExistType> CtxRcdNECbMapType;
    typedef std::recursive_mutex MutexType;
    typedef std::vector<RecordChannel*> RecordChnlVecType;

    static constexpr const char* kAppNameField = "app";

    std::string GetAppMPManagerName(const std::string& context_name) const
    {
        return MakeMPTableName(context_name);
    }

    ErrorCode_def HandShake(const std::string& ctx_name, bool is_recovery);
    ErrorCode_def HandShakeWithExistRecorder(const std::string& ctx_name, bool is_recovery);

    /**
     * 检查recorder侧是否对于指定请求操作失败
     */
    bool HasRecorderFailed(const ami::Property& reply)
    {
        if (ControlMessageType::kActionFail == reply.GetValue(kMessageType, 0))
        {
            LOG_DEBUG("recorder do action failed.");
            return true;
        }

        return false;
    }

    void TryAddRcdNECallBack(const std::string& context_name,
                             const CtxRcdNECbMapType::mapped_type& cb)
    {
        if (!cb)
            return;
        if (ctx_rcd_ne_cb_.count(context_name))
            return;
        ctx_rcd_ne_cb_.emplace(std::make_pair(context_name, cb));
    }

    void OnRecorderNE() const
    {
        std::lock_guard<MutexType> lock(mutex_);
        for (const auto& item : ctx_rcd_ne_cb_)
        {
            if (!!item.second)
            {
                try
                {
                    item.second();
                }
                catch (...)
                {
                }
            }
        }
    }

    ErrorCode_def CheckError() const
    {
        if (has_error_)
        {
            LOG_ERROR("error happened.");
            return kFailure;
        }

        return kSuccess;
    }

    ErrorCode_def CheckReady() const
    {
        if (!ready_to_go_)
        {
            LOG_ERROR("not ready.");
            return kFailure;
        }

        return kSuccess;
    }

    void Reset();  ///< 单元测试用，将单例的状态复原

    RecorderId recorder_id_;
    ControlClient control_client_;

    std::string mq_manager_name_;
    adk::MQManager* mq_manager_ = nullptr;
    CtxRcdNECbMapType ctx_rcd_ne_cb_;
    mutable MutexType mutex_;
    RecordChnlVecType record_chn_vec_;
    std::string data_path_;

    /***********************************************
     * 这些状态量只能从false变为true，不允许反向变化
     */
    bool is_inited_  = false;
    bool has_error_  = false;
    bool is_stopped_ = false;
    /***********************************************/
    bool tcp_link_ok_             = false;
    bool ready_to_go_             = false;
    bool has_outstanding_request_ = false;

    void WaitOutstandingRequest()
    {
        while (has_outstanding_request_)
        {
            mutex_.unlock();
            usleep(1000);
            mutex_.lock();
        }
    }

    boost::function<void()> on_idle_;

    LOG_DECLARE
};

}  // namespace ami

#endif /* AMI_RECORD_CLIENT_H_ */
