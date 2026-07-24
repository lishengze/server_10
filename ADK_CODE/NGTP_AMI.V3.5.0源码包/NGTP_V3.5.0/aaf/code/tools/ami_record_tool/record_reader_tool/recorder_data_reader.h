#ifndef RECORDER_DATA_READER_H
#define RECORDER_DATA_READER_H

#include <string>
#include <vector>
#include <boost/crc.hpp>

namespace recorder_data
{

// typedef typename boost::uint_t<8>::fast MsgCRCType;
typedef uint32_t MsgCRCType;

struct RecorderMessage;

class RecorderDataReader
{
public:
    RecorderDataReader(const std::string& recorder_data_path):
    recorder_data_path_(recorder_data_path)
    {}

    ~RecorderDataReader() = default;

    /**
     * @brief      读取Rx方向上某个context的持久化消息，如果end不等于0，则读取[begin, end)内的消息, 
     *             如果end = 0，则读到文件末尾
     *
     * @param[in]  context_name  指定context
     * @param[in]  begin_sqn     指定读取数据的起始序号
     * @param[in]  end_sqn       指定读取数据的结束序号
     * 
     * @return     成功时返回ture，失败时返回false
     */
    bool ReadRxMessage(const std::string& context_name,
                       const int64_t begin_sqn = 1,
                       int64_t end_sqn = 0);


    /**
     * @brief      读取Tx方向上某个context的持久化消息，如果end不等于0，则读取[begin, end)内的消息, 
     *             如果end = 0，则读到文件末尾
     *
     * @param[in]  context_name    指定context
     * @param[in]  transport_name  指定transport
     * @param[in]  begin_sqn       指定读取数据的起始序号
     * @param[in]  end_sqn         指定读取数据的结束序号
     * 
     * @return     成功时返回ture，失败时返回false
     */
    bool ReadTxMessage(const std::string& context_name, 
                       const std::string& transport_name,
                       const int64_t begin_sqn = 1,
                       int64_t end_sqn = 0);

private:
    /**
     * @brief      读取持化文件的最大消息序号
     *
     * @param[in]  fd            指定索引文件对应的fd
     * @param[out] last_msg_sqn  保存获取到的最大的消息序号
     * 
     * @return     成功时返回ture，失败时返回false
     */
    bool GetLastMsgSqn(const int fd, 
                       int64_t* last_msg_sqn);

    /**
     * @brief      读取持久化消息头部数据
     *
     * @param[in]  fd            指定持久化数据文件对应的fd
     * @param[out] header_length 保存获取到的头部长度
     * @param[out] version       保存获取到的版本号
     * @param[out] is_check_crc  是否使用CRC检查
     * 
     * @return     成功时返回ture，失败时返回false
     */
    bool ReadMsgDataHeader(const int fd,
                           uint8_t* header_length,
                           uint8_t* version,
                           bool* is_check_crc);

    /**
     * @brief      读取索引
     *
     * @param[in]  fd            指定索引文件对应的fd
     * @param[in]  sqn           指定消息序号
     * @param[out] offset        保存索引对应数据的文件偏移位置，读取消息时需要加上消息头部长度
     * 
     * @return     成功时返回ture，失败时返回false
     */
    bool ReadOneIndex(const int fd, 
                      const int64_t sqn,
                      int64_t* offset);

    /**
     * @brief      读取持久化消息
     *
     * @param[in]  fd                  指定持久化数据文件对应的fd
     * @param[in]  is_check_crc        指定是否进行CRC检查
     * @param[out] offset              指定消息文件的起始偏移量
     * @param[out] crc                 指定crc循环校验码
     * @param[out] recorder_message    保存持久化消息数据
     *
     * 
     * @return     成功时返回ture，失败时返回false
     */
    bool ReadOneMessage(const int fd,
                        const bool is_check_crc,
                        int64_t* offset, 
                        MsgCRCType& crc,
                        RecorderMessage* recorder_message);

private:
    std::string recorder_data_path_;
};

}

#endif 