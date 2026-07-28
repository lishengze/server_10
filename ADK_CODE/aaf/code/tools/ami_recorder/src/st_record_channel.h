/**
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_ST_RECORD_CHANNEL_H_
#define AMI_ST_RECORD_CHANNEL_H_

#include "record_channel.h"

namespace ami
{

class StRecordChannel : public RecordChannel
{
public:
    /**
     * 接收状态消息
     *
     * @param on_status_msg 状态消息回调该functor交给调用者处理
     *
     * @return kSuccess - 消息回调完毕；kFailure - 中途出现错误
     *
     * @par 线程模型和生命周期
     * @li 调用本方法的线程将被接管，使用该线程回调on_status_msg。
     * on_status_msg返回后本方法返回。
     * @li 在接收历史消息的过程中，请保持on_status_msg的有效性
     */
    ErrorCode_def GetStatusMessage(const OnAMIMessageType& on_status_msg)
    {
        return reader_->ReadStatusMessage(boost::filesystem::path(RecordDataRoot(data_root_)) / path_,
                                          on_status_msg);
    }

private:
    StRecordChannel(const std::string data_root,
                    const std::string& path,
                    adk::MPSCQueue* msg_ptr_queue,
                    const std::string& tag = std::string())
        : RecordChannel(data_root, path, msg_ptr_queue, tag)
    {
    }

    StRecordChannel(const StRecordChannel& rhs)
        : RecordChannel(rhs)
    {
    }

    virtual void IncMessageCounter(AmiMessage* ami_message)
    {
        ami_message->inc_slave_counter_tx();
    }

    virtual void DecMessageCounter(AmiMessage* ami_message)
    {
        ami_message->dec_slave_counter_tx();
    }

    friend class AsyncRecordClient;
};

}  //namespace ami

#endif /* AMI_RX_RECORD_CHANNEL_H_ */
