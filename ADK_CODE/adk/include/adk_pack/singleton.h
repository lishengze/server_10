/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/
#ifndef ADK_SINGLETON_H_
#define ADK_SINGLETON_H_

#include <type_traits> //remove_reference
#include <map>
#include <mutex>
#include <tuple>

namespace adk
{

/**
 * @brief 单例模式实现类模板
 *
 * 本实现是对基础单例模式实现的扩展，支持使用不同的构造参数创建某个类的的不同单例。
 *
 * @tparam T 实现单例模式的基础类型
 */
template <typename T>
class Singleton
{
public:
    /**
     * @brief 获取无构造参数类的单例对象指针
     *
     * @return T类的单例对象指针
     */
    static inline T* GetInstance();
};

template <typename T>
T* Singleton<T>::GetInstance()
{
    static T t;
    return &t;
}

}

#endif /* AMI_SINGLETON_H_ */
