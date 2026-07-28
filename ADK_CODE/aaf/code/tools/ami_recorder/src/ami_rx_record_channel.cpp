/**
 * @author 陈志(chenzhi@af.local)
 */

#include "rx_record_channel.h"
#include <ami/ami_rx_record_channel.h>

namespace ami
{

AmiRxRecordChannel::AmiRxRecordChannel(RxRecordChannel* impl)
    : AmiRecordChannel(impl)
{
}

AmiRxRecordChannel::AmiRxRecordChannel(const AmiRxRecordChannel& rhs)
    : AmiRecordChannel(rhs)
{
    impl_read_only_.reset(new RxRecordChannel(*(RxRecordChannel*)impl_));
}

ErrorCode_def AmiRxRecordChannel::GetHistMessage(const OnMessageType& on_hist_msg,
                                                 const Message::SqnType& begin,
                                                 const Message::SqnType& end)
{
    RxRecordChannel* impl = nullptr;
    if (impl_read_only_)
    {
        impl = (RxRecordChannel*)impl_read_only_.get();
    }
    else
    {
        impl = (RxRecordChannel*)impl_;
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

ErrorCode_def AmiRxRecordChannel::GetTNPHistMessage(const OnMessageType& on_hist_msg,
                                                    const Message::IDType& transport_id,
                                                    const Message::SqnType& begin,
                                                    const Message::SqnType& end)
{
    RxRecordChannel* impl = nullptr;
    if (impl_read_only_)
    {
        impl = (RxRecordChannel*)impl_read_only_.get();
    }
    else
    {
        impl = (RxRecordChannel*)impl_;
    }

    if (impl == nullptr)
        return ErrorCode::kFailure;

    return impl->GetTNPHistMessage(
        [&on_hist_msg](AmiMessage* msg) -> ErrorCode {
            Message* app_msg = msg->message();
            return on_hist_msg(app_msg);
        },
        transport_id, begin, end);
}

ErrorCode_def AmiRxRecordChannel::GetEDPHistMessage(const OnMessageType& on_hist_msg,
                                                    const Message::IDType& endpoint_id,
                                                    const Message::SqnType& begin,
                                                    const Message::SqnType& end)
{
    RxRecordChannel* impl = nullptr;
    if (impl_read_only_)
    {
        impl = (RxRecordChannel*)impl_read_only_.get();
    }
    else
    {
        impl = (RxRecordChannel*)impl_;
    }

    if (impl == nullptr)
        return ErrorCode::kFailure;

    return impl->GetEDPHistMessage(
        [&on_hist_msg](AmiMessage* msg) -> ErrorCode {
            Message* app_msg = msg->message();
            return on_hist_msg(app_msg);
        },
        endpoint_id, begin, end);
}

ErrorCode_def AmiRxRecordChannel::GenMD5(MD5& md5,
                                         const Message::SqnType& begin,
                                         const Message::SqnType& end)
{
    RxRecordChannel* impl = nullptr;
    if (impl_read_only_)
    {
        impl = (RxRecordChannel*)impl_read_only_.get();
    }
    else
    {
        impl = (RxRecordChannel*)impl_;
    }

    if (impl == nullptr)
        return ErrorCode::kFailure;

    return impl->GenMD5(md5, begin, end);
}

ErrorCode_def AmiRxRecordChannel::GenTNPMD5(MD5& md5,
                                            const Message::IDType& transport_id,
                                            const Message::SqnType& begin,
                                            const Message::SqnType& end)
{
    RxRecordChannel* impl = nullptr;
    if (impl_read_only_)
    {
        impl = (RxRecordChannel*)impl_read_only_.get();
    }
    else
    {
        impl = (RxRecordChannel*)impl_;
    }

    if (impl == nullptr)
        return ErrorCode::kFailure;

    return impl->GenTNPMD5(md5, transport_id, begin, end);
}

ErrorCode_def AmiRxRecordChannel::GenEDPMD5(MD5& md5,
                                            const Message::IDType& endpoint_id,
                                            const Message::SqnType& begin,
                                            const Message::SqnType& end)
{
    RxRecordChannel* impl = nullptr;
    if (impl_read_only_)
    {
        impl = (RxRecordChannel*)impl_read_only_.get();
    }
    else
    {
        impl = (RxRecordChannel*)impl_;
    }

    if (impl == nullptr)
        return ErrorCode::kFailure;

    return impl->GenEDPMD5(md5, endpoint_id, begin, end);
}

}  //namespace ami
