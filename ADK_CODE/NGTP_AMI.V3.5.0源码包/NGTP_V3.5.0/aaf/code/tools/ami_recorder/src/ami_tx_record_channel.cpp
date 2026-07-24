/**
 * @author 陈志(chenzhi@af.local)
 */
#include "tx_record_channel.h"
#include <ami/ami_tx_record_channel.h>

#include <boost/thread.hpp>

namespace ami
{

struct AmiTxRecordChannelPrivate
{
    boost::mutex mutex;
};

AmiTxRecordChannel::AmiTxRecordChannel(TxRecordChannel* impl)
    : AmiRecordChannel(impl)
{
    private_data_ = new AmiTxRecordChannelPrivate();
}

AmiTxRecordChannel::AmiTxRecordChannel(const AmiTxRecordChannel& rhs)
    : AmiRecordChannel(rhs)
{
    impl_read_only_.reset(new TxRecordChannel(*(TxRecordChannel*)impl_));
    private_data_ = new AmiTxRecordChannelPrivate();
}

ErrorCode_def AmiTxRecordChannel::GetHistMessage(const OnMessageType& on_hist_msg,
                                                 const Message::SqnType& begin,
                                                 const Message::SqnType& end)
{
    AmiTxRecordChannelPrivate* tx_priv = (AmiTxRecordChannelPrivate*)private_data_;
    boost::mutex::scoped_lock lock_guard(tx_priv->mutex);

    TxRecordChannel* impl = nullptr;
    if (impl_read_only_)
    {
        impl = (TxRecordChannel*)impl_read_only_.get();
    }
    else
    {
        impl = (TxRecordChannel*)impl_;
    }

    if (impl == nullptr)
        return ErrorCode::kFailure;

    return impl->GetHistMessage(
        [&on_hist_msg](AmiMessage* msg) -> ErrorCode {
            Message* app_msg = msg->message();
            return on_hist_msg(app_msg);
        },
        begin, end);
}

ErrorCode_def AmiTxRecordChannel::GenMD5(MD5& md5,
                                         const Message::SqnType& begin,
                                         const Message::SqnType& end)
{
    AmiTxRecordChannelPrivate* tx_priv = (AmiTxRecordChannelPrivate*)private_data_;
    boost::mutex::scoped_lock lock_guard(tx_priv->mutex);

    TxRecordChannel* impl = nullptr;
    if (impl_read_only_)
    {
        impl = (TxRecordChannel*)impl_read_only_.get();
    }
    else
    {
        impl = (TxRecordChannel*)impl_;
    }

    if (impl == nullptr)
        return ErrorCode::kFailure;

    return impl->GenMD5(md5, begin, end);
}

Message::SqnType AmiTxRecordChannel::GetTxTNPHistMsgCnt()
{
    TxRecordChannel* impl = nullptr;
    if (impl_read_only_)
    {
        impl = (TxRecordChannel*)impl_read_only_.get();
    }
    else
    {
        impl = (TxRecordChannel*)impl_;
    }

    if (impl == nullptr)
        return ErrorCode::kFailure;

    return impl->GetTNPHistMsgCnt();
}

}  //namespace ami
