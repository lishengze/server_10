/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_OBJECT_POOL_H_
#define ADK_OBJECT_POOL_H_

#include "error_code.h"

#include <string>
#include <functional>

namespace adk
{

using std::string;

/**
 * @brief      对象池接口
 */
class IPool
{
public:
    IPool();
    ~IPool();

    /**
     * @brief      删除对象所对应的内存块
     *
     * @param      mem_buf  内存块地址
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    int32_t Delete(void* mem_buf);
};

/**
 * @brief      对象池对象接口
 */
class IObject
{
public:
    IObject() {}
    virtual ~IObject() {}

    /**
     * @brief      删除对象
     *
     * @return     成功时返回ErrorCode::kSuccess
     */
    int32_t Delete();

    /**
     * @brief      重置对象
     */
    virtual void Reset() {};

private:
    IPool* obj_pool_;
};

namespace impl
{

class ObjectPool
{
public:
    static ObjectPool* Create(const string& pool_name, 
                              uint32_t size, 
                              uint32_t element_size, 
                              void* constructor);

    void* NewBuffer();

    void* NewBufferEx(bool is_force_new, uint32_t element_size);

private:
    void* object_pool_impl_;
};

template<typename ObjectType>
static inline void DoConstruct(void* object_buffer)
{
    new (object_buffer) ObjectType();
}

}

/**
 * @brief      对象池
 *
 * @tparam     ElementType  对象池中缓存的对象类型
 */
template<typename ElementType>
class ObjectPool : public IPool
{
public:
    static_assert(std::is_base_of<IObject, ElementType>::value,
                  "ElementType must be derived class of IObject");
    ObjectPool() = default;
    ~ObjectPool() = default;

    /**
     * @brief      创建对象池
     *
     * @param[in]  pool_name  对象池名称
     * @param[in]  size       对象池容量
     *
     * @return     成功时返回对象池引用，失败时返回NULL
     */
    static ObjectPool<ElementType>* Create(const string& pool_name, uint32_t size)
    {
        return (ObjectPool<ElementType>*)(impl::ObjectPool::Create(pool_name, 
                                                                   size, 
                                                                   sizeof(ElementType), 
                                                                   (void*)adk::impl::DoConstruct<ElementType>));
    }

    /**
     * @brief      从对象池申请一个对象
     *
     * @return     成功时返回对象引用，失败时返回NULL
     */
    ElementType* NewObject()
    {
        return (ElementType*)reinterpret_cast<impl::ObjectPool*>(this)->NewBuffer();
    }

    ElementType* NewObjectEx(bool is_force_new = true)
    {
        return (ElementType*)reinterpret_cast<impl::ObjectPool*>(this)->NewBufferEx(is_force_new, sizeof(ElementType));
    }
};

} // adk

#endif // ADK_OBJECT_POOL_H_
