/**
 * @brief 持久化消息的索引
 * @author Chen Zhi
 */
#ifndef AMI_RECORDED_MESSAGE_INDEX_H_
#define AMI_RECORDED_MESSAGE_INDEX_H_

///< cpp std
#include <ostream>
#include <streambuf>

///< adk, ami public
#include <ami/message.h>

///< ami impl
#include "../ami_message.h"

///< impl
#include "recorder_base.h"

namespace ami
{

typedef unsigned long long HashMapKeyType;

constexpr size_t kIndexBufferSize = 2 * 1024;  ///< 索引文件的写缓存大小统一设为2K

/**
 * 消息的序数索引
 *
 * 每个消息都对应一个该消息的序数索引。
 * 消息的序数索引存储为一个序数索引文件，消息的序数索引严格按照消息的存储顺序存储，
 * 即1号消息存储在消息文件的第一个，1号消息对应的序号索引也存储在序数索引文件的第
 * 一个，以此类推。
 */
class OrdinalIndex
{
public:
    OrdinalIndex()
        : file_pos(0)
    {
    }

    OrdinalIndex(const FilePosType& pos)
        : file_pos(pos)
    {
    }

    char* ValueBegin() const
    {
        return (char*)this;
    }

    static constexpr SizeType ValueSize()
    {
        return sizeof(OrdinalIndex);
    }

    FilePosType GetPos() const
    {
        return file_pos;
    }

    /**
     * 获取消息序号为sqn的消息的索引在序数索引文件中的位置
     */
    static FilePosType GetMsgIndexPos(const Message::SqnType& sqn)
    {
        return (sqn - 1) * sizeof(OrdinalIndex);
    }

    /**
     * 获取位于序数索引文件的位置index_file_pos处的消息的序号
     */
    static Message::SqnType GetMsgSqn(const FilePosType& index_file_pos)
    {
        return (index_file_pos / ValueSize());
    }

    bool operator<(const OrdinalIndex& rhs) const
    {
        return (file_pos < rhs.file_pos);
    }

    bool operator==(const OrdinalIndex& rhs) const
    {
        return ((!((*this) < rhs)) && (!(rhs < (*this))));
    }

private:
    FilePosType file_pos;

    friend std::ostream& operator<<(std::ostream&, const OrdinalIndex&);
};

inline std::ostream& operator<<(std::ostream& os, const OrdinalIndex& file_pos_index)
{
    os << "pos<" << (std::streamsize)file_pos_index.file_pos << ">";
    return os;
}

/**************************************************************************
 * 定义基于各种关键字的索引
 */

///< transport_id为键的索引
struct TransportKey
{
    typedef typename AmiMetaData::IDType KeyType;

    TransportKey()
        : key_value()
    {
    }

    TransportKey(const KeyType& key_value_)
        : key_value(key_value_)
    {
    }

    TransportKey(const HashMapKeyType& value_hash_code)
        : key_value(static_cast<const KeyType>(value_hash_code))
    {
    }

    static std::string KeyTypeName()
    {
        return "TRANSPORT_ID";
    }

    static HashMapKeyType TypeHashCode()
    {
        return typeid(TransportKey).hash_code();
    }

    HashMapKeyType HashCode() const
    {
        return static_cast<HashMapKeyType>(key_value);
    }

    bool operator==(const TransportKey& rhs) const
    {
        return key_value == rhs.key_value;
    }

    bool operator!=(const TransportKey& rhs) const
    {
        return !(this->operator==(rhs));
    }

    KeyType key_value;
};

///< endpoint_id为键的索引
struct EndpointKey
{
    typedef typename AmiMetaData::IDType KeyType;

    EndpointKey()
        : key_value()
    {
    }

    EndpointKey(const KeyType& key_value_)
        : key_value(key_value_)
    {
    }

    EndpointKey(const HashMapKeyType& value_hash_code)
        : key_value(static_cast<const KeyType>(value_hash_code))
    {
    }

    static std::string KeyTypeName()
    {
        return "ENDPOINT_ID";
    }

    static HashMapKeyType TypeHashCode()
    {
        return typeid(EndpointKey).hash_code();
    }

    HashMapKeyType HashCode() const
    {
        return static_cast<HashMapKeyType>(key_value);
    }

    bool operator==(const EndpointKey& rhs) const
    {
        return key_value == rhs.key_value;
    }

    bool operator!=(const EndpointKey& rhs) const
    {
        return !(this->operator==(rhs));
    }

    KeyType key_value;
};

///< stream_id为键的索引
struct StreamKey
{
    typedef typename MessageHeader::IDType KeyType;

    StreamKey()
        : key_value()
    {
    }

    StreamKey(const KeyType& key_value_)
        : key_value(key_value_)
    {
    }

    StreamKey(const HashMapKeyType& value_hash_code)
        : key_value(static_cast<const KeyType>(value_hash_code))
    {
    }

    static std::string KeyTypeName()
    {
        return "STREAM_ID";
    }

    static HashMapKeyType TypeHashCode()
    {
        return typeid(StreamKey).hash_code();
    }

    HashMapKeyType HashCode() const
    {
        return static_cast<HashMapKeyType>(key_value);
    }

    bool operator==(const StreamKey& rhs) const
    {
        return key_value == rhs.key_value;
    }

    bool operator!=(const StreamKey& rhs) const
    {
        return !(this->operator==(rhs));
    }

    KeyType key_value;
};
/*************************************************************************/

/**
 * 目前一个通道的数据文件最多有3种索引，包括基本全序索引和两种关键字索
 * 引
 */
constexpr size_t kKeyIndexTypeCnt = 3;
constexpr size_t kIndexTypeCnt    = 1 + kKeyIndexTypeCnt;

}  // namespace ami

#endif /* AMI_RECORDED_MESSAGE_INDEX_H_ */
