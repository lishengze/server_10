/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */

///< posix
#include <unistd.h>  //usleep

///< impl
#include "record_channel.h"

namespace ami
{

namespace bt = boost::property_tree;

LOG_DEFINE(RecordChannel);

constexpr Message::SqnType RecordChannel::kBegin;
constexpr Message::SqnType RecordChannel::kMostRecent;

RecordChannel::RecordChannel(const std::string data_root,
                             const std::string& path,
                             adk::MPSCQueue* msg_ptr_queue,
                             const std::string& tag)
    : msg_ptr_queue_(msg_ptr_queue),
      data_root_(data_root),
      path_(path),
      tag_(tag),
      reader_(new RecordReader()),
      discarded_msg_cnt_(0),
      read_only_(false)
{
}

RecordChannel::RecordChannel(const RecordChannel& rhs)
    : data_root_(rhs.data_root_),
      path_(rhs.path_),
      tag_(rhs.tag_),
      reader_(new RecordReader()),
      discarded_msg_cnt_(0),
      read_only_(true) /*拷贝构造出的channel全部只读*/
{
}

RecordChannel::~RecordChannel()
{
    msg_ptr_queue_ = nullptr;
}

ErrorCode_def RecordChannel::PushMessage(AmiMessage* ami_message)
{
    if (nullptr == ami_message)
    {
        return kSuccess;
    }

    if (read_only_)
    {
        LOG_ERROR_RATELIMITED_VERY_LOW("try push into READ ONLY channel: {1}",
                  boost::filesystem::path(
                      RecordDataRoot(data_root_))
                      / path_);
        return kFailure;
    }

    if (HasError())
    {
        if (discarded_msg_cnt_.is_lock_free())
        {
            ++discarded_msg_cnt_;
        }

        return kSuccess;
    }

    IncMessageCounter(ami_message);

    do
    {
        if (kSuccess == msg_ptr_queue_->EmplacePush<MQMsgEntry>(*ami_message))
        {
            LOG_TRACE("push origin message({1}; {2}) "
                      "into queue(name={3}, index={4}, len={5})",
                      (void*)(ami_message),
                      (void*)(ami_message->message()),
                      msg_ptr_queue_->name(),
                      msg_ptr_queue_->index(),
                      msg_ptr_queue_->length());

            return kSuccess;
        }

        usleep(1);
    } while (!HasError());

    DecMessageCounter(ami_message);

    if (discarded_msg_cnt_.is_lock_free())
    {
        ++discarded_msg_cnt_;
    }

    return kSuccess;
}

ErrorCode_def RecordChannel::AsyncPushMessage(AmiMessage* ami_message)
{
    if (nullptr == ami_message)
    {
        return kSuccess;
    }

    if (read_only_)
    {
        LOG_ERROR_RATELIMITED_VERY_LOW("try push into READ ONLY channel: {1}",
                  boost::filesystem::path(
                      RecordDataRoot(data_root_))
                      / path_);
        return kFailure;
    }

    if (HasError())
        return kSuccess;

    IncMessageCounter(ami_message);

    if (kSuccess == msg_ptr_queue_->EmplacePush<MQMsgEntry>(*ami_message))
    {
        LOG_TRACE("push origin message({1}; {2}) "
                  "into queue(name={3}, index={4}, len={5})",
                  (void*)(ami_message),
                  (void*)(ami_message->message()),
                  msg_ptr_queue_->name(),
                  msg_ptr_queue_->index(),
                  msg_ptr_queue_->length());

        return kSuccess;
    }
    else
    {
        DecMessageCounter(ami_message);
        return kTryAgain;
    }
}

void RecordChannel::DumpToPtree(boost::property_tree::ptree& status_tree) const
{
    bt::ptree& channel_status_tree =
        status_tree.add_child(ChannelName(), bt::ptree());

    channel_status_tree.put("discarded_msg_cnt_",
                            (discarded_msg_cnt_.is_lock_free()
                                 ? std::to_string(discarded_msg_cnt_)
                                 : "n/a"));
}

}  //namespace ami
