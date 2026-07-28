/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */

///< boost
#include <adk/util.h>
#include <boost/locale/format.hpp>
///< adk, ami public header
#include <ami/config_key.h>

/// ami impl

///< impl
#include "async_record_client.h"
#include "config_default_value.h"
#include "recorder.h"
#include "rx_record_channel.h"
#include "tx_record_channel.h"

namespace ami
{

namespace acc  = config::context;
namespace accr = config::context::recorder;
namespace rcdv = recorder::cdv;
namespace bl   = boost::locale;
namespace ba   = boost::asio;

LOG_DEFINE(ami::AsyncRecordClient)

constexpr const char* AsyncRecordClient::kAppNameField;

AsyncRecordClient::AsyncRecordClient()
{
    on_idle_ = [&]() {
        mutex_.unlock();
        usleep(1000);
        mutex_.lock();
    };
}

AsyncRecordClient::~AsyncRecordClient()
{
}

void AsyncRecordClient::Reset()
{
    if (tcp_link_ok_)
    {
        control_client_.Stop();
    }

    recorder_id_ = RecorderId();
    mq_manager_  = nullptr;
    ctx_rcd_ne_cb_.clear();
    for (auto chn : record_chn_vec_)
    {
        delete chn;
    }
    record_chn_vec_.clear();

    is_inited_   = false;
    tcp_link_ok_ = false;
    ready_to_go_ = false;
    has_error_   = false;
    is_stopped_  = false;
}

ErrorCode_def
AsyncRecordClient::Init(const RecorderId& recorder_id, const Property& ctx_props,
                        const RecordAgent::OnRcdNotExistType& on_recorder_ne,
                        ba::io_service* ios)
{
    IF_ERR_RET(CheckError());
    std::string ctx_name =
        ctx_props.GetValue(acc::kName, std::string());
    if (true == is_inited_)
    {
        assert(!ctx_name.empty());
        TryAddRcdNECallBack(ctx_name, on_recorder_ne);
        return kSuccess;
    }

    LOG_INFO("props: {1}, initializing...", ctx_props.Dump(false));

    TryAddRcdNECallBack(ctx_name, on_recorder_ne);

    recorder_id_ = recorder_id;

    do
    {
        std::lock_guard<MutexType> lock(mutex_);

        IF_ERR_RET(control_client_.Init(recorder_id_,
                                        ControlConnection::OnRequestHandler(),
                                        ios,
                                        boost::bind(&AsyncRecordClient::OnRecorderNE, this)),
                   has_error_ = true;
                   LOG_ERROR("Init control client failed."));
    } while (false);

    is_inited_ = true;
    LOG_INFO("init ok");
    return ErrorCode::kSuccess;
}

ErrorCode_def AsyncRecordClient::Start(std::string& data_path,
                                       const std::string& ctx_name,
                                       bool is_recovery)
{
    if (is_stopped_)
    {
        LOG_ERROR("recorder client is stopped");
        return kFailure;
    }

    IF_ERR_RET(CheckError());
    if (true == ready_to_go_)
    {
        if (HandShakeWithExistRecorder(ctx_name, is_recovery)
            != kSuccess)
        {
            return kFailure;
        }

        data_path = data_path_;
        return kSuccess;
    }

    LOG_INFO("starting...");

    do
    {
        std::lock_guard<MutexType> lock(mutex_);

        IF_ERR_RET(control_client_.Start(),
                   (has_error_ = true);
                   LOG_ERROR("start control client failed."));

        tcp_link_ok_ = true;
        LOG_INFO("connect to server ok.");

        IF_ERR_RET(HandShake(ctx_name, is_recovery),
                   (has_error_ = true);
                   LOG_ERROR("hand shake with recorder failed."));
        LOG_INFO("hand shake with recorder ok.");
    } while (false);

    data_path    = data_path_;
    ready_to_go_ = true;
    LOG_INFO("start ok");
    return ami::kSuccess;
}

void AsyncRecordClient::Stop()
{
    if (is_stopped_)
        return;
    is_stopped_ = true;  // prevent start/stop

    ready_to_go_ = false;  // prevent Creating channels
    if (tcp_link_ok_)
    {
        control_client_.Stop();
        tcp_link_ok_ = false;
    }
}

ErrorCode_def AsyncRecordClient::HandShakeWithExistRecorder(const std::string& ctx_name, bool is_recovery)
{
    Property request;
    request.SetValue(
        kMessageType,
        ControlMessageType::kConnectToExistRecorder);
    request.SetValue(
        kContextName,
        ctx_name);
    request.SetValue(
        kIsAgentRecovery,
        is_recovery);
    std::string str = request.Dump(false);
    char reply_buf[ControlConnection::kMaxReplyMsgBodyLen];
    uint32_t reply_len = sizeof(reply_buf);
    IF_ERR_RET(control_client_.Request(
                   str.c_str(), str.length(), reply_buf, &reply_len),
               LOG_ERROR("Request to existing recorder failed."));

    Property prop(std::string(reply_buf, reply_len));
    if (true == HasRecorderFailed(prop))
    {
        return kFailure;
    }

    return ErrorCode::kSuccess;
}

ErrorCode_def AsyncRecordClient::HandShake(const std::string& ctx_name, bool is_recovery)
{
    Property request;
    request.SetValue(
        kMessageType,
        ControlMessageType::kConnectToRecorder);
    request.SetValue(
        kContextName,
        ctx_name);
    request.SetValue(
        kIsAgentRecovery,
        is_recovery);
    std::string str = request.Dump(false);
    char reply_buf[ControlConnection::kMaxReplyMsgBodyLen];
    uint32_t reply_len = sizeof(reply_buf);
    IF_ERR_RET(control_client_.Request(
                   str.c_str(), str.length(), reply_buf, &reply_len),
               LOG_ERROR("Request to recorder failed."));

    Property prop(std::string(reply_buf, reply_len));
    if (true == HasRecorderFailed(prop))
    {
        return kFailure;
    }

    if (!prop.HasValue(kDataPath))
    {
        return ErrorCode::kFailure;
    }
    data_path_ = prop.GetStringValue(kDataPath);

    if (!prop.HasValue(kMQManagerName))
    {
        return ErrorCode::kFailure;
    }
    std::string mq_manager_name = prop.GetStringValue(kMQManagerName);
    mq_manager_                 = adk::MQManager::Attach(mq_manager_name);
    if (mq_manager_)
    {
        LOG_INFO("attach to recorder's mq manager '{1}' ok.",
                 mq_manager_name);
    }
    else
    {
        LOG_ERROR("attach to recorder's mq manager '{1}' failed.",
                  mq_manager_name);
        return ErrorCode::kFailure;
    }

    mq_manager_name_ = mq_manager_name;

    return ErrorCode::kSuccess;
}

std::vector<RxRecordChannel*>
AsyncRecordClient::CreateMergeChannels(const std::string& context_name, const Property& chnl_prop)
{
    std::vector<RxRecordChannel*> empty_ret;

    if (kSuccess != CheckError())
    {
        return empty_ret;
    }

    if (kSuccess != CheckReady())
    {
        return empty_ret;
    }

    std::lock_guard<MutexType> lock(mutex_);
    WaitOutstandingRequest();
    has_outstanding_request_ = true;
    adk::OnExit<> do_on_exit([&]() { has_outstanding_request_ = false; });

    Property request;
    request.SetValue(
        kMessageType, ControlMessageType::kCreateMergeChannels);
    request.SetValue(kAppMsgMPName, GetAppMPManagerName(context_name));
    request.SetValue(kPath, RX_PATH(context_name));
    request.SetValue(acc::kRecorder, chnl_prop);
    std::string str = request.Dump(false);
    char reply_buf[ControlConnection::kMaxReplyMsgBodyLen];
    uint32_t reply_len;

    LOG_INFO("create merge channels request: {1}", str);
    ErrorCode_def ec = control_client_.Request(
        str.c_str(), str.length(),
        reply_buf, &reply_len,
        on_idle_);
    if (ec)
    {
        return empty_ret;
    }

    Property prop(std::string(reply_buf, reply_len));
    if (true == HasRecorderFailed(prop))
    {
        return empty_ret;
    }

    int origin_queue_index = prop.GetValue(kOriginQueueIndex, -1);
    int repair_queue_index = prop.GetValue(kRepairQueueIndex, -1);

    adk::MPSCQueue* origin_msg_queue =
        mq_manager_->AttachSharedMPSCQueue(origin_queue_index);
    if (nullptr == origin_msg_queue)
    {
        has_error_ = true;
        LOG_ERROR("attach to origin msg ptr queue(index={1}) failed.",
                  origin_queue_index);
        return empty_ret;
    }
    else
    {
        LOG_INFO("attach to origin msg ptr queue(index={1}) ok.",
                 origin_queue_index);
    }

    adk::MPSCQueue* repair_msg_queue =
        mq_manager_->AttachSharedMPSCQueue(repair_queue_index);
    if (nullptr == repair_msg_queue)
    {
        has_error_ = true;
        LOG_ERROR("attach to repair msg ptr queue(index={1}) failed.",
                  repair_queue_index);
        return empty_ret;
    }
    else
    {
        LOG_INFO("attach to repair msg ptr queue(index={1}) ok.",
                 repair_queue_index);
    }

    std::vector<RxRecordChannel*> result;
    result.push_back(new RxRecordChannel(data_path_, RX_PATH(context_name),
                                         origin_msg_queue, "realtime"));
    result.push_back(new RxRecordChannel(data_path_, RX_PATH(context_name),
                                         repair_msg_queue, "retransmit"));
    record_chn_vec_.emplace_back(result[0]);
    record_chn_vec_.emplace_back(result[1]);

    return result;
}

TxRecordChannel*
AsyncRecordClient::CreateMessageChannel(const std::string& context_name,
                                        const std::string& transport_name,
                                        const Property& chnl_prop)
{
    if (kSuccess != CheckError())
    {
        return nullptr;
    }

    if (kSuccess != CheckReady())
    {
        return nullptr;
    }

    std::lock_guard<MutexType> lock(mutex_);
    WaitOutstandingRequest();
    has_outstanding_request_ = true;
    adk::OnExit<> do_on_exit([&]() { has_outstanding_request_ = false; });

    Property request;
    request.SetValue(
        kMessageType, ControlMessageType::kCreateMessageChannel);
    request.SetValue(kAppMsgMPName, GetAppMPManagerName(context_name));
    request.SetValue(kPath, TX_PATH(context_name, transport_name));
    request.SetValue(acc::kRecorder, chnl_prop);
    std::string str = request.Dump(false);
    LOG_INFO("create message channel request: {1}", str);
    char reply_buf[ControlConnection::kMaxReplyMsgBodyLen];
    uint32_t reply_len;
    ErrorCode_def ec = control_client_.Request(str.c_str(), str.length(),
                                               reply_buf, &reply_len, on_idle_);
    if (ec)
    {
        return nullptr;
    }

    Property prop(std::string(reply_buf, reply_len));
    if (true == HasRecorderFailed(prop))
    {
        return nullptr;
    }

    int queue_index = prop.GetValue(kDataQueueIndex, -1);
    adk::MPSCQueue* msg_queue =
        mq_manager_->AttachSharedMPSCQueue(queue_index);
    if (nullptr == msg_queue)
    {
        LOG_ERROR("attach to msg ptr queue(index={1}) failed.",
                  queue_index);
        has_error_ = true;
        return nullptr;
    }
    else
    {
        LOG_INFO("attach to msg ptr queue(index={1}) ok.",
                 queue_index);
    }

    TxRecordChannel* res = new TxRecordChannel(data_path_,
                                               TX_PATH(context_name, transport_name),
                                               msg_queue);
    record_chn_vec_.emplace_back(res);
    return res;
}

StRecordChannel*
AsyncRecordClient::CreateStatusChannel(const std::string& context_name,
                                       const Property& chnl_prop)
{
    if (kSuccess != CheckError())
    {
        return nullptr;
    }

    if (kSuccess != CheckReady())
    {
        return nullptr;
    }

    std::lock_guard<MutexType> lock(mutex_);
    WaitOutstandingRequest();
    has_outstanding_request_ = true;
    adk::OnExit<> do_on_exit([&]() { has_outstanding_request_ = false; });

    Property request;
    request.SetValue(kMessageType, ControlMessageType::kCreateStatusChannel);
    request.SetValue(kAppMsgMPName, GetAppMPManagerName(context_name));
    request.SetValue(kPath, ST_PATH(context_name));
    request.SetValue(acc::kRecorder, chnl_prop);
    std::string str = request.Dump(false);
    LOG_INFO("Send request: {1}" ,str);
    char reply_buf[ControlConnection::kMaxReplyMsgBodyLen];
    uint32_t reply_len;
    ErrorCode_def ec = control_client_.Request(str.c_str(), str.length(),
                                               reply_buf, &reply_len, on_idle_);
    if (ec)
    {
        return nullptr;
    }

    Property prop(std::string(reply_buf, reply_len));
    if (true == HasRecorderFailed(prop))
    {
        return nullptr;
    }

    int queue_index = prop.GetValue(kStatusQueueIndex, -1);
    adk::MPSCQueue* msg_queue =
        mq_manager_->AttachSharedMPSCQueue(queue_index);
    if (nullptr == msg_queue)
    {
        LOG_ERROR("attach to msg ptr queue(index={1}) failed.",
                  queue_index);
        has_error_ = true;
        return nullptr;
    }
    else
    {
        LOG_INFO("attach to msg ptr queue(index={1}) ok.",
                 queue_index);
    }

    StRecordChannel* res = new StRecordChannel(data_path_,
                                               ST_PATH(context_name),
                                               msg_queue);
    record_chn_vec_.emplace_back(res);
    return res;
}

int32_t AsyncRecordClient::IncreSyncIdMaps(Property& id_objects)
{
    if (kSuccess != CheckError())
    {
        return ErrorCode::kFailure;
    }

    if (kSuccess != CheckReady())
    {
        return ErrorCode::kFailure;
    }

    std::lock_guard<MutexType> lock(mutex_);
    WaitOutstandingRequest();
    has_outstanding_request_ = true;
    adk::OnExit<> do_on_exit([&]() { has_outstanding_request_ = false; });

    Property request;
    request.SetValue(kMessageType, ControlMessageType::kDynamicSyncIdMaps);
    request.SetValue(kDynamicSyncIdMaps, id_objects);
   
    std::string str = request.Dump(false);
    LOG_INFO("Send request: {1}" ,str);
    char reply_buf[ControlConnection::kMaxReplyMsgBodyLen];
    uint32_t reply_len;
    ErrorCode_def ec = control_client_.Request(str.c_str(), str.length(),
                                               reply_buf, &reply_len, on_idle_);
    return ec;   
}

}  // namespace ami
