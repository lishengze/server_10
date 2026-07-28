/** 
 *  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
 *  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
 *  For more information about Archforce, welcome to archforce.cn.
 *  @file message.h
 **/

#ifndef ADK_IMPL_IO_ENGINE_MESSAGE_H_
#define ADK_IMPL_IO_ENGINE_MESSAGE_H_

#include <string>

#include <assert.h>
#include <stdint.h>

namespace adk_impl
{

namespace io_engine
{

class Endpoint;

class Message
{
public:
    char* data() const
    {
        assert(consume_len_ <= capacity_);
        return app_data_ + consume_len_;
    }

    const char* const_data() const
    {
        return data();
    }

    uint32_t data_len() const
    {
        assert(data_len_ >= consume_len_);
        return data_len_ - consume_len_;
    }

    uint32_t free_size() const
    {
        assert(capacity_ >= data_len_);
        return capacity_ - data_len_;
    }

    void set_data_len(uint32_t data_len)
    {
        assert(0 == consume_len_);
        data_len_ = data_len;
    }

    /**
     * @brief        设置该消息后续消息结合处理信息
     *
     * @param[in]    consume_len设置该消息中已经被消费的长度
     * @param[in]    data_more设置该消息后续需要尾追的长度
     *               如果后续长度未知则设置为-1，Engine会将下一次读取到的数据进行尾追
     *
     * @note         该函数用于MessageHandler::OnMessage返回Result::kFollowUp时提供相关后续信息
     *               如果date_more大于0，下次递交为完整消息需要全部消费，否则会导致后续消息截断
     */
    void set_follow_up(uint32_t consume_len, int32_t data_more)
    {
        consume_len_ += consume_len;
        data_flag_ = data_more;
    }

    /**
     * @brief        获取Rx消息所属的endpoint
     *
     * @return       成功返回ErrorCode::kSuccess / 失败返回其他
     */
    Endpoint* endpoint();

    /**
     * @brief        获取Rx消息所属endpoint的自定义的share_ctx
     *
     * @return       成功返回share_ctx / 失败返回nullptr
     */
    void* endpoint_share_ctx();

    /**
     * @brief        通过Rx消息进行消息回复
     *
     * @param[in]    msg 待发送消息
     *
     * @return       成功返回ErrorCode::kSuccess / 失败返回其他
     */
    template<bool kBlock = true>
    int32_t Reply(Message* msg)
    {
        return kBlock ? ReplyBlock(msg) : ReplyUnBlock(msg);
    }

    /**
     * @brief        通过Rx消息进行消息回复
     *
     * @param[in]    buffer 待发送缓存的地址
     * @param[in]    len    待发送缓存的长度
     *
     * @return       成功返回ErrorCode::kSuccess / 失败返回其他
     */
    template<bool kBlock = true>
    int32_t Reply(const void* buffer, uint32_t len)
    {
        return kBlock ? ReplyBlock(buffer, len) : ReplyUnBlock(buffer, len);
    }

    /**
     * @brief        将消息使用权从IO Engine层转移到应用层
     *               以便应用可以在MessageHandler::OnMessage之外使用
     *
     * @attention    需要应用调用DeleteMessage将该消息的管理权将被归还
     *
     * @return       成功返回ErrorCode::kSuccess / 失败返回其他
     */
    int32_t forward_acquire();

    /**
     * @brief        设置消息的扇出情况
     *               用于一个消息通过多个Endpoint发送的场景
     *
     * @param[in]    fanout_nr 指定扇出的Endpoint数量
     *
     * @return       成功返回ErrorCode::kSuccess / 失败返回其他
     */
    int32_t set_fanout(uint32_t fanout_nr);

protected:
    int32_t ReplyBlock(Message* msg);
    int32_t ReplyUnBlock(Message* msg);

    int32_t ReplyBlock(const void* buffer, uint32_t len);
    int32_t ReplyUnBlock(const void* buffer, uint32_t len);

    uint32_t data_len_;
    uint32_t capacity_;
    uint32_t consume_len_;
    int32_t  data_flag_;
    void*    endpoint_ctx_;
    char*    app_data_;
    char     data_[];

    friend class Endpoint;
};

}

}


#endif