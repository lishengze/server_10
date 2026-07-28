/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_LOCK_FREE_UNBOUNDED_QUEUE_VARIANT_H_
#define ADK_LOCK_FREE_UNBOUNDED_QUEUE_VARIANT_H_

#include "arch/generic.h"
#include "error_code.h"

#include <stdint.h>

#include <string>
#include <functional>

namespace adk
{

namespace variant
{

using std::string;

class MPSCUnboundedQueueImpl
{
private:
    static MPSCUnboundedQueueImpl* Create(const string& name, uint32_t node_size, uint32_t element_size);

    static void Delete(MPSCUnboundedQueueImpl* queue_impl);

    int32_t Push(const void* element);

    int32_t Pop(void* element);

    uint64_t length();

    void* Alloc();

    int32_t Post(const void* element);

    void* Top();

    int32_t Commit(const void* element);

    void* queue_cache_;
    void* assign_;

    template<typename QElementType>
    friend class MPSCUnboundedQueue;
};

/*QElementType only support pod structure*/
template<typename QElementType>
class MPSCUnboundedQueue
{
public:
    static MPSCUnboundedQueue<QElementType>* Create(const string& name, uint32_t node_size = 1024)
    {
        MPSCUnboundedQueueImpl* queue_impl = MPSCUnboundedQueueImpl::Create(name, node_size, sizeof(QElementType));
        if (nullptr != queue_impl)
        {
            queue_impl->assign_ = (void*)impl::AssignFunction<QElementType>;
        }
        return (MPSCUnboundedQueue<QElementType>*)queue_impl;
    }

    static void Delete(MPSCUnboundedQueue<QElementType>* unbouneded_queue)
    {
        MPSCUnboundedQueueImpl::Delete((MPSCUnboundedQueueImpl*)unbouneded_queue);
    }

    int32_t Push(const QElementType& element)
    {
        return ((MPSCUnboundedQueueImpl*)this)->Push(&element);
    }

    int32_t Pop(QElementType& element)
    {
        return ((MPSCUnboundedQueueImpl*)this)->Pop(&element);
    }
    
    inline uint64_t length()
    {
        return ((MPSCUnboundedQueueImpl*)this)->length();
    }

    QElementType* Alloc()
    {
        return (QElementType*)((MPSCUnboundedQueueImpl*)this)->Alloc();
    }

    int32_t Post(QElementType* element)
    {
        return ((MPSCUnboundedQueueImpl*)this)->Post(element);
    }

    QElementType* Top()
    {
        return (QElementType*)((MPSCUnboundedQueueImpl*)this)->Top();
    }

    int32_t Commit(QElementType* element)
    {
        return ((MPSCUnboundedQueueImpl*)this)->Commit(element);
    }
};

}
}

#endif