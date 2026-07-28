/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*
*  @file spsc_byte_buffer.h
*  @brief 用于单生产者单消费者之间的网络报文处理
**/
#ifndef ADK_SPSC_BYTE_BUFFER_H_
#define ADK_SPSC_BYTE_BUFFER_H_

#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "error_code.h"

namespace adk
{

namespace bytebuffer
{

constexpr bool kNonblock = true;

/**
 * @brief      从网络读取报文，写入ByteBuffer
 */
class Producer
{
public:
    /**
     * @brief      返回ByteBuffer剩余空间大小
     *
     * @return     <=0时，当前ByteBuffer没有足够的空间，需要RenewBuffer重新分配
     */
    int32_t remaining();

    /**
     * @brief      上一次写入ByteBuffer的内存地址
     *
     * @return     返回对应的内存地址
     */
    char* current();

    /**
     * @brief      同current()
     */
    char* Current();
    /**
     * @brief      上一次写入ByteBuffer的字节偏移
     *
     * @return     返回对应的字节偏移
     */
    int32_t position();

    /**
     * @brief      将写入字节偏移增加delta
     *
     * @param[in]  delta  字节偏移的增量
     */
    void inc_position(int32_t delta);

    /**
     * @brief      将写入的有效数据增加delta_visible_bytes字节
     *             这里有效数据为完整的数据包
     *
     * @param[in]  delta_visible_bytes  有效数据的增量
     */
    void inc_valid_data(int32_t delta_visible_bytes);

    /**
     * @brief      设置写入的有效数据末尾至字节偏移visible_bytes处
     *
     * @param[in]  visible_bytes  有效数据末尾的字节偏移
     */
    void set_valid_data(int32_t visible_bytes);

    /**
     * @brief      有效数据末尾
     *
     * @return     返回相应的字节偏移
     */
    int32_t valid_data();

    /**
     * @brief       申请一个新的 ByteBuffer，默认为阻塞模式
     *              该接口会将旧 ByteBuffer 结尾处的非法数据(position() - valid_data())
     *              拷贝值新 ByteBuffer 的起始处
     *
     * @param[in]  is_non_block  指示是否为非阻塞模式
     *
     * @return      成功时返回 ErrorCode::kSuccess
     */ 
    int32_t RenewBuffer(bool is_non_block = false);

    /**
     * @brief      向ByteBuffer中追加len字节的数据
     *
     * @param[in]  src   数据源首地址
     * @param[in]  len   数据长度
     *
     * @return     { description_of_the_return_value }
     */
    void AppendBytes(const void* src, int32_t len);

    /**
     * @brief      除去有效数据后，追加的数据长度
     *
     * @return     返回相应的数据长度
     */
    int32_t AppendBytesSize();

    /**
     * @brief      最近一条有效数据的终止地址
     *
     * @return     返回有效数据的终止地址
     */
    char* ValidDataEnd();

    /**
     * @brief      增加有len字节的有效数据长度, 当ByteBuffer不够时申请新的空间
     *
     * @param[in]  len           有效数据长度
     * @param[in]  is_non_block  是否非阻塞模式
     *
     * @return     成功时返回 ErrorCode::kSuccess
     */
    int32_t IncValidData(int32_t len, bool is_non_block = false);

    /**
     * @brief      清理ByteBuffer中无用的数据
     */
    void Clear();
};

/**
 * @brief      从ByteBuffer读取完整的消息报文
 */
class Consumer
{
public:
    /**
     * @brief      最近一次消费的字节偏移
     *
     * @return     返回相应的偏移
     */
    int32_t position();

    /**
     * @brief      消费指定字节的消息，被消费的消息不能够再碰触
     *
     * @param[in]  bytes  消费的字节长度
     */
    void consume_bytes(int32_t bytes);
	
	/**
	 * @brief      同consume_bytes()
	 */
    void ConsumeBytes(int32_t bytes);

    /**
     * @brief      获取ByteBuffer中完整消息的剩余字节数
     *
     * @return     返回剩余字节数，当返回值<=0时，ByteBuffer为空
     */
    int32_t remaining();
	
	/**
	 * @brief      同remaining()
	 */
    int32_t Remaining();

    /**
     * @brief      ByteBuffer中下一完整消息的起始地址
     *             只有 remaining() > 0 时才可以使用
     *
     * @return     返回相应的地址
     */
    char* current();
	
    /**
	 * @brief      同current()
	 */
    char* Current();
};

/**
 * @brief      单生产者线程，单消费者线程 安全的ByteBuffer对象
 */
class ByteBuffer
{
public:
    ByteBuffer();
    /**
     * @brief      初始化
     *
     * @param[in]  buffer_size  ByteBuffer大小，至少要大于最大的报文大小
     * @param[in]  extra_size   额外空间用于提升ByteBuffer消息，非0时，至少要大于最大的报文大小
     *
     * @return     成功时返回 ErrorCode::kSuccess
     */
    int32_t Init(int32_t buffer_size, int32_t extra_size = 0);

    /**
     * @brief      用于生产者线程生产消息
     *
     * @return     返回生产者端
     */
    Producer* GetBufferProducer();

    /**
     * @brief      用于消费者者线程消费消息
     *
     * @return     返回消费者端
     */
    Consumer* GetBufferConsumer();

private:
    void* buff_impl_;
};

} // bytebuffer

} // adk

#endif // ADK_SPSC_BYTE_BUFFER_H_
