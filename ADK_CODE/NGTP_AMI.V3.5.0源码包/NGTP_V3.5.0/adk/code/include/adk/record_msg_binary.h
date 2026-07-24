/**
* @file       record_msg_binary.h
* @brief       
* @author     luojian@archforce.com.cn
* @date       2018/04/11
*/

#ifndef ADK_IMPL_RECORD_MSG_BINARY_H_
#define ADK_IMPL_RECORD_MSG_BINARY_H_

#include "record_msg.h"
#include "object_pool.h"

#include <string>
#include <cassert>
#include <exception>

#ifdef __ADK_DEBUG__
#define RECORD_MSG_OBJECT_POOL_SIZE 64    
#endif

#ifndef RECORD_MSG_OBJECT_POOL_SIZE
#define RECORD_MSG_OBJECT_POOL_SIZE 8192
#endif

namespace adk_impl
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
        RecordMsgBinary()
        {
            record_msg_ = new RecordMsg<BinaryMsgObject*>(); // 使用非ios模式
            assert(record_msg_ != nullptr) ;
            
            ptr_objectpool_  = ObjectPool<RecordMsgBinary::BinaryMsgObject>::Create("BinaryMsgObject pool"
                , RECORD_MSG_OBJECT_POOL_SIZE);

            assert(ptr_objectpool_ != nullptr);
        }
        /*
        *   @brief 使用io_service方式
        *   @param periodic_timer ios处理定时器即每个多久定时处理
        *        如果队列中存在数据，则不进行睡眠并进行消息处理；
        *           deal_number 每次处理消息数量
        *   @attention 需要调用 Start(false),RunIos();
        */
        explicit RecordMsgBinary(boost::asio::io_service* ios,
            uint32_t periodic_timer=100,
            uint32_t deal_number=10)
        {
            if(nullptr == ios)
            {
                throw std::runtime_error("io_service cannot be nullptr.");
            }
            record_msg_ = new RecordMsg<BinaryMsgObject*>(ios,periodic_timer,deal_number); // 使用ios模式
            assert(record_msg_ != nullptr) ;
        }
    
        ~RecordMsgBinary()
        {
            if (nullptr != record_msg_)
            {
                delete record_msg_;
                record_msg_ = nullptr;
            } 
            if (nullptr != record_file_)
            {
                delete record_file_;
                record_file_ = nullptr;
            }
        }
        /*
        * @breif 设置序列化处理操作
        */
        void SetSerializeFunc(RecordMsg<BinaryMsgObject*>::SerializeMsgFunc serialize_func)
        {
            if(!serialize_func)
                throw std::runtime_error("must to set the serializefunc and recordfunc.") ;
            record_msg_->SetSerializeMsgFunc(serialize_func) ;
        }
        
        /*
        *   @brief 初始化
        *   @param record_file_name     序列化消息存储的文件名
        *           is_addtail_newline  写文件时是否需要增加换行符
        *           que_buffer_size     RecordMsg中队列的大小及缓存消息的最大数量
        *           is_throw_exception  RecordMsg内部出错是否抛出异常
        */
        bool Init(const std::string& record_file_name,
            bool is_addtail_newline,
            uint32_t que_buffer_size=8192,
            bool is_throw_exception = false)
        {
            if(record_file_name.empty())
            {
                throw std::runtime_error("record_file_name is empty.");
            }

            record_file_ = new RecordToLocalFile(record_file_name, is_addtail_newline);
            if(!record_file_)
            {   return false;   }
            if (!record_file_->Init())
            {
                return false;
            }
            
            record_msg_->SetRecorder(
                std::bind(&IRecorder::Record, record_file_,std::placeholders::_1)) ;

            record_msg_->SetFreeFunc(
                [](BinaryMsgObject* object) 
                { 
                    if(object) 
                    {
                        object->Delete();
                    }    
                });
            
            return record_msg_->Init(que_buffer_size,is_throw_exception);
        }
        
         /*
        *   @brief start
        *   @param is_create_thread 是否创建单独处理线程
        *          is_create_thread=true时，将不需要调用Run,RunIos,RunOnce等函数
        */
        void Start(bool is_create_thread = false)
        {
            if(!is_work_)
            {
                is_work_ = true ;
            }   
            
            if(is_create_thread)
            {
                record_msg_->Start(true); 
            }
            else
            {
               record_msg_->Start();
            }
        }
    
        /*
        * @brief        stop
        * @attention    使用io_service方式时，需要在io_service stop之前调用此函数
        */
        void Stop()
        {
           record_msg_->Stop(); // 停止自身创建的线程并使用join等
        }
        
        /*
        * @brief        PutMsg将消息放入处理队列
        * @note         进行数据的深拷贝
        */
        void PutMsg(const char* msg,uint32_t size)
        {
            if(!is_work_)
                return ;
            
            BinaryMsgObject *object = ptr_objectpool_->NewObjectEx() ;
            if(!object)
            {return ;}
            object->binary_msg.assign(msg,size) ;
            record_msg_->PutMsg(object);
            return ;
        }
        
        /*
        * @brief        io_service模式处理消息
        */
        void RunIos()
        {
            if(!is_work_)
                return ;
            record_msg_->RunWithIos();
        }
        
        /*
        * @brief        处理消息，此函数将会阻塞当前线程
        */
        void Run()
        {
            if(!is_work_)
                return ;
            record_msg_->Run();
        }
        
        void RunOnce(int32_t number)
        {
            if(!is_work_)
                return ;
            record_msg_->RunOnce(number);
        }

        #ifdef __ADK_DEBUG__
        uint64_t GetSysMem()
        {
            return ptr_objectpool_->nr_sys_mem_;
        }
        #endif
    
    private:
        RecordMsg<BinaryMsgObject*> *record_msg_; ///< 消息处理
        IRecorder *record_file_{nullptr};   ///<写记录接口
        bool is_work_{false} ; ///< 如果没有调用start函数将不会起作用
        ObjectPool<BinaryMsgObject> *ptr_objectpool_{nullptr} ; 
    };
} //end namespace adk_impl

#endif