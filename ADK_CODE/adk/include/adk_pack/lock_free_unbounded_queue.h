/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_LOCK_FREE_UNBOUNDED_QUEUE_H_
#define ADK_LOCK_FREE_UNBOUNDED_QUEUE_H_

#include "arch/generic.h"
#include "error_code.h"

#include <stdint.h>

#include <string>
#include <functional>

namespace adk
{
 
template<typename QElementType>
class SPSCUnboundedQueue;

namespace impl
{

class SPSCUnboundedQueue
{
private:
    static SPSCUnboundedQueue* Create(const std::string& name, uint32_t cache_size, uint32_t node_size, uint32_t element_size);

    int32_t Push(const void* element);

    int32_t Pop(void* element);

    void* Head();

    int32_t Pop();

    void* ElementAt(uint64_t index);

    void ForeachElement(const std::function<bool(void*)>& callback);
    
private:
    void* queue_impl_;
    void* assign_;

    template<typename QElementType>
    friend class adk::SPSCUnboundedQueue;
};

}

template<typename QElementType>
class SPSCUnboundedQueue
{
public:
    static SPSCUnboundedQueue<QElementType>* Create(const std::string& name, uint32_t cache_size = 2, uint32_t node_size = 1024)
    {
        impl::SPSCUnboundedQueue* queue_impl = impl::SPSCUnboundedQueue::Create(name, cache_size, node_size, sizeof(QElementType));

        queue_impl->assign_ = (void*)impl::AssignFunction<QElementType>;
        return reinterpret_cast<SPSCUnboundedQueue<QElementType>*>(queue_impl);
    }

    int32_t Push(QElementType& push_element)
    {
        return reinterpret_cast<impl::SPSCUnboundedQueue*>(this)->Push(&push_element);
    }

    int32_t Pop(QElementType& pop_element)
    {
        return reinterpret_cast<impl::SPSCUnboundedQueue*>(this)->Pop(&pop_element);
    }

    QElementType* Head()
    {
        return (QElementType*)reinterpret_cast<impl::SPSCUnboundedQueue*>(this)->Head();
    }

    int32_t Pop()
    {
        return reinterpret_cast<impl::SPSCUnboundedQueue*>(this)->Pop();
    }

    QElementType* ElementAt(uint64_t index)
    {
        return (QElementType*)reinterpret_cast<impl::SPSCUnboundedQueue*>(this)->ElementAt(index);
    }

    /**
     * @brief   foreach element
     *
     * @param   callback
     *          callback(QElementType* element)
     *
     * @example 
     *          ForeachElement([&](QElementType* element) {
     *          });
     *  or
     *          std::function<bool(QElementType*)> callback;
     *          ForeachElement(callback);
     */
    template<typename CallbackType>
    void ForeachElement(const CallbackType& callback)
    {
        return reinterpret_cast<impl::SPSCUnboundedQueue*>(this)->ForeachElement([callback](void* element) {
            return callback(reinterpret_cast<QElementType*>(element));
        });
    }
};

} // adk

#endif // ADK_LOCK_FREE_UNBOUNDED_QUEUE_H_
