/** 
*  Copyright (c) 2021 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_RECORD_MSG_BINARY_V2_H_
#define ADK_RECORD_MSG_BINARY_V2_H_

#include <boost/property_tree/ptree.hpp>

#include "error_code.h"
#include <functional>
#include <string>

namespace adk
{


template<typename ObjectType>
static inline void DoConstruct(void* object_buffer)
{
    new (object_buffer) ObjectType();
}


/**
 * @brief recorder
*/
class RecordMsgBinaryV2
{
public:
    using SerializeMsgFunc = std::string (*)(const void*, uint32_t);

public:
    /**
     * @brief 构造函数，参数说明如下：
     * 1. queue_size为默认值0，则RecordMsgBinaryV2队列总大小为32M，单个队列大小为32M / queue_num;
     * 2. queue_size不为默认值，则RecordMsgBinaryV2队列总大小为(queue_num * queue_size)Bytes，总大小不能
     *    超过512M，如果超过512M，则RecordMsgBinaryV2实际队列数为1，队列实际大小为512M;
     * @note queue_size单位为字节(Byte)
    */
    RecordMsgBinaryV2(uint8_t queue_num = 1, uint32_t queue_size = 0);

    ~RecordMsgBinaryV2();

    /**
     * @brief 设置序列化回调函数
    */
    void SetSerializeFunc(SerializeMsgFunc serialize_func);

    /**
     * @brief 初始化 RecordMsgBinaryV2
     * @param[in] record_file_name: recorder用来记录消息的文件路径; 如果为空则不会写文件L
     * @param[in/out] err_msg 初始化失败时的提示信息
    */
    bool Init(const std::string& record_file_name, std::string* err_msg = nullptr);

    /**
     * @brief 启动一个后台消费者线程从内部队列消费消息
     * @param[in/out] err_msg 启动失败时的提示信息
     * @note 后台线程会使用前面设置的序列化回调函数来"解码"PushMsg放入的消息
    */
    ErrorCode Start(std::string* err_msg = nullptr);

    /**
     * @brief 停止后台线程, 停止recorder
     * @note 即使此时内部队列的消息没有被消费完毕, 后台消费线程也会退出
    */
    void Stop();

    /**
     * @brief 写入消息到recorder内部队列(线程安全)
    */
    ErrorCode PutMsg(const void* msg, uint32_t size, uint8_t queue_index = 0, std::string* err_msg = nullptr);


    /**
     * @brief 批量消费 batch_num 条消息
     * @param batch_num 一次消费的消息条数; 为0的时候表示一直消费至队列为空;
     */
    ErrorCode BatchConsumeMsg(uint32_t batch_num = 0);

    /**
     * @brief 从内部队列上申请一块内存存放消息
     */
    void* AllocBuffer(uint32_t data_len, uint8_t queue_index = 0);


    template <class T>
    T& AllocBuffer(uint8_t queue_index = 0)
    {
        void* alloc_buf = AllocBuffer(sizeof(T), queue_index);
        new (alloc_buf) T();
        return *(T*)(alloc_buf);
    }

    /**
     * @brief 把申请的消息提交到到内部队列
     */
    void PostBuffer(void* entry_data, uint8_t queue_index = 0);

    /**
     * @brief 收集内部队列的指标
     * @param[out] indicator_ptree 保存指标信息的json
     */
    void CollectIndicator(boost::property_tree::ptree& indicator_ptree);

private:
    void* impl_;
};

}  //end namespace adk

#endif
