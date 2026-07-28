/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_RECORD_MSG_BINARY_H_
#define ADK_RECORD_MSG_BINARY_H_

#include "object_pool.h"

#include <string>
#include <cassert>
#include <exception>
#include <boost/asio.hpp>

namespace adk 
{
    
class RecordMsgBinary
{
public:
    /*
     * @brief 载体类
     */
    class BinaryMsgObject : public IObject
    {
    public:
        std::string binary_msg ; ///< binary msg

        void Reset() override
        {
            binary_msg.clear();
        }
    };

public:
    RecordMsgBinary();

    /*
    *   @brief 使用io_service方式
    *   @param periodic_timer ios处理定时器即每个多久定时处理
    *        如果队列中存在数据，则不进行睡眠并进行消息处理；
    *           deal_number 每次处理消息数量
    *   @attention 需要调用 Start(false),RunIos();
    */
    explicit RecordMsgBinary(boost::asio::io_service* ios, uint32_t periodic_timer = 100, uint32_t deal_number = 10);

    ~RecordMsgBinary();

    /*
     * @breif 设置序列化处理操作
     */
    void SetSerializeFunc(std::function<std::string(BinaryMsgObject*)> serialize_func);
        
    /*
     *   @brief 初始化
     *   @param record_file_name     序列化消息存储的文件名
     *           is_addtail_newline  写文件时是否需要增加换行符
     *           que_buffer_size     RecordMsg中队列的大小及缓存消息的最大数量
     *           is_throw_exception  RecordMsg内部出错是否抛出异常
     */
    bool Init(const std::string& record_file_name, bool is_addtail_newline, 
        uint32_t que_buffer_size = 8192, bool is_throw_exception = false);

    /*
     *   @brief start
     *   @param is_create_thread 是否创建单独处理线程
     *          is_create_thread=true时，将不需要调用Run,RunIos,RunOnce等函数
     */
    void Start(bool is_create_thread = false);

    /*
    * @brief        stop
    * @attention    使用io_service方式时，需要在io_service stop之前调用此函数
    */
    void Stop();

    /*
    * @brief        PutMsg将消息放入处理队列
    * @note         进行数据的深拷贝
    */
    void PutMsg(const char* msg, uint32_t size);

    /*
    * @brief        io_service模式处理消息
    */
    void RunIos();

    /*
    * @brief        处理消息，此函数将会阻塞当前线程
    */
    void Run();

    void RunOnce(int32_t number);

    #ifdef __ADK_DEBUG__
    uint64_t GetSysMem();
    #endif

private:
    void* record_msg_binary_impl_;
};

} //end namespace adk

#endif