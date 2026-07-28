#include <md5.h>
#include <boost/regex.hpp>
#include <boost/filesystem.hpp>
#include <adk/arch/generic.h>
#include <ami/ami_recorder_reader.h>
#include "recorder_base.h"
#include "record_reader.h"
#include "record_data_tracker.h"
#include "../log.h"

namespace ami
{
LOG_DEFINE(ami::AMIRecorderReader)
LOG_LOCAL(ami::AMIRecorderReader)

class RecordDataTracker;

constexpr Message::SqnType AMIRecorderReader::kBegin;
constexpr Message::SqnType AMIRecorderReader::kMostRecent;

class OpenSSLMD5
{
public:
    ErrorCode Init()
    {
        md5_init(&md5_ctx_);
        return ErrorCode::kSuccess; 
    }

    ErrorCode FeedData(void* data, const size_t len)
    {
        md5_append(&md5_ctx_, reinterpret_cast<md5_byte_t*>(data), static_cast<int>(len));
        return ErrorCode::kSuccess;
    }
    
    ErrorCode GenMD5Code(AMIRecorderReader::MD5Code &md5_code)
    {
        md5_finish(&md5_ctx_, md5_code.GetRawMD5());
        return ErrorCode::kSuccess;
    }

private:
    md5_state_t md5_ctx_;
};

static std::vector<Message::IDType> GetIDList(const boost::regex &reg, const boost::filesystem::path &path)
{
    std::vector<Message::IDType> id_vec;
    boost::system::error_code ec;
    boost::filesystem::directory_iterator beg(path, ec);
    if (ec)
    {
        LOG_ERROR("path invaild, error message:<{1}>", ec.message());
        return id_vec;
    }
    boost::filesystem::directory_iterator end;
    while(beg != end)
    {
        if (boost::filesystem::is_regular_file(*beg))
        {
            boost::smatch result;
            auto file_name = beg->path().filename().string(); // get the file name save as left value
            if (boost::regex_match(file_name, result, reg)) // check the file name
            {
                try
                {
                    id_vec.push_back(std::stoull(result[1]));
                }
                catch (...)
                {
                }
            }
        }
        ++beg;
    }

    return id_vec;
}

std::ostream& operator<<(std::ostream &os, const AMIRecorderReader::MD5Code &md5_code)
{
    os << "[ ";
    std::for_each(&md5_code.md5_[0], &md5_code.md5_[MD5_DIGEST_LENGTH], [&os](unsigned char ch){ os << (int)ch << " "; });
    os << " ]";
    return os;
}

std::vector<std::string> AMIRecorderReader::GetContextNameList(const std::string &recorder_data_path)
{
    std::vector<std::string> context_name_vec;
    boost::filesystem::path path(recorder_data_path);
    if (!boost::filesystem::exists(path) || !boost::filesystem::is_directory(path))
    {
        LOG_WARN("recorder data path invalid, path:<{1}> don't exist", recorder_data_path);
        return context_name_vec;
    }
    
    boost::system::error_code ec;
    boost::filesystem::directory_iterator beg(path, ec);
    if (ec)
    {
        LOG_ERROR("path invaild, error message:<{1}>", ec.message());
        return context_name_vec;
    }    
    boost::filesystem::directory_iterator end;

    while (beg != end)
    {
        if (boost::filesystem::is_directory(*beg))
        {
            auto name = beg->path().filename().string();
            if (name != "lock" && name != "backup" && name != "temp")
            {
                context_name_vec.push_back(std::move(name));
            }
        }
        ++beg;
    }

    return context_name_vec;
}

AMIRecorderReader* AMIRecorderReader::CreateAMIRecorderReader(const std::string &recorder_data_path, const std::string &context_name)
{
    if (!boost::filesystem::exists(recorder_data_path))
    {
        LOG_ERROR("recorder data path invalid, path:<{1}> don't exist", recorder_data_path);
        return nullptr;
    }

    boost::filesystem::path path(recorder_data_path);
    path /= context_name;
    if (!boost::filesystem::exists(path))
    {
        LOG_ERROR("context name invalid, path:<{1}> don't exist", path.string());
        return nullptr;
    }

    return new AMIRecorderReader(recorder_data_path, context_name);
}

AMIRecorderReader* AMIRecorderReader::CreateAMIRecorderReader(const Property& props)
{
    auto context_name = props.GetValue(config::context::recorder::kName, std::string());
    auto recorder_props = props.GetValue(config::context::kRecorder, Property());
    auto recorder_data_path = recorder_props.GetValue(config::context::recorder::kDataPath, std::string());
    
    auto* ami_recorder_reader = CreateAMIRecorderReader(recorder_data_path, context_name);
    if (ami_recorder_reader == nullptr)
    {
        return nullptr;
    }
    
    ami_recorder_reader->record_data_tracker_ = std::make_shared<RecordDataTracker>();
    if (ami_recorder_reader->record_data_tracker_ == nullptr)
    {
        return nullptr;
    }
    
    if (ami_recorder_reader->record_data_tracker_->Init(props) != ErrorCode::kSuccess)
    {
        return nullptr;
    }
    
    if (ami_recorder_reader->record_data_tracker_->Start() != ErrorCode::kSuccess)
    {
        return nullptr;
    }
    
    return ami_recorder_reader;
}

AMIRecorderReader::AMIRecorderReader(const std::string &recorder_data_path, const std::string &context_name)
{
    recorder_data_path_ = recorder_data_path;
    context_name_ = context_name;
    rx_path_ = (boost::filesystem::path(recorder_data_path_) / context_name_ / kRxPathID).string();
    tx_path_ = (boost::filesystem::path(recorder_data_path_) / context_name_ / kTxPathID).string();
    status_path_ = (boost::filesystem::path(recorder_data_path_) / context_name_ / kAckPathID).string();

    auto tx_stream_id_vec = GetTxStreamIDList();
    for (const auto &stream_id : tx_stream_id_vec)
    {
        GetTxTransportPath(stream_id);
    }

    recorder_reader_ = new RecordReader;
}

AMIRecorderReader::~AMIRecorderReader()
{
    if (recorder_reader_)
    {
        delete recorder_reader_;
        recorder_reader_ = nullptr;
    }
    
    if (record_data_tracker_ != nullptr)
    {
        record_data_tracker_->Stop();
        record_data_tracker_ = nullptr;
    }
}

std::vector<Message::IDType> AMIRecorderReader::GetRxStreamIDList()
{
    static boost::regex reg("^STREAM_ID-([0-9]+)_index$"); //STREAM_ID-2073391958_index
    return GetIDList(reg, rx_path_);
}

std::vector<Message::IDType> AMIRecorderReader::GetRxTransportIDList()
{
    static boost::regex reg("^TRANSPORT_ID-([0-9]+)_index$"); //TRANSPORT_ID-53524_index
    return GetIDList(reg, rx_path_);
}

std::vector<Message::IDType> AMIRecorderReader::GetRxEndpointIDList()
{
    static boost::regex reg("^ENDPOINT_ID-([0-9]+)_index$"); //ENDPOINT_ID-5331_index
    return GetIDList(reg, rx_path_);
}

std::vector<std::string> AMIRecorderReader::GetTransporNametList()
{
    std::vector<std::string> transport_name_vec;
    
    boost::system::error_code ec;
    boost::filesystem::directory_iterator beg(tx_path_, ec);
    if (ec)
    {
        LOG_ERROR("path invaild, error message:<{1}>", ec.message());
        return transport_name_vec;
    }    
    boost::filesystem::directory_iterator end;
    while (beg != end)
    {
        if (boost::filesystem::is_directory(*beg))
        {
            transport_name_vec.push_back(beg->path().filename().string());
        }

        ++beg;
    }

    return transport_name_vec;
}

std::vector<Message::IDType> AMIRecorderReader::GetTxStreamIDList()
{
    typedef std::vector<Message::IDType>::iterator iter_type;
    static boost::regex reg("^STREAM_ID-([0-9]+)_index$"); //STREAM_ID-2073391958_index

    std::vector<Message::IDType> id_vec;
    auto transport_name_vec = GetTransporNametList();
    for (const auto &transport_name : transport_name_vec)
    {
        auto tmp_id_vec = GetIDList(reg, GetTxTransportPath(transport_name));
        if (ADK_LIKELY(tmp_id_vec.size() == 1))
        {
           id_vec.push_back(std::move(*tmp_id_vec.begin()));
        }
        else
        {
            std::copy(std::move_iterator<iter_type>(tmp_id_vec.begin()), std::move_iterator<iter_type>(tmp_id_vec.end()), std::back_inserter(id_vec));
        }        
    }

    return id_vec;
}

std::vector<std::pair<std::string, Message::IDType>> AMIRecorderReader::GetTxTransportNameStreamIDList()
{
    typedef std::vector<Message::IDType>::iterator iter_type;
    static boost::regex reg("^STREAM_ID-([0-9]+)_index$"); //STREAM_ID-2073391958_index

    std::vector<std::pair<std::string, Message::IDType>> name_id_vec;
    auto transport_name_vec = GetTransporNametList();
    for (const auto &transport_name : transport_name_vec)
    {
        auto tmp_id_vec = GetIDList(reg, GetTxTransportPath(transport_name));
        if (ADK_LIKELY(tmp_id_vec.size() == 1))
        {
           name_id_vec.emplace_back(std::make_pair(transport_name, std::move(*tmp_id_vec.begin())));
        }
    }
    
    return name_id_vec;
}
    
ErrorCode_def AMIRecorderReader::GetRxMessage(const MessageHandler &on_rx_hist_msg,
                                              const Message::SqnType &begin,
                                              const Message::SqnType &end)
{
    assert(recorder_reader_);
    return recorder_reader_->ReadRxHistMessage(rx_path_, begin, end, [&](AmiMessage *msg) -> ErrorCode
    {
        Message *app_msg = msg->message();
        return on_rx_hist_msg(app_msg);
    });
}

ErrorCode_def AMIRecorderReader::GetRxStreamMessage(const MessageHandler &on_rx_hist_msg,
                                                    const Message::IDType &stream_id,
                                                    const Message::SqnType &begin,
                                                    const Message::SqnType &end)
{
    assert(recorder_reader_);
    return recorder_reader_->ReadRxSTRHistMessage(rx_path_, stream_id, begin, end, [&](AmiMessage *msg) -> ErrorCode
    {
        Message *app_msg = msg->message();
        return on_rx_hist_msg(app_msg);
    });
}

ErrorCode_def AMIRecorderReader::GetRxTransportMessage(const MessageHandler &on_rx_hist_msg,
                                                       const Message::IDType &transport_id,
                                                       const Message::SqnType &begin,
                                                       const Message::SqnType &end)
{
    assert(recorder_reader_);
    return recorder_reader_->ReadRxTNPHistMessage(rx_path_, transport_id, begin, end, [&](AmiMessage *msg) -> ErrorCode
    {
        Message *app_msg = msg->message();
        return on_rx_hist_msg(app_msg);
    });
}

ErrorCode_def AMIRecorderReader::GetRxEndpointMessage(const MessageHandler &on_rx_hist_msg,
                                                      const Message::IDType &endpoint_id,
                                                      const Message::SqnType &begin,
                                                      const Message::SqnType &end)
{
    assert(recorder_reader_);
    return recorder_reader_->ReadRxEDPHistMessage(rx_path_, endpoint_id, begin, end, [&](AmiMessage *msg) -> ErrorCode
    {
        Message *app_msg = msg->message();
        return on_rx_hist_msg(app_msg);
    });
}

ErrorCode_def AMIRecorderReader::GetTxStreamMessage(const MessageHandler &on_tx_hist_msg,
                                                     const Message::IDType &stream_id,
                                                     const Message::SqnType &begin,
                                                     const Message::SqnType &end)
{
    assert(recorder_reader_);
    return recorder_reader_->ReadTxSTRHistMessage(GetTxTransportPath(stream_id), stream_id, begin, end, [&](AmiMessage *msg) -> ErrorCode
    {
        Message *app_msg = msg->message();
        return on_tx_hist_msg(app_msg);
    });
}

ErrorCode_def AMIRecorderReader::GetTxTransportMessage(const MessageHandler &on_tx_hist_msg,
                                                       const std::string &transport_name,
                                                       const Message::SqnType &begin,
                                                       const Message::SqnType &end)
{
    assert(recorder_reader_);
    auto path = GetTxTransportPath(transport_name);
    return recorder_reader_->ReadTxHistMessage(path, begin, end, [&](AmiMessage *msg) -> ErrorCode
    {
        Message *app_msg = msg->message();
        return on_tx_hist_msg(app_msg);
    });
}

ErrorCode_def AMIRecorderReader::GetTxEndpointMessage(const MessageHandler &on_hist_msg,
                                                      const std::string &endpoint_name,
                                                      const int32_t partition_no,
                                                      const Message::SqnType &begin,
                                                      const Message::SqnType &end)
{
    assert(recorder_reader_);
    auto tp_str = endpoint_name + "_" + std::to_string(partition_no);
    auto transport_name_vec = GetTransporNametList();
    for (const auto &transport_name : transport_name_vec)
    {
        if (transport_name.substr(0, tp_str.size()) == tp_str)
        {
            return GetTxTransportMessage(on_hist_msg, transport_name, begin, end);
        }
    }

    return ErrorCode::kFailure;
}

ErrorCode_def AMIRecorderReader::GetContextStatus(const MessageHandler &on_tx_hist_msg,
                                                  const Message::SqnType &begin,
                                                  const Message::SqnType &end)

{
    assert(recorder_reader_);
    return recorder_reader_->ReadStatusMessage(status_path_, [&](AmiMessage *msg) -> ErrorCode
    {
        Message *app_msg = msg->message();
        return on_tx_hist_msg(app_msg);
    });
}

ErrorCode_def AMIRecorderReader::GetTxMessageCnt(Message::SqnType  &nr_msgs)
{
    assert(recorder_reader_);
    nr_msgs = 0;
    auto transport_name_vec = GetTransporNametList();
    for (const auto transport_name : transport_name_vec)
    {
        nr_msgs += recorder_reader_->GetHistMsgCnt(GetTxTransportPath(transport_name));
    }

    return ErrorCode::kSuccess;
}

ErrorCode_def AMIRecorderReader::GetRxMessageCnt(Message::SqnType &nr_msgs)
{
    assert(recorder_reader_);
    nr_msgs = recorder_reader_->GetHistMsgCnt(rx_path_);
    return ErrorCode::kSuccess;    
}

ErrorCode_def AMIRecorderReader::GetTxEndpointMessageCnt(const std::string &endpoint_name, Message::SqnType &nr_msgs)
{
    assert(recorder_reader_);
    nr_msgs = 0;
    auto transport_name_vec = GetTransporNametList();
    for (const auto &transport_name : transport_name_vec)
    {
        if (transport_name.substr(0, endpoint_name.size()) == endpoint_name)
        {
            nr_msgs += recorder_reader_->GetHistMsgCnt(GetTxTransportPath(transport_name));
        }
    }
    return ErrorCode::kSuccess;
}

ErrorCode_def AMIRecorderReader::GetTxEndpointMessageCnt(const std::string& endpoint_name, const int32_t partition_no, Message::SqnType &nr_msgs)
{
    assert(recorder_reader_);
    nr_msgs = 0;
    auto tp_str = endpoint_name + "_" + std::to_string(partition_no);
    auto transport_name_vec = GetTransporNametList();
    for (const auto &transport_name : transport_name_vec)
    {
        if (transport_name.substr(0, tp_str.size()) == tp_str)
        {
            nr_msgs = recorder_reader_->GetHistMsgCnt(GetTxTransportPath(transport_name));
            return ErrorCode::kSuccess;
        }
    }
    return ErrorCode::kFailure;
}

ErrorCode_def AMIRecorderReader::GetTxTransportMessageCnt(const std::string &transport_name, Message::SqnType &nr_msgs)
{
    assert(recorder_reader_);
    nr_msgs = 0;
    auto transport_path = GetTxTransportPath(transport_name);
    nr_msgs = recorder_reader_->GetTxSTRHistMsgCnt(transport_path, GetStreamID(transport_path));
    return ErrorCode::kSuccess;
}

ErrorCode_def AMIRecorderReader::GetTxStreamMessageCnt(const Message::IDType &stream_id, Message::SqnType &nr_msgs)
{
    assert(recorder_reader_);
    nr_msgs = 0;
    auto transport_path = GetTxTransportPath(stream_id);
    nr_msgs = recorder_reader_->GetTxSTRHistMsgCnt(transport_path, stream_id);
    return ErrorCode::kSuccess;
}

ErrorCode_def AMIRecorderReader::GetRxEndpointMessageCnt(const Message::IDType &endpoint_id, Message::SqnType &nr_msgs)
{
    assert(recorder_reader_);
    nr_msgs = 0;
    nr_msgs = recorder_reader_->GetRxEDPHistMsgCnt(rx_path_, endpoint_id);
    return ErrorCode::kSuccess;
}

ErrorCode_def AMIRecorderReader::GetRxTransportMessageCnt(const Message::IDType &transport_id, Message::SqnType &nr_msgs)
{
    assert(recorder_reader_);
    nr_msgs = 0;
    nr_msgs = recorder_reader_->GetRxTNPHistMsgCnt(rx_path_, transport_id);
    return ErrorCode::kSuccess;
}

ErrorCode_def AMIRecorderReader::GetRxStreamMessageCnt(const Message::IDType &stream_id, Message::SqnType &nr_msgs)
{
    assert(recorder_reader_);
    nr_msgs = 0;
    nr_msgs = recorder_reader_->GetRxSTRHistMsgCnt(rx_path_, stream_id);
    return ErrorCode::kSuccess;
}

ErrorCode_def AMIRecorderReader::GenTxStreamMD5(MD5Code &md5,
                                                const Message::IDType &stream_id,
                                                const Message::SqnType &begin,
                                                const Message::SqnType &end)
{
    OpenSSLMD5 openssl_md5;
    IF_ERR_RET(openssl_md5.Init());
    auto msg_hd = [&openssl_md5](Message* message) -> ErrorCode
    {
        return openssl_md5.FeedData(message->app_data_begin, message->app_data_len);
    };

    IF_ERR_RET(GetTxStreamMessage(msg_hd, stream_id, begin, end));
    IF_ERR_RET(openssl_md5.GenMD5Code(md5));

    return ErrorCode::kSuccess;
}

ErrorCode_def AMIRecorderReader::GenTxTransportMD5(MD5Code &md5,
                                                   const std::string &transport_name,
                                                   const Message::SqnType &begin,
                                                   const Message::SqnType &end)
{
    OpenSSLMD5 openssl_md5;
    IF_ERR_RET(openssl_md5.Init());
    auto msg_hd = [&openssl_md5](Message* message) -> ErrorCode
    {
        return openssl_md5.FeedData(message->app_data_begin, message->app_data_len);
    };

    IF_ERR_RET(GetTxTransportMessage(msg_hd, transport_name, begin, end));
    IF_ERR_RET(openssl_md5.GenMD5Code(md5));

    return ErrorCode::kSuccess;
}

ErrorCode_def AMIRecorderReader::GenTxEndpointMD5(MD5Code &md5,
                                                 const std::string& endpoint_name,
                                                 const int32_t partition_no,
                                                 const Message::SqnType &begin,
                                                 const Message::SqnType &end)
{
    OpenSSLMD5 openssl_md5;
    IF_ERR_RET(openssl_md5.Init());
    auto msg_hd = [&openssl_md5](Message* message) -> ErrorCode
    {
        return openssl_md5.FeedData(message->app_data_begin, message->app_data_len);
    };

    IF_ERR_RET(GetTxEndpointMessage(msg_hd, endpoint_name, partition_no, begin, end));
    IF_ERR_RET(openssl_md5.GenMD5Code(md5));

    return ErrorCode::kSuccess;
}

ErrorCode_def AMIRecorderReader::GenRxMD5(MD5Code &md5)
{
    OpenSSLMD5 openssl_md5;
    IF_ERR_RET(openssl_md5.Init());
    auto msg_hd = [&openssl_md5](Message* message) -> ErrorCode
    {
        return openssl_md5.FeedData(message->app_data_begin, message->app_data_len);
    };

    IF_ERR_RET(GetRxMessage(msg_hd));
    IF_ERR_RET(openssl_md5.GenMD5Code(md5));

    return ErrorCode::kSuccess;
}

ErrorCode_def AMIRecorderReader::GenRxEndpointMD5(MD5Code &md5,
                                                  const Message::IDType &endpoint_id,
                                                  const Message::SqnType &begin,
                                                  const Message::SqnType &end)
{
    OpenSSLMD5 openssl_md5;
    IF_ERR_RET(openssl_md5.Init());
    auto msg_hd = [&openssl_md5](Message* message) -> ErrorCode
    {
        return openssl_md5.FeedData(message->app_data_begin, message->app_data_len);
    };

    IF_ERR_RET(GetRxEndpointMessage(msg_hd, endpoint_id, begin, end));
    IF_ERR_RET(openssl_md5.GenMD5Code(md5));

    return ErrorCode::kSuccess;
}

ErrorCode_def AMIRecorderReader::GenRxStreamMD5(MD5Code &md5,
                                                const Message::IDType &stream_id,
                                                const Message::SqnType &begin,
                                                const Message::SqnType &end)
{
    OpenSSLMD5 openssl_md5;
    IF_ERR_RET(openssl_md5.Init());
    auto msg_hd = [&openssl_md5](Message* message) -> ErrorCode
    {
        return openssl_md5.FeedData(message->app_data_begin, message->app_data_len);
    };

    IF_ERR_RET(GetRxStreamMessage(msg_hd, stream_id, begin, end));
    IF_ERR_RET(openssl_md5.GenMD5Code(md5));

    return ErrorCode::kSuccess;
}

ErrorCode_def AMIRecorderReader::GenRxTransportMD5(MD5Code &md5,
                                                   const Message::IDType &transport_id,
                                                   const Message::SqnType &begin,
                                                   const Message::SqnType &end)
{
    OpenSSLMD5 openssl_md5;
    IF_ERR_RET(openssl_md5.Init());
    auto msg_hd = [&openssl_md5](Message* message) -> ErrorCode
    {
        return openssl_md5.FeedData(message->app_data_begin, message->app_data_len);
    };

    IF_ERR_RET(GetRxTransportMessage(msg_hd, transport_id, begin, end));
    IF_ERR_RET(openssl_md5.GenMD5Code(md5));

    return ErrorCode::kSuccess;
}

ErrorCode_def AMIRecorderReader::RegisterErrorHandler(const OnError on_error)
{
    if (record_data_tracker_ == nullptr)
    {
        return ErrorCode::kFailure;
    }
    
    return record_data_tracker_->RegisterErrorHandler(on_error);
}
     
ErrorCode_def AMIRecorderReader::RegisterCompleteHandler(const OnComplete on_complete)
{
    if (record_data_tracker_ == nullptr)
    {
        return ErrorCode::kFailure;
    }
    
    return record_data_tracker_->RegisterCompleteHandler(on_complete);
}

int32_t AMIRecorderReader::AsyncReadRxMessage(const std::string& context_name, 
                                             const OnMessage on_message,
                                             const Message::SqnType &begin,
                                             const Message::SqnType &end,
                                             const Property& props)
{
    if (record_data_tracker_ == nullptr)
    {
        return -1;
    }
    
    return record_data_tracker_->AsyncReadRxMessage(context_name, on_message, begin, end, props);
}

int32_t AMIRecorderReader::AsyncReadRxMessage(const std::string& context_name, 
                                             const uint64_t endpoint_id,
                                             const OnMessage on_message,
                                             const Message::SqnType &begin,
                                             const Message::SqnType &end,
                                             const Property& props)
{
    if (record_data_tracker_ == nullptr)
    {
        return -1;
    }
    
    return record_data_tracker_->AsyncReadRxMessage(context_name, endpoint_id, on_message, begin, end, props);
}

int32_t AMIRecorderReader::AsyncReadRxStreamMessage(const std::string& context_name, 
                                                   const uint64_t stream_id,
                                                   const OnMessage on_message,
                                                   const Message::SqnType &begin,
                                                   const Message::SqnType &end,
                                                   const Property& props)
{
    if (record_data_tracker_ == nullptr)
    {
        return -1;
    }
    
    return record_data_tracker_->AsyncReadRxStreamMessage(context_name, stream_id, on_message, begin, end, props);
}

int32_t AMIRecorderReader::AsyncReadRxTransportMessage(const std::string& context_name, 
                                                      const uint64_t transport_id,
                                                      const OnMessage on_message,
                                                      const Message::SqnType &begin,
                                                      const Message::SqnType &end,
                                                      const Property& props)
{
    if (record_data_tracker_ == nullptr)
    {
        return -1;
    }
    
    return record_data_tracker_->AsyncReadRxTransportMessage(context_name, transport_id, on_message, begin, end, props);
}  

 int32_t AMIRecorderReader::AsyncReadTxMessage(const std::string& context_name, 
                                              const std::string& transport_name,
                                              const OnMessage on_message,
                                              const Message::SqnType &begin,
                                              const Message::SqnType &end,
                                              const Property& props)
{
    if (record_data_tracker_ == nullptr)
    {
        return -1;
    }
    
    return record_data_tracker_->AsyncReadTxMessage(context_name, transport_name, on_message, begin, end, props);
}

int32_t AMIRecorderReader::AsyncReadTxMessage(const std::string& context_name, 
                                            const std::string& transport_name,
                                            const uint64_t  stream_id,
                                            const OnMessage on_message,
                                            const Message::SqnType &begin,
                                            const Message::SqnType &end,
                                            const Property& props)
{
    if (record_data_tracker_ == nullptr)
    {
        return -1;
    } 
    
    return record_data_tracker_->AsyncReadTxMessage(context_name, transport_name, stream_id, on_message, begin, end, props);
}
                            
ErrorCode_def AMIRecorderReader::GetRxMessageCount(const std::string& context_name, Message::SqnType& count)
{
    if (record_data_tracker_ == nullptr)
    {
        return ErrorCode::kFailure;
    }
    
    return record_data_tracker_->GetRxMessageCount(context_name, count);
}
    
ErrorCode_def AMIRecorderReader::GetRxMessageCount(const std::string& context_name, const uint64_t endpoint_id, Message::SqnType& count)
{
    if (record_data_tracker_ == nullptr)
    {
        return ErrorCode::kFailure;
    }
    
    return record_data_tracker_->GetRxMessageCount(context_name, endpoint_id, count);
}

ErrorCode_def AMIRecorderReader::GetRxStreamMessageCount(const std::string& context_name, const uint64_t stream_id, Message::SqnType& count)
{
    if (record_data_tracker_ == nullptr)
    {
        return ErrorCode::kFailure;
    }
    
    return record_data_tracker_->GetRxStreamMessageCount(context_name, stream_id, count);
}
    
ErrorCode_def AMIRecorderReader::GetRxTransportMessageCount(const std::string& context_name, const uint64_t transport_id, Message::SqnType& count)
{
    if (record_data_tracker_ == nullptr)
    {
        return ErrorCode::kFailure;
    }
    
    return record_data_tracker_->GetRxTransportMessageCount(context_name, transport_id, count);
}
    
ErrorCode_def AMIRecorderReader::GetTxMessageCount(const std::string& context_name, const std::string& transport_name, Message::SqnType& count)
{
    if (record_data_tracker_ == nullptr)
    {
        return ErrorCode::kFailure;
    }
    
    return record_data_tracker_->GetTxMessageCount(context_name, transport_name, count);
}
    
ErrorCode_def AMIRecorderReader::GetTxMessageCount(const std::string& context_name, 
                                                   const std::string& transport_name, 
                                                   const uint64_t stream_id,
                                                   Message::SqnType& count)
{
    if (record_data_tracker_ == nullptr)
    {
        return ErrorCode::kFailure;
    }
    
    return record_data_tracker_->GetTxMessageCount(context_name, transport_name, stream_id, count);
}

ErrorCode_def AMIRecorderReader::StopAsyncReadMessage(const int32_t async_task_id)
{
    if (record_data_tracker_ == nullptr)
    {
        return ErrorCode::kFailure;
    }
    
    return record_data_tracker_->StopAsyncReadMessage(async_task_id); 
}
    
ErrorCode_def AMIRecorderReader::StopAllAsyncReadMessageComplete(uint32_t wait_timeout_milli)
{
    if (record_data_tracker_ == nullptr)
    {
        return ErrorCode::kFailure;
    }
    
    return record_data_tracker_->StopAllAsyncReadMessageComplete(wait_timeout_milli); 
}

ErrorCode_def AMIRecorderReader::Stop()
{
    if (record_data_tracker_ == nullptr)
    {
        return ErrorCode::kFailure;
    }
    
    return record_data_tracker_->Stop(); 
}

std::string AMIRecorderReader::GetTxTransportPath(const std::string &transprot_name)
{
    return tx_path_ + "/" + transprot_name;
}

Message::IDType AMIRecorderReader::GetStreamID(const std::string &path)
{
    static boost::regex reg("^STREAM_ID-([0-9]+)_index$"); //STREAM_ID-2073391958_index
    auto id_vec = GetIDList(reg, path);
    if (ADK_LIKELY(!id_vec.empty()))
    {
        return *id_vec.begin();
    }

    return 0;
}

std::string AMIRecorderReader::GetTxTransportPath(const Message::IDType &stream_id)
{
    static std::string empty_path;
    static boost::regex reg("^STREAM_ID-([0-9]+)_index$"); //STREAM_ID-2073391958_index

    std::lock_guard<std::mutex> lck(stream_id_path_map_mtx_);
    auto it = stream_id_path_map_.find(stream_id);
    if (it != stream_id_path_map_.end())
    {
        return it->second;    
    }

    auto transport_name_vec = GetTransporNametList();
    for (const auto &transport_name : transport_name_vec)
    {
        auto transport_path = GetTxTransportPath(transport_name);
        auto tmp_id_vec = GetIDList(reg, transport_path);
        for (const auto &id : tmp_id_vec)
        {
            stream_id_path_map_[id] = transport_path;
            if (stream_id == id)
            {
                return transport_path;
            }
        }
    }

    return empty_path;
}

} // ami
