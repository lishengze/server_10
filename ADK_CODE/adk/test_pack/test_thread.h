#ifndef ADK_THREAD_H_
#define ADK_THREAD_H_

#include <adk_pack/thread.h>
#include <adk_pack/arch/generic.h>
#include <adk_pack/error_code.h>
#include <boost/thread/thread.hpp>
#include <adk_pack/lock_free_msg_queue.h>

#include <map>
#include <string>
#include <vector>

#include <adk_pack/object_pool.h>

namespace adk
{

#ifndef ADK_MAX_THREAD_CLASS
#define ADK_MAX_THREAD_CLASS 64
#endif // ADK_MAX_THREAD_CLASS

#ifndef ADK_MAX_THREAD_INSTANCE
#define ADK_MAX_THREAD_INSTANCE 16
#endif  // ADK_MAX_THREAD_INSTANCE

#ifndef ADK_MAX_THREAD_MESSAGES
#define ADK_MAX_THREAD_MESSAGES 1024
#endif  // ADK_MAX_THREAD_MESSAGES

#ifndef ADK_THREAD_MESSAGES_BUDGET
#define ADK_THREAD_MESSAGES_BUDGET 32
#endif  // ADK_THREAD_MESSAGES_BUDGET

#ifndef ADK_MAX_THREAD_OOB_MESSAGES
#define ADK_MAX_THREAD_OOB_MESSAGES 128
#endif  // ADK_MAX_THREAD_OOB_MESSAGES

#ifndef ADK_THREAD_OOB_MESSAGES_BUDGET
#define ADK_THREAD_OOB_MESSAGES_BUDGET 2
#endif  // ADK_THREAD_OOB_MESSAGES_BUDGET

#ifndef ADK_THREAD_MESSAGES_POOL_SIZE
#define ADK_THREAD_MESSAGES_POOL_SIZE 8192
#endif  // ADK_THREAD_MESSAGES_POOL_SIZE

#define ADK_MESSAGE_BASE_TYPE   0

#define ADK_INVALID_THREAD_INSTANCE_ID  -1

extern int32_t AllocMessageType(bool);

template<typename AppMessageType>
class ThreadMessage;

template<typename AppMessageType>
class ThreadMessageBase : public IObject
{
public:
    int32_t message_type() { return message_type_; }
    const char* message_name() { return message_name_; }

    static ThreadMessage<AppMessageType> New()
    {       
        static ObjectPool<AppMessageType>* s_obj_pool = 
                    ObjectPool<AppMessageType>::Create(AppMessageType::TypeName(),
                                                       ADK_THREAD_MESSAGES_POOL_SIZE);

        s_obj_pool2 = s_obj_pool;
        return ThreadMessage<AppMessageType>(s_obj_pool->NewObjectEx());
    }

    static ThreadMessage<AppMessageType> NewUnsafe()
    {
        assert(s_obj_pool2);       
        return ThreadMessage<AppMessageType>(s_obj_pool2->NewObjectEx());
    }

protected:
    ThreadMessageBase() 
    {
        message_type_ = ADK_MESSAGE_BASE_TYPE;
        ref_counter_ = 0;
    }

    const char* message_name_;
    int32_t message_type_;
    int32_t ref_counter_;

    static ObjectPool<AppMessageType>* s_obj_pool2;

    template<typename T>
    friend class ThreadMessage;

    template<typename T>
    friend class ObjectPool;
};

template<typename AppMessageType>
ObjectPool<AppMessageType>* ThreadMessageBase<AppMessageType>::s_obj_pool2;

template<typename AppMessageType>
class ThreadMessage
{
public:
    ThreadMessage(ThreadMessageBase<AppMessageType>* mbase)
        :   message_base_(mbase)
    {
        message_base_->ref_counter_ = 1;
    }

    ~ThreadMessage()
    {
        if (message_base_->ref_counter_ == 1)
        {
            // not necessary
            // message_base_->ref_counter_ = 0
            message_base_->Delete();
        }
        else
        {
            if (atomic_dec(message_base_->ref_counter_) == 1)
            {
                message_base_->Delete();
            }
        }
    }

    AppMessageType& operator*()
    {
        return *((AppMessageType*)message_base_);
    }

    AppMessageType* operator->()
    {
        return (AppMessageType*)message_base_;
    }

    ThreadMessage& operator=(ThreadMessage& other)
    {
        message_base_ = other.message_base_;
        atomic_inc(other.message_base_->ref_counter_);
        return *this;
    }

private:
    ThreadMessageBase<AppMessageType>* message_base_;

    ThreadMessageBase<AppMessageType>* message_base() { return message_base_; }
};


#define _ADK_DEFINE_THREAD_MESSAGE(AppThreadMessage, oob)     \
    template<typename T>    \
    class AppThreadMessage##Basic : public adk::ThreadMessageBase<T>  \
    {   \
    public: \
        AppThreadMessage##Basic()   \
        {   \
            static_assert(std::is_trivially_destructible<T>::value, \
                          "non-trivial destructor is not support");    \
            static int32_t s_message_type =     \
                    adk::AllocMessageType(oob); \
            adk::ThreadMessageBase<T>::message_type_ = s_message_type; \
            adk::ThreadMessageBase<T>::message_name_ = #AppThreadMessage;  \
        }   \
        static int32_t TypeId()     \
        {   \
            AppThreadMessage##Basic obj;    \
            return obj.message_type();   \
        }   \
        static const char* TypeName()   \
        {   \
            AppThreadMessage##Basic obj;    \
            return obj.message_name();   \
        }   \
    };  \
    class AppThreadMessage final: public AppThreadMessage##Basic<AppThreadMessage>

#define ADK_OOB_THREAD_MESSAGE(AppThreadMessage)  \
            _ADK_DEFINE_THREAD_MESSAGE(AppThreadMessage, true)
#define ADK_THREAD_MESSAGE(AppThreadMessage) \
            _ADK_DEFINE_THREAD_MESSAGE(AppThreadMessage, false)

}

#endif
