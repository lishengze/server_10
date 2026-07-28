/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_LOCK_FREE_QUEUE_VARIANT_H_
#define ADK_LOCK_FREE_QUEUE_VARIANT_H_

#include "arch/generic.h"
#include "error_code.h"
#include <string>

namespace adk
{

namespace variant
{

class SPSCQueueImpl
{
public:
    static SPSCQueueImpl* Create(const std::string &name, uint32_t queue_size, uint32_t element_size);

    static int32_t Delete(SPSCQueueImpl* queue);

    int32_t Push(const void* payload);

    int32_t Pop(void* payload);

    int32_t TryPush(const void* payload);

    int32_t TryPop(void* payload);

    uint32_t queue_size() const;

    uint64_t length() const;

private:
    void* assign_;
    void* queue_impl_;

    template<typename QElementType>
    friend class SPSCQueue;
};

class MPMCQueueImpl
{
public:
    static MPMCQueueImpl* Create(const std::string &name, uint32_t queue_size, uint32_t element_size);

    static int32_t Delete(MPMCQueueImpl* queue);

    int32_t Push(const void* payload);

    int32_t Pop(void* payload);

    int32_t TryPush(const void* payload);

    int32_t TryPop(void* payload);

private:
    void* assign_;
    void* queue_impl_;

    template<typename QElementType>
    friend class MPMCQueue;
};

class SPMCQueueImpl
{
public:
    static SPMCQueueImpl* Create(const std::string &name, uint32_t queue_size, uint32_t element_size);

    static int32_t Delete(SPMCQueueImpl* queue);

    int32_t Push(const void* payload);

    int32_t Pop(void* payload);

    int32_t TryPush(const void* payload);

    int32_t TryPop(void* payload);

private:
    void* assign_;
    void* queue_impl_;

    template<typename QElementType>
    friend class SPMCQueue;
};

class MPSCQueueImpl
{
public:
    static MPSCQueueImpl* Create(const std::string &name, uint32_t queue_size, uint32_t element_size);

    static int32_t Delete(MPSCQueueImpl* queue);

    int32_t Push(const void* payload);

    int32_t Pop(void* payload);

    int32_t TryPush(const void* payload);

    int32_t TryPop(void* payload);

private:
    void* assign_;
    void* queue_impl_;

    template<typename QElementType>
    friend class MPSCQueue;
};

template<typename QElementType>
class SPSCQueue
{
public:
    using impl_type = SPSCQueueImpl;
    using this_type = SPSCQueue<QElementType>;
    static this_type* Create(const std::string &name, uint32_t queue_size)
    {
        impl_type* const queue = impl_type::Create(name, queue_size, sizeof(QElementType));
        if (nullptr != queue)
        {
            queue->assign_ = (void*)impl::AssignFunction<QElementType>;
        }
        return (this_type*)(queue);
    }

    static int32_t Delete(this_type* queue)
    {
        return impl_type::Delete((impl_type*)queue);
    }

    inline int32_t Push(const QElementType& payload)
    {
        return ((impl_type*)this)->Push(&payload);
    }

    inline int32_t Pop(QElementType& payload)
    {
        return ((impl_type*)this)->Pop(&payload);
    }

    inline int32_t TryPush(const QElementType& payload)
    {
        return ((impl_type*)this)->TryPush(&payload);
    }

    inline int32_t TryPop(QElementType& payload)
    {
        return ((impl_type*)this)->TryPop(&payload);
    }

    inline uint32_t queue_size() const
    {
        return ((impl_type*)this)->queue_size();
    }

    inline uint64_t length() const
    {
        return ((impl_type*)this)->length();
    }
};

template<typename QElementType>
class MPMCQueue
{
public:
    using impl_type = MPMCQueueImpl;
    using this_type = MPMCQueue<QElementType>;

    static this_type* Create(const std::string &name, uint32_t queue_size)
    {
        impl_type* const queue = impl_type::Create(name, queue_size, sizeof(QElementType));
        if (nullptr != queue)
        {
            queue->assign_ = (void*)impl::AssignFunction<QElementType>;
        }
        return (this_type*)(queue);
    }

    static int32_t Delete(this_type* queue)
    {
        return impl_type::Delete((impl_type*)queue);
    }

    inline int32_t Push(const QElementType& payload)
    {
        return ((impl_type*)this)->Push(&payload);
    }

    inline int32_t Pop(QElementType& payload)
    {
        return ((impl_type*)this)->Pop(&payload);
    }

    inline int32_t TryPush(const QElementType& payload)
    {
        return ((impl_type*)this)->TryPush(&payload);
    }

    inline int32_t TryPop(QElementType& payload)
    {
        return ((impl_type*)this)->TryPop(&payload);
    }
};

template<typename QElementType>
class SPMCQueue
{
public:
    using impl_type = SPMCQueueImpl;
    using this_type = SPMCQueue<QElementType>;

    static this_type* Create(const std::string &name, uint32_t queue_size)
    {
        impl_type* const queue = impl_type::Create(name, queue_size, sizeof(QElementType));
        if (nullptr != queue)
        {
            queue->assign_ = (void*)impl::AssignFunction<QElementType>;
        }
        return (this_type*)(queue);
    }

    static int32_t Delete(this_type* queue)
    {
        return impl_type::Delete((impl_type*)queue);
    }

    inline int32_t Push(const QElementType& payload)
    {
        return ((impl_type*)this)->Push(&payload);
    }

    inline int32_t Pop(QElementType& payload)
    {
        return ((impl_type*)this)->Pop(&payload);
    }

    inline int32_t TryPush(const QElementType& payload)
    {
        return ((impl_type*)this)->TryPush(&payload);
    }

    inline int32_t TryPop(QElementType& payload)
    {
        return ((impl_type*)this)->TryPop(&payload);
    }
};

template<typename QElementType>
class MPSCQueue
{
public:
    using impl_type = MPSCQueueImpl;
    using this_type = MPSCQueue<QElementType>;

    static this_type* Create(const std::string &name, uint32_t queue_size)
    {
        impl_type* const queue = impl_type::Create(name, queue_size, sizeof(QElementType));
        if (nullptr != queue)
        {
            queue->assign_ = (void*)impl::AssignFunction<QElementType>;
        }
        return (this_type*)(queue);
    }

    static int32_t Delete(this_type* queue)
    {
        return impl_type::Delete((impl_type*)queue);
    }

    inline int32_t Push(const QElementType& payload)
    {
        return ((impl_type*)this)->Push(&payload);
    }

    inline int32_t Pop(QElementType& payload)
    {
        return ((impl_type*)this)->Pop(&payload);
    }

    inline int32_t TryPush(const QElementType& payload)
    {
        return ((impl_type*)this)->TryPush(&payload);
    }

    inline int32_t TryPop(QElementType& payload)
    {
        return ((impl_type*)this)->TryPop(&payload);
    }
};

}

}

#endif