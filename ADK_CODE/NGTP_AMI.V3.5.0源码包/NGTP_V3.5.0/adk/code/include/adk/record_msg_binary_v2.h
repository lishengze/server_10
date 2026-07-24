/**
* @file       record_msg_binary_v2.h
* @brief       
* @author     wanglei@archforce.com.cn
* @date       2021/08/11
*/

#ifndef ADK_IMPL_RECORD_MSG_BINARY_V2_H_
#define ADK_IMPL_RECORD_MSG_BINARY_V2_H_

#include "record_msg.h"
#include <adk/lock_free_cont_memory.h>
#include <adk/arch/synchronize.h>
#include <cassert>
#include <exception>
#include <mutex>
#include <string>

namespace adk_impl
{

inline static void SetErrMsg(std::string* err_msg, const char* s)
{
    if (err_msg != nullptr)
    {
        *err_msg = std::move(std::string(s));
    }
}

class RecordMsgBinaryV2
{
public:
    using SerializeMsgFunc = std::string (*)(const void*, uint32_t);

public:
    //用于存储消息内容的内存RING BUFFER: default: 32MB, max: 512M
    constexpr static int kMaxContMemSize = 512 * 1024 * 1024;
    constexpr static int kDefaultContMemSize = 32 * 1024 * 1024;
    //单个消息的最大大小: 4MB  
    constexpr static int kMsgMaxSize       = 4 * 1024 * 1024;   
    constexpr static int MsgMaxContentSize()
    {  
        // kMsgMaxSize表示的单个消息内容还包含消息Header部分, 消息内容需要减去Header长度
        // Header 预留10KB
        return kMsgMaxSize - 10 * 1024;
    }

public:
    RecordMsgBinaryV2(uint8_t queue_num = 1, uint32_t queue_size = 0)
    {
        if(queue_num == 0)
        {
            queue_num = 1;
        }

        if (queue_size == 0)
        {
            // 未指定队列大小，则使用默认大小
            queue_size = kDefaultContMemSize / queue_num;
        }

        if (queue_size * queue_num > kMaxContMemSize)
        {
            // 如果总大小超过了限制, 则使用一个队列, 队列大小使用默认的最大值
            queue_size = kMaxContMemSize;
            queue_num = 1;
        }

        #ifndef NDEBUG
        std::cout << "queue num: " << (uint32_t)queue_num << ", queue size: " << queue_size << std::endl;
        #endif

        for (auto i = 0; i < queue_num; i++)
        {
            auto* continue_memory = adk_impl::ContinueMemory::Create(queue_size, kMsgMaxSize);
            continue_memory_vec_.push_back(continue_memory);
        }

        // 默认为1000微秒
        consume_interval_ = 1000;
        // 支持用环境变量更改间隔时间 
        char* env_str = std::getenv("ADK_RECORDER_CONSUME_INTERVAL");
        if (env_str != nullptr)
        {
            auto consume_interval = atoi(env_str);
            if(consume_interval != 0)
            {
                consume_interval_ = consume_interval;
            }
        }
    }

    ~RecordMsgBinaryV2()
    {
        if (nullptr != record_file_)
        {
            delete record_file_;
            record_file_ = nullptr;
        }
    }

    void SetSerializeFunc(SerializeMsgFunc serialize_func)
    {
        serialize_msg_func_ = serialize_func;
    }

    bool Init(const std::string& record_file_name, std::string* err_msg = nullptr)
    {
        if (record_file_name.empty())
        {  
            // 文件名为空则不会写磁盘
            return true;
        }
        record_file_ = new RecordToLocalFile(record_file_name, true);
        if (!record_file_->Init())
        {
            SetErrMsg(err_msg, "RecordToLocalFile::Init failed.");
            return false;
        }
        return true;
    }

    /**
     * @brief 启动一个后台线程来消费(落盘)
     * @param err_msg 返回启动失败的原因描述
    */
    ErrorCode Start(std::string* err_msg = nullptr)
    {
        if (start_flag_)
        {
            SetErrMsg(err_msg, "Start failed: BgThread already exist.");
            return ErrorCode::kFailure;
        }
        else
        {
            auto prev = start_flag_.exchange(true);
            if (prev)
            {
                SetErrMsg(err_msg, "Start failed: BgThread already exist.");
                return ErrorCode::kFailure;
            }
        }
        bg_thread_ = std_thread("adk_recordmsg", "bg write-msg thread", boost::bind(&RecordMsgBinaryV2::BgThrdConsumeMsg, this));
        return ErrorCode::kSuccess;
    }

    void Stop()
    {  
        // 让后台线程结束并等待其结束
        start_flag_.store(false);
        if (bg_thread_.joinable())
        {
            bg_thread_.join();
        }
    }


    ErrorCode PutMsg(const void* msg, uint32_t msg_size, uint8_t queue_index = 0, std::string* err_msg = nullptr)
    {
        if (msg_size > MsgMaxContentSize())
        {
            SetErrMsg(err_msg, "PutMsg Failed: Msg too long.");
            return ErrorCode::kFailure;
        }
        adk_impl::ContEntry* entry_ptr = nullptr;
        auto* continue_memory = continue_memory_vec_[queue_index % continue_memory_vec_.size()];

        auto ret = continue_memory->LockAllocEntry(msg_size, &entry_ptr, lock_);
        
        if (ErrorCode::kSuccess != ret)
        {
            SetErrMsg(err_msg, "PutMsg Failed: LockAllocEntry error.");
            return ret;
        }
        memcpy(entry_ptr->GetBuffer(), msg, msg_size);
        continue_memory->PostEntryThreadSafe(entry_ptr);
        return ErrorCode::kSuccess;
    }

    void* AllocBuffer(uint32_t data_len, uint8_t queue_index = 0)
    {
        if (data_len > MsgMaxContentSize())
        { 
            return nullptr;
        }

        adk_impl::ContEntry* entry_ptr = nullptr;
        auto* continue_memory = continue_memory_vec_[queue_index % continue_memory_vec_.size()];
        auto ret = continue_memory->LockAllocEntry(data_len, &entry_ptr, lock_);
        if(ret != ErrorCode::kSuccess)
        {
            return nullptr;
        }
        return entry_ptr->GetBuffer();
    }

    template <class T>
    T& AllocBuffer(uint8_t queue_index = 0)
    {
        void* alloc_buf = AllocBuffer(sizeof(T), queue_index);
        new (alloc_buf) T();
        return *(T*)(alloc_buf);
    }


    void PostBuffer(void* entry_data, uint8_t queue_index = 0)
    {
        
        auto* continue_memory = continue_memory_vec_[queue_index % continue_memory_vec_.size()];
        auto offset = ADK_OFFSET_OF(adk_impl::ContEntry, buffer);
        auto* entry_ptr = reinterpret_cast<adk_impl::ContEntry*>((char*)entry_data - offset);
        continue_memory->PostEntryThreadSafe(entry_ptr);
    }

    ErrorCode TryPutMsg(const void* msg, uint32_t msg_size, uint8_t queue_index = 0, std::string* err_msg = nullptr)
    {
        if (msg_size > MsgMaxContentSize())
        {
            SetErrMsg(err_msg, "TryPutMsg Failed: Msg too long.");
            return ErrorCode::kFailure;
        }
        adk_impl::ContEntry* entry_ptr = nullptr;
        auto* continue_memory = continue_memory_vec_[queue_index % continue_memory_vec_.size()];

        ErrorCode ret = static_cast<ErrorCode>(continue_memory->TryLockAllocEntry(msg_size, &entry_ptr, lock_));
        if (ret == ErrorCode::kWouldblock)
        {  
            // ContMem里面没有剩余空间了
            SetErrMsg(err_msg, "TryPutMsg Failed: Would block, no space for msg now.");
            return ret;
        }

        memcpy(entry_ptr->GetBuffer(), msg, msg_size);
        continue_memory->PostEntryThreadSafe(entry_ptr);
        return ErrorCode::kSuccess;
    }

    /**
     * @brief 批量消费 batch_num 条消息
     * @batch_num 一次消费的消息条数; 为0的时候表示一直消费至队列为空;
     */
    ErrorCode BatchConsumeMsg(uint32_t batch_num = 0)
    {
        bool consume_all = (batch_num == 0);
        ErrorCode ret = ErrorCode::kSuccess;
        while (true)
        {
            ret = TryConsumeOneMsg();
            
            if (ErrorCode::kSuccess != ret)
            {
                break;
            }
            
            if((!consume_all) && ((--batch_num) == 0))
            {
                break;
            }

        }
        return ret;
    }

    void CollectIndicator(boost::property_tree::ptree& indicator_ptree)
    {
        boost::property_tree::ptree& record_binary_indi_tree =
                        indicator_ptree.add_child("RecordMsgBinaryV2", boost::property_tree::ptree());
        for (uint32_t queue_index = 0; queue_index < continue_memory_vec_.size(); ++queue_index)
        {
            boost::property_tree::ptree& queue_indi_tree = record_binary_indi_tree.push_back(
                            boost::property_tree::ptree::value_type("", boost::property_tree::ptree()))->second;
            queue_indi_tree.put("index", std::to_string(queue_index));
            const auto* continue_memory = continue_memory_vec_[queue_index];
            continue_memory->CollectIndicator(queue_indi_tree);
        }
    }

private:
    // 从 ContinueMemory 取出 Msg 然后写盘;
    void BgThrdConsumeMsg()
    {
        char* env_str = std::getenv("ADK_RECODER_CPU_NODE");
        if (env_str != nullptr)
        {
            adk_impl::SetCpuAffinity(env_str);
        }

        ErrorCode ret = ErrorCode::kSuccess;
        while (start_flag_)
        {
            ret = TryConsumeOneMsg();
            if (ErrorCode::kWouldblock == ret)
            {  
                // Msg消费完了, sleep一段时间再试;
                std::this_thread::sleep_for(std::chrono::microseconds(consume_interval_));
            }
            else if (ErrorCode::kSuccess == ret)
            {
                continue;
            }
        }
    }

    ErrorCode TryConsumeOneMsg()
    {
        ErrorCode ret = ErrorCode::kSuccess; 
        auto queue_num = continue_memory_vec_.size();
        while(queue_num--)
        {
            auto* continue_memory = continue_memory_vec_[cursor_ % continue_memory_vec_.size()];
            ContEntry* entry_pointer = nullptr;
            ret = static_cast<ErrorCode>(continue_memory->TryWaitEntry(&entry_pointer));

            if (ret == ErrorCode::kSuccess)
            {  
                // 反序列化 -> 写到磁盘 -> 释放entry
                if(!serialize_msg_func_)
                {
                    return ErrorCode::kFailure;
                }

                const void* msg   = entry_pointer->GetBuffer();
                uint32_t msg_size = entry_pointer->app_data_len();
                std::string str   = serialize_msg_func_(msg, msg_size);
                if (record_file_)
                {
                    record_file_->Record(str);
                }
                continue_memory->FreeEntry(entry_pointer);
                ++cursor_;
                // 消费了一条消息, 退出循环返回
                break; 
            }
            else
            {
                ++cursor_;
                // 尝试消费下一个队列
                continue;
            }
        }

        return ret;
    }

private:
    //消费消息时如果队列为空等待 consume_interval_ 微秒再重试;
    uint32_t consume_interval_;            
    ///< parse the msg to string  
    SerializeMsgFunc serialize_msg_func_;  
    ///<写记录接口
    IRecorder* record_file_ {nullptr};       
    std::atomic_bool start_flag_ {false};
    std::thread bg_thread_;
    ///< 自旋锁保护入队
    LightWeightSpinLock lock_;  
    std::vector<adk_impl::ContinueMemory*> continue_memory_vec_;
    uint64_t cursor_ = 0;
};

}  //end namespace adk_impl

#endif
