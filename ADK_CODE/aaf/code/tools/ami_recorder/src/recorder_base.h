/**
 * @brief recorder的常量和基本类型
 * @author 陈志(chenzhi@af.local)
 */
#ifndef AMI_RECORD_BASE_H_
#define AMI_RECORD_BASE_H_

#include <time.h>

///< cpp std
#include <cstdlib>  //getenv
#include <streambuf>
#include <string>

///< boost
#include <boost/crc.hpp>
#include <boost/filesystem.hpp>
#include <boost/locale/format.hpp>  //format
#include "../log.h"

///< ami public
#include <ami/ami_recorder_base.h>

///< ami impl
#include "../ami_message.h"
#include "../convertor.h"
#include "../util.h"

namespace ami
{

/******************************************************************************
 * 常量和类型
 */
constexpr size_t kContextCapacity(16u);  ///< 最多支持16个context
constexpr size_t kTxChannelCapacity(8u * 1024u);  ///< 最多支持8K个tx channel

///< 1 rx + 1 ack + all tx
constexpr size_t kChannelCapacity = 2 * kContextCapacity + kTxChannelCapacity;

///< 每个可以故障恢复的文件缓存对象占用1k的共享内存，全部是故障恢复时需要的信息
constexpr size_t kFileBufSizeInShm = 1024u;

constexpr const char* kRecordAgent        = "RecordAgent";
constexpr const char* kIndexFileName      = "index";
constexpr const char* kDataFilePrefix     = "msgdata";
constexpr const char* kBackupDirName      = "backup";
constexpr const char* kUnknownInsName     = "unknown@unknown_host";
constexpr const char* kTxPathID           = "tx";
constexpr const char* kRxPathID           = "rx";
constexpr const char* kAckPathID          = "ackedsqn";
constexpr const int kRecorderExitValue    = 0xF0000000;
constexpr const int kRecorderExitImmValue = 0xF0000001;

typedef std::streambuf::pos_type FilePosType;   // 16 bytes
typedef std::streambuf::off_type PosOffType;    // 8 bytes
typedef std::streamsize FileSizeType;           // 8 bytes
typedef size_t FileSqnType;
typedef std::pair<FileSqnType, FilePosType> PhysicalFilePosType;

// #define MsgCRCCalFunc boost::augmented_crc<8, 0xA6>  ///< 计算CRC的方法
// typedef typename boost::uint_t<8>::fast MsgCRCType;

#define MsgCRCCalFunc CalCheckSum  ///< 计算CheckSum的方法
typedef uint32_t MsgCRCType;

static uint32_t CalCheckSum(void const* buffer, size_t byte_count, uint32_t rem)
{
    uint64_t csum = 0;
    size_t i = 0;
    for (; i + 7 < byte_count; i += 8)
    {
        csum += *(uint64_t*)((const char*)buffer + i);
    }

    for (; i < byte_count; ++i)
    {
        csum += *(uint8_t*)((const char*)buffer + i);
    }
    return (uint32_t)(csum + rem);
}

struct MsgData
{
};  ///< 消息数据文件标记
struct Index
{
};  ///< 消息索引文件标记
struct LostMsg
{
};  ///< 丢消息场景的标记

struct RxTag
{
};  ///< RX方向消息标记
struct TxTag
{
};  ///< TX方向消息标记
struct StTag
{
};  ///< 状态消息标记

typedef std::function<ErrorCode(AmiMessage* ami_message)> OnAMIMessageType;
/*****************************************************************************/

/******************************************************************************
 * 持久化消息
 */

/**
 * 消息属性
 */
enum class RMFlagEnum
{
    kNoFlag      = 0,
    kDiscarded   = 1u,  ///消息被丢弃
    kPlaceHodler = 2u  ///占位消息
};
typedef uint32_t RMFlagType;

inline RMFlagEnum operator&(RMFlagEnum a, RMFlagEnum b)
{
    return RMFlagEnum(static_cast<RMFlagType>(a) & static_cast<RMFlagType>(b));
}

inline RMFlagEnum operator|(RMFlagEnum a, RMFlagEnum b)
{
    return RMFlagEnum(static_cast<RMFlagType>(a) | static_cast<RMFlagType>(b));
}

inline RMFlagEnum operator^(RMFlagEnum a, RMFlagEnum b)
{
    return RMFlagEnum(static_cast<RMFlagType>(a) ^ static_cast<RMFlagType>(b));
}

inline RMFlagEnum operator~(RMFlagEnum a)
{
    return RMFlagEnum(~static_cast<RMFlagType>(a));
}

inline RMFlagEnum& operator&=(RMFlagEnum& a, RMFlagEnum b)
{
    return a = a & b;
}

inline RMFlagEnum& operator|=(RMFlagEnum& a, RMFlagEnum b)
{
    return a = a | b;
}

inline RMFlagEnum& operator^=(RMFlagEnum& a, RMFlagEnum b)
{
    return a = a ^ b;
}

inline bool operator!(RMFlagEnum a)
{
    return RMFlagEnum::kNoFlag == a;
}

class RecordedMsgProp
{
public:
    static constexpr RMFlagEnum kNoFlag      = RMFlagEnum::kNoFlag;
    static constexpr RMFlagEnum kDiscarded   = RMFlagEnum::kDiscarded;
    static constexpr RMFlagEnum kPlaceHolder = RMFlagEnum::kPlaceHodler;

public:
    RecordedMsgProp() = default;

    bool operator==(const RecordedMsgProp& rhs) const
    {
        return flags_ == rhs.flags_;
    }

    bool operator!=(const RecordedMsgProp& rhs) const
    {
        return !((*this) == rhs);
    }

    operator bool() const
    {
        return !!flags_;
    }

    /**************************************
 * flags 
 */
    RecordedMsgProp(RMFlagEnum flags)
    {
        flags_ = flags;
    }

    RMFlagEnum operator&(RMFlagEnum flags) const
    {
        return flags_ & flags;
    }

    RMFlagEnum operator|(RMFlagEnum flags) const
    {
        return flags_ | flags;
    }

    RMFlagEnum operator^(RMFlagEnum flags) const
    {
        return flags_ ^ flags;
    }

    RMFlagEnum operator~() const
    {
        return ~flags_;
    }

    RecordedMsgProp& operator&=(RMFlagEnum flags)
    {
        this->flags_ &= flags;
        return (*this);
    }

    RecordedMsgProp& operator|=(RMFlagEnum flags)
    {
        this->flags_ |= flags;
        return (*this);
    }

    RecordedMsgProp& operator^=(RMFlagEnum flags)
    {
        this->flags_ ^= flags;
        return (*this);
    }
    /**************************************/

private:
    RMFlagEnum flags_ = RMFlagEnum::kNoFlag;

    friend std::ostream& operator<<(std::ostream&, const RecordedMsgProp&);
};

inline std::ostream& operator<<(std::ostream& os, const RecordedMsgProp& msg_prop)
{
    if (RecordedMsgProp::kNoFlag == msg_prop.flags_)
    {
        os << "empty";
    }
    else
    {
        os << "d|p - "
           << (!!(msg_prop & RecordedMsgProp::kDiscarded) ? "o" : "x")
           << (!!(msg_prop & RecordedMsgProp::kPlaceHolder) ? "o" : "x");
    }

    return os;
}

namespace recorder
{
struct ExMessageHeader
{
    RecordedMsgProp msg_prop;
    uint32_t pad1;
    uint64_t c_stream_sqn;
    uint64_t c_topic_sqn;
    uint64_t c_endpoint_sqn;
    uint64_t ami_user_context_0;
    uint64_t ami_user_context_1;
    uint64_t recorder_receive_msg_time_ns;
};
}

union RecordedMsgPropUnion {
    RecordedMsgPropUnion()
        : orig_holder()
    {
    }

    RecordedMsgPropUnion(const ami::ExMessageHeader& ex_header)
        : orig_holder(ex_header)
    {
    }

    RecordedMsgPropUnion(const RecordedMsgProp& prop)
        : msg_prop(prop)
    {
    }

    //持久化后Message::ExMessageHeader原来的意义已经没有了，用作消息属性
    ami::ExMessageHeader orig_holder;

    RecordedMsgProp msg_prop;
};

/**
 * ami::Message需要持久化的字段
 * Message::stream_sqn
 * Message::topic_sqn
 * Message::app_data_len
 * Message::msg_header
 * Message::app_data_begin开始的消息数据
 */
//ami::Message持久化的元信息的总长度
constexpr Message::SizeType kAppmsgMetaDataLen =
    sizeof(Message::stream_sqn) + sizeof(Message::topic_sqn)
    + sizeof(Message::app_data_len)
    + sizeof(MessageHeader) + sizeof(recorder::ExMessageHeader);

//ami::Message的消息属性的偏移
constexpr Message::SizeType kAppmsgPropOffset =
    sizeof(Message::stream_sqn) + sizeof(Message::topic_sqn)
    + sizeof(Message::app_data_len)
    + sizeof(MessageHeader);

struct MsgRecord
{
    typedef Message::SqnType SqnType;
    typedef Message::SizeType SizeType;
    typedef AmiMetaData::IDType IDType;

    enum SignalNo
    {
        kRepairWithPlaceHolder = 1
    };

    IDType endpoint_id;
    IDType transport_id;
    Message::SqnType stream_sqn;
    Message::SqnType topic_sqn;
    Message::SizeType pad1;
    Message::SizeType app_data_len;
    MessageHeader msg_header;

    // FIXME: remove the following fields to reduce the structure size
    recorder::ExMessageHeader ex_msg_header;
    char* app_data;

    MsgRecord(SignalNo sig,
              Message::SqnType range_len)
        : stream_sqn(range_len),
          app_data_len((Message::SizeType)sig)
    {
    }

    MsgRecord(const AmiMessage& ami_msg)
        : endpoint_id(ami_msg.ami_meta_data.endpoint_id),
          transport_id(ami_msg.ami_meta_data.transport_id)
    {
        const Message& msg                         = *(const_cast<AmiMessage&>(ami_msg).message());
        stream_sqn                                 = msg.stream_sqn;
        topic_sqn                                  = msg.topic_sqn;
        app_data_len                               = msg.app_data_len;
        msg_header                                 = msg.msg_header;
    }

    bool IsSignal(SignalNo sig) const
    {
        return (SignalNo)app_data_len == sig;
    }

    Message::SqnType GetPlaceHolderCnt() const
    {
        return stream_sqn;
    }

    friend std::ostream& operator<<(std::ostream&, const MsgRecord&);
};

inline std::ostream& operator<<(std::ostream& os, const MsgRecord& msg_record)
{
    os << "endpoint_id=" << msg_record.endpoint_id << " "
       << "transport_id=" << msg_record.transport_id << " "
       << "stream_sqn=" << msg_record.stream_sqn << " "
       << "topic_sqn=" << msg_record.topic_sqn << " "
       << "app_data_len=" << msg_record.app_data_len << " "
       << "header.option_offset=" << msg_record.msg_header.option_offset << " "
       << "header.stream_id=" << msg_record.msg_header.stream_id << " "
       << "header.ancestor_id=" << msg_record.msg_header.ancestor_id;

    return os;
}

inline MsgRecord* NewPlaceHolderMsgRecord(const MsgRecord& prototype)
{
    MsgRecord* ret    = new MsgRecord(prototype);
    ret->app_data_len = 0;  //占位消息没有消息体
    RecordedMsgPropUnion prop_union;
    prop_union.msg_prop         = RecordedMsgProp(RecordedMsgProp::kPlaceHolder);
    ret->ex_msg_header.msg_prop = prop_union.msg_prop;

    return ret;
}

struct MQMsgEntry
{
    adk::ShmPointer ami_msg_shm_pointer;
    MsgRecord msg_record;

    MQMsgEntry(AmiMessage& ami_msg)
        : ami_msg_shm_pointer(
              Convertor::ConvertToMemoryBuffer(&ami_msg)->shm_ptr),
          msg_record(ami_msg)
    {
    }

    MQMsgEntry(MsgRecord::SignalNo sig, Message::SqnType range_len)
        : msg_record(sig, range_len)
    {
        ami_msg_shm_pointer.Reset();
    }

    AmiMessage* GetOrigAmiMsg(adk::MPManager& mp_manager)
    {
        return AmiMessage::ConvertFromShmPointer(
            &ami_msg_shm_pointer, mp_manager);
    }

    adk::Entry* GetEncloingAdkEntry()
    {
        return ADK_CONTAINER_OF(this, adk::Entry, buffer);
    }

    static MQMsgEntry* InitFromAdkEntry(adk::Entry* entry,
                                        adk::MPManager& mp_manager)
    {
        MQMsgEntry* ret = (MQMsgEntry*)entry->buffer;
        ret->InitAfterMM(mp_manager);
        return ret;
    }

    static MQMsgEntry* ConvertFromAdkEntry(adk::Entry* entry)
    {
        return (MQMsgEntry*)entry->buffer;
    }

    static bool IsSignalRecord(adk::Entry* entry, MsgRecord::SignalNo sig)
    {
        auto msg_entry = ConvertFromAdkEntry(entry);

        if (0 != msg_entry->ami_msg_shm_pointer.value)
        {
            return false;
        }

        return msg_entry->msg_record.IsSignal(sig);
    }

private:
    void InitAfterMM(adk::MPManager& mp_manager)
    {
        AmiMessage* orig_ami_msg = GetOrigAmiMsg(mp_manager);
        msg_record.app_data =
            orig_ami_msg->message()->app_data_begin;

        struct timespec tp;
        if (ADK_LIKELY(clock_gettime(CLOCK_REALTIME, &tp) == 0))
        {
            msg_record.ex_msg_header.recorder_receive_msg_time_ns = tp.tv_sec * 1000000000UL + tp.tv_nsec;
        }
    }
};
/*****************************************************************************/

/******************************************************************************
 * 名字组合
 */
inline std::string TX_PATH(const std::string& ctx, const std::string& tn)
{
    return (boost::filesystem::path(ctx) / std::string(kTxPathID) / tn).string();
}

inline std::string RX_PATH(const std::string& ctx)
{
    return (boost::filesystem::path(ctx) / std::string(kRxPathID)).string();
}

inline std::string ST_PATH(const std::string& ctx)
{
    return (boost::filesystem::path(ctx) / std::string(kAckPathID)).string();
}

class RecorderId
{
public:
    RecorderId() {}

    explicit RecorderId(pid_t pid)
        : pid_(pid)
    {
    }

    RecorderId(pid_t pid, const std::string& ins_name, const std::string &data_path)
        : pid_(pid), // recorder pid
          ins_name_(ins_name), // recorder name
          data_path_(data_path) // recorder data path
    {
    }

    bool IsVoid() const
    {
        return -1 == pid_;
    }

    pid_t GetPid() const
    {
        return pid_;
    }

    std::string GetInsName() const
    {
        return ins_name_;
    }

    // get recorder data path
    std::string GetDataPath() const
    {
        return data_path_;
    }

    std::string Id() const
    {
        if (IsVoid())
        {
            return kUnknownInsName;
        }

        if (ins_name_.empty())
        {
            return (boost::locale::format("pid{1}")
                    % pid_)
                .str();
        }

        return ins_name_;
    }

    bool operator==(const RecorderId &other) const 
    {
        return (pid_ == other.pid_ && ins_name_ == other.ins_name_);
    }

    bool operator!=(const RecorderId &other) const 
    {
        return !(*this == other);
    }

private:
    pid_t pid_ = -1;
    std::string ins_name_;
    std::string data_path_;

    friend std::ostream& operator<<(std::ostream& os, const RecorderId& id);
};

inline std::ostream& operator<<(std::ostream& os, const RecorderId& id)
{
    os << id.Id();
    return os;
}

inline std::string UnixSockPath(const RecorderId& id)
{
    std::string ret = ((boost::locale::format("{1}/.ami_domain_socket/{2}/rc_unix_socket_{3}")
                         % GetLoginUserHome() // recorder data path
                         % id.GetInsName() // recorder name
                         % id.GetPid()) // recorder pid
                            .str());
    boost::system::error_code ec;
    if (!boost::filesystem::exists(ret, ec))
    {
        boost::filesystem::create_directories(ret, ec);
    }
    return ret;
}

inline std::string RecordDataRoot(const std::string& data_root,
                                  const RecorderId& id = RecorderId())
{
    return data_root;
    //    return ( boost::filesystem::path(data_root)/RCD_INT_NAME(port) ).string();
}

inline std::string GetOrdinalIndexFilePath(const std::string& track_data_path)
{
    boost::filesystem::path filepath =
        boost::filesystem::path(track_data_path)
        / boost::filesystem::path(kIndexFileName);
    return filepath.string();
}

inline std::string GetMsgDataFilePath(const std::string& track_data_path,
                                      size_t msgdata_filesqn)
{
    boost::filesystem::path filepath =
        boost::filesystem::path(track_data_path)
        / (boost::locale::format("{1}_{2}")
           % kDataFilePrefix
           % msgdata_filesqn)
              .str();

    return filepath.string();
}

template <typename KeyType>
inline std::string GetKeyindexFilePath(const std::string& track_data_path, const KeyType& key_value)
{
    return (boost::filesystem::path(track_data_path)
            / boost::filesystem::path((boost::locale::format("{1}-{2}_{3}")
                                       % KeyType::KeyTypeName()
                                       % key_value.HashCode()
                                       % kIndexFileName)
                                          .str()))
        .string();
}

/*****************************************************************************/

/******************************************************************************
 * 名字分解
 */

///< 从track path中分解出context name
inline std::string CtxName(const std::string track_path)
{
    return (boost::filesystem::path(track_path).begin()->string());
}

///< 从track path中分解出transport name
inline std::string TransportName(const std::string tx_track_path)
{
    return (boost::filesystem::path(tx_track_path).rbegin()->string());
}

/*****************************************************************************/

}  //namespace ami
namespace fmt
{
template <> 
struct formatter<ami::MsgRecord> : formatter<string_view>
{
    template<typename FormatContext>
    auto format(const ami::MsgRecord& x, FormatContext& ctx) -> decltype(this->formatter<string_view>::format(string_view{}, ctx))
    {
        std::ostringstream os;
        os << x;
        return formatter<string_view>::format(string_view(os.str()), ctx);
    }
};
}
#endif /* AMI_RECORD_BASE_H_ */
