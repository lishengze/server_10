/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*  @file 
*  @brief 对象池，加速大对象的分配和释放
**/

#ifndef ADK_IMPL_OBJ_POOL_VARIANT_H_
#define ADK_IMPL_OBJ_POOL_VARIANT_H_

#include <functional>
#include <stdint.h>
#include <adk/error_code.h>

namespace adk_impl
{

namespace variant
{
int32_t DoGetTimeId();

template<typename T>
int32_t GetTypeId()
{
    // using inside the same share object file or exe file
    static int32_t type_id_cache = DoGetTimeId();
    return type_id_cache;
}

typedef void (*ObjConstructorType)(char* buffer);

struct ObjPoolBase
{
    int32_t Init(ObjConstructorType f,
                 std::size_t size, int32_t id);

    void* New(int32_t id);

    static void Delete(void* obj, int32_t id);

    ObjConstructorType         f_;
    std::size_t                size_;
    void*                      obj_pool_private_;

    static void GenObjMain();
};

template<typename T>
void OjbConstructor(char* buffer)
{
    new (buffer) T();
}

/**
 * @brief      本对象池可管理4096种对象，每种对象的管理由同一ObjPool管理
 *             
 * @tparam     ObjType  ObjPool管理的对象类型
 */
template<typename ObjType>
class ObjPool : public ObjPoolBase
{
public:
    /**
     * @brief      创建并初始化对象池
     *
     * @return     对象池为单例模式，即对于同一对象返回同一对象池，失败时返回nullptr
     */
    static ObjPool<ObjType>* Create()
    {
        static ObjPool<ObjType>* obj_pool = []()-> ObjPool<ObjType>*
        {
            auto* ret = new ObjPool<ObjType>();   
            if (ret->Init() != ErrorCode::kSuccess)
            {
                return nullptr;
            }
            return ret;
        }();
        
        return obj_pool;
    }

    /**
     * @brief      从对象池分配一个新对象
     *
     * @return     成功时返回对象指针
     */
    ObjType* New()
    {
        return (ObjType*)ObjPoolBase::New(id_);
    }

    /**
     * @brief      将对象释放
     *
     * @param      obj   待释放的对象
     */
    void Delete(ObjType* obj)
    {
        ObjPoolBase::Delete(obj, id_);
    }

    /**
     * @brief      将对象释放
     *
     * @param      obj   待释放的对象
     */
    static void UniformDelete(ObjType* obj)
    {
        ObjPoolBase::Delete(obj, GetTypeId<ObjType>());
    }

private:
    int32_t id_;

    int32_t Init()
    {
        id_ = GetTypeId<ObjType>();
        return ObjPoolBase::Init((ObjConstructorType)&OjbConstructor<ObjType>,
                                 sizeof(ObjType), id_);
    }
};

/**
 * @brief      释放对象
 *
 * @param      obj   待释放对象指针
 *
 * @return     成功时返回ErrorCode::kSuccess
 * 
 */
int32_t Delete(void* obj);

}
} // adk

#endif // ADK_OBJ_POOL_VARIANT_H_
