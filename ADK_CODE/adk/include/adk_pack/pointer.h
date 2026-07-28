/** 
 *  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
 *  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
 *  For more information about Archforce, welcome to archforce.cn.
 **/

#pragma once

namespace adk
{

/**
 * @brief 无类型指针类型
 */
struct Pointer
{
    /**
     * @brief 转换为任意类型指针
     *
     * @tparam T 目标类型
     *
     * @return 目标类型的指针
     */
    template<typename T>
    T& as()
    {
        return *reinterpret_cast<T*>(ptr);
    }

    /**
     * @brief 转换为任意类型指针
     *
     * @tparam T 目标类型
     *
     * @return 目标类型的指针
     */
    template<typename T>
    T as_ptr()
    {
        return reinterpret_cast<T>(ptr);
    }

    /**
     * @brief 将指针转换为整形值
     *
     * @return 整形值
     */
    unsigned long get_value() const
    {
        return reinterpret_cast<unsigned long>(ptr);
    }

    /**
     * @brief 构造函数
     */
    Pointer()
        :   ptr(NULL)
    {}

    /**
     * @brief 使用整形值构造指针
     *
     * @param value 整形值
     */
    Pointer(unsigned long value)
    {
        ptr = reinterpret_cast<void*>(value);
    }

    /**
     * @brief 赋值操作符
     *
     * @param value 用于构造指针的整形值
     *
     * @return 自身引用
     */
    Pointer& operator=(unsigned long value)
    {
        ptr = reinterpret_cast<void*>(value);
        return *this;
    }

    void* ptr;      ///< 实际指针
};

template<typename T>
inline adk::Pointer MakePointer(T* ptr)
{
    return adk::Pointer(reinterpret_cast<unsigned long>(ptr));
}

} // adk

