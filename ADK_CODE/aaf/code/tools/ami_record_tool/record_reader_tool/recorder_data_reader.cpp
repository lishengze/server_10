#include "recorder_data_reader.h"
#include "recorder_data_util.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include <iostream>
#include <sstream>

#include <boost/filesystem.hpp>

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

bool CheckCRC(const int fd, const int64_t cur_offset, MsgCRCType& crc)
{
    MsgCRCType local_crc = {0};

    const auto len_to_read = sizeof(MsgCRCType);

    if (len_to_read != pread64(fd, (char*)&local_crc, len_to_read, cur_offset))
    {
        std::cerr << "read local crc failed" << std::endl;
        return false;
    }

    if (local_crc != crc)
    {
        std::cerr << "message CRC check failed" << std::endl;
        return false;
    }
    crc = 0;  // reset crc for next message
    return true;
}

#define AMI_INGRESS_MESSAGE         0x0008

constexpr uint32_t AMI_MAX_MESSAGE_SIZE_INTERNAL = 1024u * 1024u;

namespace recorder_data
{

bool RecorderDataReader::ReadRxMessage(const std::string& context_name, 
                                       const int64_t begin_sqn,
                                       int64_t end_sqn)
{
    if (context_name.empty())
    {
        std::cerr << "context name is empty" << std::endl;
        return false;
    }

    if (end_sqn != 0
        && begin_sqn >= end_sqn)
    {
        std::cerr << "begin sqn: " << begin_sqn 
                  << " is great than end sqn: " << end_sqn << std::endl;
        return false;
    }

    std::stringstream recorder_data_path_stream;
    recorder_data_path_stream << recorder_data_path_ << '/' << context_name;
    if (!boost::filesystem::exists(recorder_data_path_stream.str()))
    {
        std::cerr << "recorder data path: " <<  recorder_data_path_stream.str() << " not exists" << std::endl;
        return false; 
    }

    std::stringstream index_file_stream;
    index_file_stream << recorder_data_path_stream.str() << "/rx/index";
    if (!boost::filesystem::exists(index_file_stream.str()))
    {
        std::cerr << "index file: " <<  index_file_stream.str() << " not exists" << std::endl;
        return false; 
    }

    std::stringstream msg_data_file_stream;
    msg_data_file_stream << recorder_data_path_stream.str() << "/rx/msgdata_0";
    if (!boost::filesystem::exists(msg_data_file_stream.str()))
    {
        std::cerr << "msg data file: " <<  msg_data_file_stream.str() << " not exists" << std::endl;
        return false; 
    }

    int index_fd = open(index_file_stream.str().c_str(), O_RDONLY);
    if (index_fd < 0)
    {
        std::cerr << "open file: " << index_file_stream.str() << " failed, err: " << ::strerror(errno) << std::endl;
        return false; 
    }

    int msg_data_fd = open(msg_data_file_stream.str().c_str(), O_RDONLY);
    if (msg_data_fd < 0)
    {
        std::cerr << "open file: " << msg_data_file_stream.str() << " failed, err: " << ::strerror(errno) << std::endl;
        return false; 
    }
    
    int64_t last_msg_sqn = 0;
    if (!GetLastMsgSqn(index_fd, &last_msg_sqn))
    {
        std::cout << "get last msg sqn failed" << std::endl;
        return false;
    }

    if (end_sqn != 0
       && end_sqn >= last_msg_sqn)
    {
        std::cerr << "end sqn: " << end_sqn << " is greater than last msg sqn: " << last_msg_sqn << std::endl;
        return false;
    }

    if (end_sqn == 0)
    {
        end_sqn = last_msg_sqn;
    }


    uint8_t header_length;
    uint8_t version;
    MsgCRCType crc = {0};
    int64_t offset = 0;
    bool is_check_crc = false;

    // 读取消息头部
    if (!ReadMsgDataHeader(msg_data_fd, &header_length, &version, &is_check_crc))
    {
        std::cerr << "read msg data header failed" << std::endl;
        return false;
    }

    std::cout << "begin_sqn: " << begin_sqn << " end sqn: " << end_sqn << std::endl; 

    char ami_msg_buf[AMI_MAX_MESSAGE_SIZE_INTERNAL] = {0};
    RxRecorderMessage* rx_recorder_message = reinterpret_cast<RxRecorderMessage*>(ami_msg_buf);
    rx_recorder_message->recorder_message.ex_message_header.msg_prop.msg_prop |= AMI_INGRESS_MESSAGE;

    for (auto sqn = begin_sqn; sqn < end_sqn; ++sqn)
    {
        // 读取索引
        if (!ReadOneIndex(index_fd, sqn, &offset))
        {
            std::cerr << "read index failed" << std::endl;
            return false;
        }

        offset += header_length;
        auto endpoint_id_len = sizeof(uint32_t); // rx持久化消息需要先读取endpoint_id和transport_id，再读取消息
        if (endpoint_id_len != pread64(msg_data_fd, (char*)&rx_recorder_message->endpoint_id, endpoint_id_len, offset))
        {
            std::cerr << "read endpoint id failed" << std::endl;
            return false;
        }

        if (is_check_crc)  // 计算CRC
        {
            crc = MsgCRCCalFunc(&rx_recorder_message->endpoint_id, endpoint_id_len, crc);
        }
        offset += endpoint_id_len;

        auto transport_id_len = sizeof(uint32_t); // rx持久化消息需要先读取endpoint_id和transport_id，再读取消息
        if (transport_id_len != pread64(msg_data_fd, (char*)&rx_recorder_message->transport_id, transport_id_len, offset))
        {
            std::cerr << "read transport id failed" << std::endl;
            return false;
        }

        if (is_check_crc) // 计算CRC
        {
            crc = MsgCRCCalFunc(&rx_recorder_message->transport_id, transport_id_len, crc);
        }
        offset += transport_id_len;

        std::cout << "ednpoint_id: " << rx_recorder_message->endpoint_id << std::endl;
        std::cout << "transport_id: " << rx_recorder_message->transport_id << std::endl;

        // 读取一条消息数据
        if (!ReadOneMessage(msg_data_fd, is_check_crc, &offset, crc, &rx_recorder_message->recorder_message))
        {
            std::cerr << "read one message failed" << std::endl;
            return false;
        }
        
        if (is_check_crc && (!CheckCRC(msg_data_fd, offset, crc)))  // 进行CRC检查
        {
            std::cerr << "check crc failed" << std::endl;
            return false;
        }

        std::cout << "read one message: " << rx_recorder_message->recorder_message.data() << std::endl;
    }

    return true;
}

bool RecorderDataReader::ReadTxMessage(const std::string& context_name, 
                                        const std::string& transport_name,
                                        const int64_t begin_sqn,
                                        int64_t end_sqn)
{
    if (context_name.empty())
    {
        std::cerr << "context name is empty" << std::endl;
        return false;
    }

    if (transport_name.empty())
    {
        std::cerr << "transport name is empty" << std::endl;
        return false;
    }

    if (end_sqn != 0
        && begin_sqn >= end_sqn)
    {
        std::cerr << "begin sqn: " << begin_sqn 
                  << " is great than end sqn: " << end_sqn << std::endl;
        return false;
    }

    std::stringstream recorder_data_path_stream;
    recorder_data_path_stream << recorder_data_path_ << '/' << context_name;
    if (!boost::filesystem::exists(recorder_data_path_stream.str()))
    {
        std::cerr << "recorder data path: " <<  recorder_data_path_stream.str() << " not exists" << std::endl;
        return false; 
    }

    std::stringstream index_file_stream;
    index_file_stream << recorder_data_path_stream.str() << "/tx/" << transport_name << "/index";
    if (!boost::filesystem::exists(index_file_stream.str()))
    {
        std::cerr << "index file: " <<  index_file_stream.str() << " not exists" << std::endl;
        return false; 
    }

    std::stringstream msg_data_file_stream;
    msg_data_file_stream << recorder_data_path_stream.str() << "/tx/" << transport_name << "/msgdata_0";
    if (!boost::filesystem::exists(msg_data_file_stream.str()))
    {
        std::cerr << "msg data file: " <<  msg_data_file_stream.str() << " not exists" << std::endl;
        return false; 
    }

    int index_fd = open(index_file_stream.str().c_str(), O_RDONLY);
    if (index_fd < 0)
    {
        std::cerr << "open file: " << index_file_stream.str() << " failed, err: " << ::strerror(errno) << std::endl;
        return false; 
    }

    int msg_data_fd = open(msg_data_file_stream.str().c_str(), O_RDONLY);
    if (msg_data_fd < 0)
    {
        std::cerr << "open file: " << msg_data_file_stream.str() << " failed, err: " << ::strerror(errno) << std::endl;
        return false; 
    }
    
    int64_t last_msg_sqn = 0;
    if (!GetLastMsgSqn(index_fd, &last_msg_sqn))
    {
        std::cout << "get last msg sqn failed" << std::endl;
        return false;
    }

    if (end_sqn != 0
       && end_sqn >= last_msg_sqn)
    {
        std::cerr << "end sqn: " << end_sqn << " is greater than last msg sqn: " << last_msg_sqn << std::endl;
        return false;
    }

    if (end_sqn == 0)
    {
        end_sqn = last_msg_sqn;
    }

    // 读取消息头部
    uint8_t header_length = 0;
    uint8_t version = 0;
    MsgCRCType crc = 0;
    int64_t offset = 0;
    bool is_check_crc = false;

    // 读取消息头部
    if (!ReadMsgDataHeader(msg_data_fd, &header_length, &version, &is_check_crc))
    {
        std::cerr << "read msg data header failed" << std::endl;
        return false;
    }

    std::cout << "begin_sqn: " << begin_sqn << " end sqn: " << end_sqn << std::endl; 

    char ami_msg_buf[AMI_MAX_MESSAGE_SIZE_INTERNAL] = {0};
    RxRecorderMessage* rx_recorder_message = reinterpret_cast<RxRecorderMessage*>(ami_msg_buf);
    rx_recorder_message->recorder_message.ex_message_header.msg_prop.msg_prop = 0;

    for (auto sqn = begin_sqn; sqn < end_sqn; ++sqn)
    {
        // 读取索引
        if (!ReadOneIndex(index_fd, sqn, &offset))
        {
            std::cerr << "read index failed" << std::endl;
            return false;
        }

        offset += header_length;

        // 读取持久化消息，Tx方向不需要读取EndpointId和TransportId
        if (!ReadOneMessage(msg_data_fd, is_check_crc, &offset, crc, &rx_recorder_message->recorder_message))
        {
            std::cerr << "read one message failed" << std::endl;
            return false;
        }
        
        if (is_check_crc && (!CheckCRC(msg_data_fd, offset, crc)))  // 进行CRC检查
        {
            std::cerr << "check crc failed" << std::endl;
            return false;
        }

        std::cout << "read one message: " << rx_recorder_message->recorder_message.data() << std::endl;
    }

    return true;
}

bool RecorderDataReader::ReadMsgDataHeader(const int fd,
                                           uint8_t* header_length,
                                           uint8_t* version,
                                           bool* is_check_crc)
{
    RecordFileHdr reader_header;
    *header_length = pread(fd, (char*)&reader_header, sizeof(RecordFileHdr), 0); 
    if (*header_length != sizeof(RecordFileHdr))
    {
        std::cerr << "read header len failed: " << std::endl;
        return false;
    }

    auto length = *header_length - sizeof(RecordFileHdr);
    auto len = pread(fd, ((char*)&reader_header) + *header_length, length, *header_length);
    if (len < 0)
    {
        std::cerr << "read header failed: " << std::endl;
        return false;
    }

    *version = reader_header.version;
    if (*version != 1)
    {
        std::cerr << "recorder version is invalid, version: " << *version << std::endl;
        return false;
    }

    auto file_opt = reader_header.file_opt;
    if (file_opt | static_cast<uint16_t>(file_option_type::kUseCrc))  // 需要检查crc
    {
        std::cout << "use crc check" << std::endl;
        *is_check_crc = true;
    }

    return true;
}

bool RecorderDataReader::GetLastMsgSqn(const int fd, 
                                       int64_t* last_msg_sqn)
{
    struct stat index_file_stat;
    if (::fstat(fd, &index_file_stat) != 0)
    {
        std::cerr<< "get latest msg sqn failed" << std::endl;
        return false;
    }
    
    auto index_file_size = index_file_stat.st_size;

    *last_msg_sqn = index_file_size / sizeof(OrdinalIndex);
    return true;
}

bool RecorderDataReader::ReadOneIndex(const int fd, 
                                      const int64_t sqn,
                                      int64_t* offset)
{
    OrdinalIndex file_pos;
    auto msg_sqn_pos = (sqn - 1) * sizeof(OrdinalIndex);
    int index_size = sizeof(OrdinalIndex);

    if (index_size != pread64(fd, file_pos.file_pos, index_size, msg_sqn_pos))
    {
        std::cerr << "read one msg index failed, cur msg index pos: <{1}>" << msg_sqn_pos;
        return false;
    }

    auto sqn_offset = reinterpret_cast<int64_t*>(file_pos.file_pos);
    *offset = *sqn_offset; 

    return true;
}

bool RecorderDataReader::ReadOneMessage(const int fd, 
                                        const bool is_check_crc,
                                        int64_t* offset,
                                        MsgCRCType& crc,
                                        RecorderMessage* recorder_message)
{
    const auto len_sqn_to_read = sizeof(recorder_message->stream_sqn) + sizeof(recorder_message->topic_sqn);
    if(len_sqn_to_read != pread64(fd, (char*)&recorder_message->stream_sqn, len_sqn_to_read, *offset))
    {
        std::cerr << "read stream sqn and topic sqn failed" << std::endl;
        return false;
    }
    *offset += len_sqn_to_read;

    const auto app_data_len = sizeof(recorder_message->app_data_len);
    if (app_data_len != pread64(fd, (char*)&recorder_message->app_data_len, app_data_len, *offset))
    {
        std::cerr << "read app data len failed" << std::endl;
        return false;
    }

    *offset += app_data_len;

    if (recorder_message->app_data_len > AMI_MAX_MESSAGE_SIZE_INTERNAL)
    {
        std::cerr << "message exceed ami max limit size: " <<  AMI_MAX_MESSAGE_SIZE_INTERNAL << " bytes"<< std::endl;
        return false;
    }

    const auto msg_header_len = sizeof(MessageHeader);
    if (msg_header_len != sizeof(recorder_message->message_header))
    {
        std::cerr << "msg header len is not equal app_msg.msg_header length" << std::endl;
        return false;
    }

    if (msg_header_len != pread64(fd, (char*)&recorder_message->message_header, msg_header_len, *offset))
    {
        std::cerr << "read msg header failed" << std::endl;
        return false;
    }

    *offset += msg_header_len;

    ExMessageHeader ex_msg_header;

    const auto ex_msg_header_len = sizeof(ExMessageHeader);
    if (ex_msg_header_len != pread64(fd, (char*)&ex_msg_header, ex_msg_header_len, *offset))
    {
        std::cerr << "read ex msg header failed" << std::endl;
        return false;
    }

    *offset += ex_msg_header_len;

    if (recorder_message->app_data_len != pread64(fd, (char*)recorder_message->app_data_begin, recorder_message->app_data_len, *offset))
    {
        std::cerr << "read app data failed, error: " << strerror(errno) << std::endl;
        return false;
    }

    *offset += recorder_message->app_data_len;

    // 进行CRC校验
    if (is_check_crc)
    {
        crc = MsgCRCCalFunc(&recorder_message->stream_sqn,
                        len_sqn_to_read,
                        crc);

        crc = MsgCRCCalFunc(&recorder_message->app_data_len,
                        sizeof(recorder_message->app_data_len),
                        crc);

        crc = MsgCRCCalFunc(&recorder_message->message_header,
                             sizeof(MessageHeader) + recorder_message->app_data_len,
                             crc);
    }

    return true;
}

}