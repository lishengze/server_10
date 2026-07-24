/**
 * @author 李云翀
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_RECORD_CHANNEL_H_
#define AMI_RECORD_CHANNEL_H_

///< cpp std
#include <memory>
#include <ostream>
#include <string>

///< boost
#include <boost/atomic.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/thread/mutex.hpp>

///< adk, ami public
#include <adk/lock_free_msg_queue.h>
#include <ami/ami_record_channel.h>
#include <ami/error_code.h>

///< ami impl
#include "../log.h"
#include "../verifier.h"

///< impl
#include "record_iterator.h"
#include "record_keyindex_iterator.h"
#include "record_reader.h"
#include "recorder_base.h"
#include "recorder_fwd.h"

namespace ami
{

class RecordChannel
{
protected:
    typedef AmiRecordChannel::MD5 MD5;

    class MD5Calculator
    {
    public:
        MD5Calculator() : v_(i_)
        {
        }

        ErrorCode operator()(AmiMessage* ami_msg)
        {
            Message* app_msg = ami_msg->message();
            v_.FeedData(app_msg->app_data_begin, app_msg->app_data_len, i_);

            return kSuccess;
        }

        void GenMD5(AmiRecorderBase::MD5CheckCode& md5)
        {
            v_.GenCheckCode(i_, md5);
        }

    private:
        MD5Verifier::InterimType i_;
        MD5Verifier v_;
    };

    static constexpr Message::SqnType kBegin = AmiRecordChannel::kBegin;
    static constexpr Message::SqnType kMostRecent =
        AmiRecordChannel::kMostRecent;

public:
    RecordChannel(const RecordChannel&);
    virtual ~RecordChannel();

    /**
     * @brief 同步推送拟持久化消息
     *
     * @param ami_message 拟持久化的消息
     *
     * @return kSuccess
     *
     * @par 返回的场景
     * @li 推送消息成功（message=nullptr立即返回）
     * @li recorder不存在了，此时消息实际上被丢弃
     * @li 创建本对象的AmiRecordeAgent析构了，此时消息实际上被丢弃
     * @li 用户调用创建本对象的RecordeAgent::ReleasePushThread，此时消息实际上被丢弃
     *
     * @note 线程安全
     *
     * @par 线程模型
     * 调用本接口的线程会被阻塞
     */
    ErrorCode_def PushMessage(AmiMessage* ami_message);

    /**
     * @brief 异步推送拟持久化的消息
     *
     * @param ami_message 拟持久化的消息
     *
     * @par 返回的场景
     * @li 推送消息成功（message=nullptr立即返回） - kSuccess
     * @li 队列满 - kTryAgain
     * @li recorder不存在了，此时消息实际上被丢弃 - kSucess
     * @li 创建本对象的AmiRecordeAgent析构了，此时消息实际上被丢弃 - kSuccess
     *
     * @note 线程安全
     */
    ErrorCode_def AsyncPushMessage(AmiMessage* ami_message);

    void DumpToPtree(boost::property_tree::ptree& status_tree) const;

protected:
    RecordChannel(const std::string data_root,
                  const std::string& path,
                  adk::MPSCQueue* msg_ptr_queue,
                  const std::string& tag = std::string());
    RecordChannel& operator=(const RecordChannel&) = delete;

    std::string ChannelName() const
    {
        if (tag_.empty())
        {
            return path_;
        }
        else
        {
            return path_ + "[" + tag_ + "]";
        }
    }

    void RecorderNotExist()
    {
        recorder_not_exist_ = true;
    }

    void AgentReleased()
    {
        agent_released_ = true;
    }

    bool HasError() const
    {
        return (agent_released_ || recorder_not_exist_);
    }

private:
    virtual void IncMessageCounter(AmiMessage* ami_message) = 0;
    virtual void DecMessageCounter(AmiMessage* ami_message) = 0;

protected:
    adk::MPSCQueue* msg_ptr_queue_ = nullptr;
    std::string data_root_;
    std::string path_;
    std::string tag_;
    boost::mutex reader_lock_;
    std::unique_ptr<RecordReader> reader_;
    bool agent_released_     = false;
    bool recorder_not_exist_ = false;
    boost::atomic<unsigned int> discarded_msg_cnt_;
    bool read_only_;

private:
    friend class RecordAgent;
    friend std::ostream& operator<<(std::ostream&, const RecordChannel&);
    LOG_DECLARE
};

inline std::ostream& operator<<(std::ostream& os, const RecordChannel& chnl)
{
    os << "channel(" << chnl.ChannelName() << "): "
       << "discarded_msg_cnt_ = "
       << (chnl.discarded_msg_cnt_.is_lock_free()
               ? std::to_string(chnl.discarded_msg_cnt_)
               : "n/a");

    return os;
}

}  //namespace ami

#endif /* AMI_RECORD_CHANNEL_H_ */
