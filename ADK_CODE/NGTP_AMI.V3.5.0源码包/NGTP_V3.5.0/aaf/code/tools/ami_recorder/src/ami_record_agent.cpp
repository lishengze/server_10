/**
 * @author 陈志(chenzhi@af.local)
 */

///< cpp std
#include <utility>
#include <vector>

///< public
#include <ami/ami_record_agent.h>
#include <ami/ami_rx_record_channel.h>
#include <ami/ami_tx_record_channel.h>
#include <ami/endpoint.h>
#include <boost/thread.hpp>

///< impl
#include "record_agent.h"

namespace ami
{

struct AmiRecordAgentPrivate
{
    boost::mutex tx_lock;
    std::map<std::string, std::vector<TxRecordChannel*>> tx_endpoint_to_ll_rec_channl;
    boost::mutex rx_lock;
    std::map<std::string, std::vector<int32_t>> rx_endpoint_to_rxtp_ids;
    boost::recursive_mutex get_rx_channel_mutex;
    boost::recursive_mutex get_tx_channel_mutex;
    boost::mutex get_rx_hist_msg_mutex;
    boost::mutex get_tx_hist_msg_mutex;
};

AmiRecordAgent::AmiRecordAgent(RecordAgent* impl)
    : impl_(impl)
{
    private_data_ = new AmiRecordAgentPrivate();
}

AmiRecordAgent::~AmiRecordAgent()
{
    if (impl_)
    {
        delete impl_;
        impl_ = nullptr;
    }
}

AmiRxRecordChannel* AmiRecordAgent::GetRxChannel()
{
    AmiRecordAgentPrivate* ami_ra_priv = (AmiRecordAgentPrivate*)private_data_;
    boost::recursive_mutex::scoped_lock lock_guard(ami_ra_priv->get_rx_channel_mutex);

    if (rx_channel_)
    {
        return rx_channel_.get();
    }

    RxRecordChannel* low_level_rx_channel = impl_->GetRxChannel();
    if (!low_level_rx_channel)
    {
        return nullptr;
    }

    rx_channel_.reset(new AmiRxRecordChannel(AmiRxRecordChannel(low_level_rx_channel)));
    return rx_channel_.get();
}

AmiTxRecordChannel*
AmiRecordAgent::GetTxChannel(const Message::IDType& endpoint_id,
                             int32_t partition_no)
{
    AmiRecordAgentPrivate* ami_ra_priv = (AmiRecordAgentPrivate*)private_data_;
    boost::recursive_mutex::scoped_lock lock_guard(ami_ra_priv->get_tx_channel_mutex);

    TransportIDType t(endpoint_id, partition_no);
    if (tx_channel_map_.count(t))
    {
        return tx_channel_map_.at(t).get();
    }

    TxRecordChannel* low_level_tx_channel =
        impl_->GetTxChannel(endpoint_id, partition_no);
    if (!low_level_tx_channel)
    {
        return nullptr;
    }

    std::shared_ptr<AmiTxRecordChannel> res(
        new AmiTxRecordChannel(AmiTxRecordChannel(low_level_tx_channel)));
    tx_channel_map_.emplace(t, res);
    return res.get();
}

AmiTxRecordChannel*
AmiRecordAgent::GetTxChannel(const std::string endpoint_name,
                             int32_t partition_no)
{
    AmiRecordAgentPrivate* ami_ra_priv = (AmiRecordAgentPrivate*)private_data_;
    boost::recursive_mutex::scoped_lock lock_guard(ami_ra_priv->get_tx_channel_mutex);

    TransportIDType2 t(endpoint_name, partition_no);
    if (tx_channel_map2_.count(t))
    {
        return tx_channel_map2_.at(t).get();
    }

    TxRecordChannel* low_level_tx_channel =
        impl_->GetTxChannel(endpoint_name, partition_no);
    if (!low_level_tx_channel)
    {
        return nullptr;
    }

    std::shared_ptr<AmiTxRecordChannel> res(
        new AmiTxRecordChannel(AmiTxRecordChannel(low_level_tx_channel)));
    tx_channel_map2_.emplace(t, res);
    return res.get();
}

ErrorCode_def AmiRecordAgent::GetRxHistMessage(const OnMessageType& on_rx_hist_msg,
                                               const Message::SqnType& begin,
                                               const Message::SqnType& end)
{
    AmiRecordAgentPrivate* ami_ra_priv = (AmiRecordAgentPrivate*)private_data_;
    boost::mutex::scoped_lock lock_guard(ami_ra_priv->get_rx_hist_msg_mutex);
    auto counter = begin;
    return impl_->GetRxHistMessage(
        [&on_rx_hist_msg, &counter](AmiMessage* msg) -> ErrorCode {
            msg->ami_meta_data.ami_flags |= AMI_INGRESS_MESSAGE;
            msg->ami_meta_data.ami_recv_sqn = counter++;
            Message* app_msg                = msg->message();
            return on_rx_hist_msg(app_msg);
        },
        begin, end);
}

ErrorCode_def AmiRecordAgent::GetRxTNPHistMessage(const OnMessageType& on_rx_hist_msg,
                                                  const Message::IDType& transport_id,
                                                  const Message::SqnType& begin,
                                                  const Message::SqnType& end)
{
    AmiRecordAgentPrivate* ami_ra_priv = (AmiRecordAgentPrivate*)private_data_;
    boost::mutex::scoped_lock lock_guard(ami_ra_priv->get_rx_hist_msg_mutex);

    return impl_->GetRxTNPHistMessage(
        [&on_rx_hist_msg](AmiMessage* msg) -> ErrorCode {
            msg->ami_meta_data.ami_flags |= AMI_INGRESS_MESSAGE;
            Message* app_msg = msg->message();
            return on_rx_hist_msg(app_msg);
        },
        transport_id, begin, end);
}

ErrorCode_def AmiRecordAgent::GetRxEDPHistMessage(const OnMessageType& on_rx_hist_msg,
                                                  const Message::IDType& endpoint_id,
                                                  const Message::SqnType& begin,
                                                  const Message::SqnType& end)
{
    AmiRecordAgentPrivate* ami_ra_priv = (AmiRecordAgentPrivate*)private_data_;
    boost::mutex::scoped_lock lock_guard(ami_ra_priv->get_rx_hist_msg_mutex);

    return impl_->GetRxEDPHistMessage(
        [&on_rx_hist_msg](AmiMessage* msg) -> ErrorCode {
            msg->ami_meta_data.ami_flags |= AMI_INGRESS_MESSAGE;
            Message* app_msg = msg->message();
            return on_rx_hist_msg(app_msg);
        },
        endpoint_id, begin, end);
}

ErrorCode_def AmiRecordAgent::GetTxHistMessage(const Message::IDType& endpoint_id,
                                               int32_t partition_no,
                                               const OnMessageType& on_tx_hist_msg,
                                               const Message::SqnType& begin,
                                               const Message::SqnType& end)
{
    AmiRecordAgentPrivate* ami_ra_priv = (AmiRecordAgentPrivate*)private_data_;
    boost::mutex::scoped_lock lock_guard(ami_ra_priv->get_tx_hist_msg_mutex);

    return impl_->GetTxHistMessage(endpoint_id, partition_no,
                                   [&on_tx_hist_msg](AmiMessage* msg) -> ErrorCode {
                                       Message* app_msg = msg->message();
                                       return on_tx_hist_msg(app_msg);
                                   },
                                   begin, end);
}

ErrorCode_def AmiRecordAgent::GetTxHistMessageCnt(uint64_t& nr_msgs)
{
    return impl_->GetTxHistMessageCnt(nr_msgs);
}

ErrorCode_def AmiRecordAgent::GetRxHistMessageCnt(Message::SqnType& nr_msgs)
{
    return impl_->GetRxHistMessageCnt(nr_msgs);
}

ErrorCode_def AmiRecordAgent::GetTxEndpointHistMessage(
    const OnMessageType& on_hist_msg,
    const std::string& endpoint_name,
    int32_t partition_no,
    const Message::SqnType& begin,
    const Message::SqnType& end)
{
    AmiTxRecordChannel* tx_rec_chann = GetTxChannel(endpoint_name, partition_no);
    if (tx_rec_chann == nullptr)
        return ErrorCode::kFailure;

    return tx_rec_chann->GetHistMessage(on_hist_msg, begin, end);
}

ErrorCode_def AmiRecordAgent::GenTxEndpointMD5(
    AmiRecordChannel::MD5& md5,
    const std::string& endpoint_name,
    int32_t partition_no,
    const Message::SqnType& begin,
    const Message::SqnType& end)
{
    AmiTxRecordChannel* tx_rec_chann = GetTxChannel(endpoint_name, partition_no);
    if (tx_rec_chann == nullptr)
        return ErrorCode::kFailure;

    return tx_rec_chann->GenMD5(md5, begin, end);
}

ErrorCode_def AmiRecordAgent::GetTxEndpointHistMessageCnt(
    Message::SqnType& nr_msgs,
    const std::string& endpoint_name,
    int32_t partition_no)
{
    AmiTxRecordChannel* tx_rec_chann = GetTxChannel(endpoint_name, partition_no);
    if (tx_rec_chann == nullptr)
        return kFailure;

    nr_msgs = tx_rec_chann->GetTxTNPHistMsgCnt();
    return kSuccess;
}

ErrorCode_def AmiRecordAgent::GetTxEndpointHistMessageCnt(
    Message::SqnType& nr_msgs,
    const std::string& endpoint_name)
{
    AmiRecordAgentPrivate* ami_ra_priv = (AmiRecordAgentPrivate*)private_data_;
    assert(ami_ra_priv != nullptr);

    boost::mutex::scoped_lock lock_guard(ami_ra_priv->tx_lock);

    auto it = ami_ra_priv->tx_endpoint_to_ll_rec_channl.find(endpoint_name);
    if (it == ami_ra_priv->tx_endpoint_to_ll_rec_channl.end())
    {
        std::vector<std::string> txtp_vec;
        if (impl_->GetTxTransports(endpoint_name, txtp_vec) != kSuccess)
            return kFailure;

        std::vector<TxRecordChannel*>& tx_rec_chann_vec =
            ami_ra_priv->tx_endpoint_to_ll_rec_channl[endpoint_name];
        for (auto& txtp_name : txtp_vec)
        {
            auto* low_level_tx_channel = impl_->GetTxChannel(txtp_name);
            if (low_level_tx_channel == nullptr)
                continue;

            tx_rec_chann_vec.push_back(low_level_tx_channel);
        }

        it = ami_ra_priv->tx_endpoint_to_ll_rec_channl.find(endpoint_name);
    }

    nr_msgs = 0;
    for (auto& low_level_tx_channel : it->second)
    {
        // GetTNPHistMsgCnt() is multi-thread safe
        nr_msgs += low_level_tx_channel->GetTNPHistMsgCnt();
    }

    return kSuccess;
}

ErrorCode_def AmiRecordAgent::GetRxEndpointHistMessageCnt(
    Message::SqnType& nr_msgs,
    const std::string& endpoint_name)
{
    AmiRecordAgentPrivate* ami_ra_priv = (AmiRecordAgentPrivate*)private_data_;
    assert(ami_ra_priv != nullptr);

    boost::mutex::scoped_lock lock_guard(ami_ra_priv->rx_lock);

    auto it = ami_ra_priv->rx_endpoint_to_rxtp_ids.find(endpoint_name);
    if (it == ami_ra_priv->rx_endpoint_to_rxtp_ids.end())
    {
        std::vector<int32_t>& rxtp_id_vec =
            ami_ra_priv->rx_endpoint_to_rxtp_ids[endpoint_name];
        if (impl_->GetRxTransportIdListByName(endpoint_name, rxtp_id_vec) != kSuccess)
            return kFailure;

        it = ami_ra_priv->rx_endpoint_to_rxtp_ids.find(endpoint_name);
    }

    auto* low_level_rx_channel = impl_->GetRxChannel();
    if (low_level_rx_channel == nullptr)
        return kFailure;

    nr_msgs = 0;
    for (auto rxtp_id : it->second)
    {
        // GetTNPHistMsgCnt() is multi-thread safe
        nr_msgs += low_level_rx_channel->GetTNPHistMsgCnt(rxtp_id);
    }

    return kSuccess;
}

ErrorCode_def AmiRecordAgent::GetRxEndpointHistMessage(
    const OnMessageType& on_rx_hist_msg,
    const std::string& endpoint_name,
    const Message::SqnType& begin,
    const Message::SqnType& end)
{
    int32_t rxep_id = impl_->GetRxEndpointIdByName(endpoint_name);
    if (rxep_id == -1)
        return kFailure;

    return GetRxEDPHistMessage(on_rx_hist_msg, rxep_id, begin, end);
}

ErrorCode_def AmiRecordAgent::GenRxMD5(AmiRecordChannel::MD5& md5)
{
    auto* rx_chan = GetRxChannel();
    if (rx_chan == nullptr)
        return kFailure;

    AmiRecordAgentPrivate* ami_ra_priv = (AmiRecordAgentPrivate*)private_data_;
    boost::mutex::scoped_lock lock_guard(ami_ra_priv->get_rx_hist_msg_mutex);
    return rx_chan->GenMD5(md5);
}

ErrorCode_def AmiRecordAgent::GenRxEndpointMD5(
    AmiRecordChannel::MD5& md5,
    const std::string& endpoint_name,
    const Message::SqnType& begin,
    const Message::SqnType& end)
{
    int32_t rxep_id = impl_->GetRxEndpointIdByName(endpoint_name);
    if (rxep_id == -1)
        return kFailure;

    auto* rx_chan = GetRxChannel();
    if (rx_chan == nullptr)
        return kFailure;

    AmiRecordAgentPrivate* ami_ra_priv = (AmiRecordAgentPrivate*)private_data_;
    boost::mutex::scoped_lock lock_guard(ami_ra_priv->get_rx_hist_msg_mutex);
    return rx_chan->GenEDPMD5(md5, rxep_id, begin, end);
}

}  //namespace ami
