/** 
 *  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
 *  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
 *  For more information about Archforce, welcome to archforce.cn.
 *
 *  @file property.h
 *  @brief 属性容器, 应用可使用Property特例化属性
 **/

#pragma once

#include <string>
#include <vector>
#include <utility>
#include <istream> 
#include <stdexcept>
#include <boost/property_tree/ptree.hpp>

#include "pointer.h"

namespace adk
{

class PropertyContainer;

typedef std::vector< std::pair<std::string, std::string> > KVPairs;

/**
 * @brief 配置属性类
 *
 * 用于存储配置属性集，可存储不同类型的配置字段。
 */
class Property
{
public:
    /**
     * @brief 异常类型：无效的JSON字符串
     */
    struct InvalidJsonString : public std::runtime_error
    {
        /// 构造函数
        InvalidJsonString(const std::string& _str)
            : std::runtime_error("invalid json string : " + _str)
        {
        }
    };

    /**
     * @brief 异常类型：无效的key
     */
    struct InvalidKey : public std::runtime_error
    {
        /// 构造函数
        InvalidKey(const std::string& _key)
            : std::runtime_error("invalid key : " + _key)
        {
        }
    };

    /**
     * @brief 异常类型：无效的value类型
     */
    struct InvalidValue : public std::runtime_error
    {
        /// 构造函数
        InvalidValue(const std::string& _value)
            : std::runtime_error("invalid value : " + _value)
        {
        }
    };

    /**
     * @brief 构造函数
     */
    Property();

    /**
     * @brief 拷贝构造函数
     *
     * @param property 用于复制的配置属性
     */
    Property(const Property& property);

    /**
     * @brief 从JSON字符串构造配置属性
     *
     * @param json_str 以JSON字符串表示的配置属性
     *
     * @exception InvalidJsonStr
     */
    Property(const std::string& json_str);

    /**
     * @brief 从输入流造配置属性
     *
     * @param istream JSON格式的输入流
     *
     * @exception InvalidJsonStr
     */
    Property(std::basic_istream<char> &istream);

    /**
     * @brief 析构函数
     */
    ~Property();

    /**
     * @brief 赋值操作符
     *
     * @param property 用于复制的配置属性
     *
     * @return 对自身的引用
     */
    Property& operator=(const Property& property);

    template<typename ValueType>
    Property& operator()(const std::string& key, const ValueType& value)
    {
        SetValue(key, value);
        return *this;
    }

    /**
     * @brief 设置属性值
     *
     * @param key 属性名称
     * @param value 属性值
     */
    void SetValue(const std::string& key, const std::string& value);

    /**
     * @copydoc Propety::SetValue(const std::string&, const char*)
     */
    void SetValue(const std::string& key, const char* value);

    /**
     * @copydoc Propety::SetValue(const std::string&, short)
     */
    void SetValue(const std::string& key, short value);
    
    /**
     * @copydoc Propety::SetValue(const std::string&, unsigned short)
     */
    void SetValue(const std::string& key, unsigned short value);
    
    /**
     * @copydoc Propety::SetValue(const std::string&, int)
     */
    void SetValue(const std::string& key, int value);
    
    /**
     * @copydoc Propety::SetValue(const std::string&, unsigned int)
     */
    void SetValue(const std::string& key, unsigned int value);

    /**
     * @copydoc Propety::SetValue(const std::string&, long)
     */
    void SetValue(const std::string& key, long value);

    /**
     * @copydoc Propety::SetValue(const std::string&, unsigned long)
     */
    void SetValue(const std::string& key, unsigned long value);

    /**
     * @copydoc Propety::SetValue(const std::string&, long long)
     */
    void SetValue(const std::string& key, long long value);

    /**
     * @copydoc Propety::SetValue(const std::string&, unsigned long long)
     */
    void SetValue(const std::string& key, unsigned long long value);
    
    /**
     * @copydoc Propety::SetValue(const std::string&, bool)
     */
    void SetValue(const std::string& key, bool value);

    /**
     * @copydoc Propety::SetValue(const std::string&, float)
     */
    void SetValue(const std::string& key, float value);

    /**
     * @copydoc Propety::SetValue(const std::string&, double)
     */
    void SetValue(const std::string& key, double value);

    /**
     * @copydoc Propety::SetValue(const std::string&, long double)
     */
    void SetValue(const std::string& key, long double value);
    
    /**
     * @copydoc Propety::SetValue(const std::string&, const Property&)
     */
    void SetValue(const std::string& key, const Property& value);

    /**
     * @copydoc Propety::SetValue(const std::string&, const Pointer&)
     */
    void SetValue(const std::string& key, const Pointer& value);

    /**
     * @copydoc Propety::SetValue(const std::string&, void*)
     */
    void SetValue(const std::string& key, void* pointer);

    /**
     * @copydoc Propety::SetValue(const std::string&, std::nullptr_t)
     */
    void SetValue(const std::string& key, std::nullptr_t pointer);

    /**
     * @copydoc Propety::SetValue(const std::string&, const std::vector<std::string>&)
     */
    void SetValue(const std::string& key, const std::vector<std::string>& value);

    /**
     * @copydoc Propety::SetValue(const std::string&, const std::vector<int>&)
     */
    void SetValue(const std::string& key, const std::vector<int>& value);

    /**
     * @copydoc Propety::SetValue(const std::string&, const std::vector<bool>&)
     */
    void SetValue(const std::string& key, const std::vector<bool>& value);

    /**
     * @copydoc Propety::SetValue(const std::string&, const std::vector<Property>&)
     */
    void SetValue(const std::string& key, const std::vector<Property>& value);

    /**
     * @copydoc Propety::SetValue(const std::string&, const std::vector<adk::Pointer>&)
     */
    void SetValue(const std::string& key, const std::vector<adk::Pointer>& value);

    /**
     * @brief 获取属性值
     *
     * @param key 属性名称
     *
     * @return 属性值
     *
     * @exception InvalidKey
     */
    std::string GetStringValue(const std::string& key) const;

    /**
     * @brief 获取属性值
     *
     * @param key 属性名称
     *
     * @return 属性值
     *
     * @exception InvalidKey
     * @exception InvalidValue
     */
    int GetIntValue(const std::string& key) const;

    /**
     * @copydoc Propety::GetIntValue(const std::string&)
     */
    bool GetBoolValue(const std::string& key) const;

    /**
     * @copydoc Propety::GetIntValue(const std::string&)
     */
    Property GetPropertyValue(const std::string& key) const;

    /**
     * @copydoc Propety::GetIntValue(const std::string&)
     */
    adk::Pointer GetPointerValue(const std::string& key) const;

    /**
     * @copydoc Propety::GetIntValue(const std::string&)
     */
    std::vector<std::string> GetStringVectorValue(const std::string& key) const;

    /**
     * @copydoc Propety::GetIntValue(const std::string&)
     */
    std::vector<int> GetIntVectorValue(const std::string& key) const;

    /**
     * @copydoc Propety::GetIntValue(const std::string&)
     */
    std::vector<bool> GetBoolVectorValue(const std::string& key) const;

    /**
     * @copydoc Propety::GetIntValue(const std::string&)
     */
    std::vector<Property> GetPropertyVectorValue(const std::string& key) const;

    /**
     * @copydoc Propety::GetIntValue(const std::string&)
     */
    std::vector<adk::Pointer> GetPointerVectorValue(const std::string& key) const;

    /**
     * @brief 获取属性值，如属性不存在则返回默认值
     *
     * @param key 属性名称
     * @param default_value 默认值
     *
     * @return 属性值
     */
    std::string GetValue(const std::string& key, const std::string& default_value) const;

    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    std::string GetValue(const std::string& key, const char* default_value) const;

    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    short GetValue(const std::string& key, short default_value) const;

    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    unsigned short GetValue(const std::string& key, unsigned short default_value) const;
    
    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    int GetValue(const std::string& key, int default_value) const;

    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    unsigned int GetValue(const std::string& key, unsigned int default_value) const;

    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    long GetValue(const std::string& key, long default_value) const;

    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    unsigned long GetValue(const std::string& key, unsigned long default_value) const;

    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    long long GetValue(const std::string& key, long long default_value) const;

    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    unsigned long long GetValue(const std::string& key, unsigned long long default_value) const;
    
    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    bool GetValue(const std::string& key, bool default_value) const;

    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    float GetValue(const std::string& key, float default_value) const;

    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    double GetValue(const std::string& key, double default_value) const;

    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    long double GetValue(const std::string& key, long double default_value) const;
    
    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    adk::Pointer GetValue(const std::string& key, const adk::Pointer& default_value) const;

    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    Property GetValue(const std::string& key, const Property& default_value) const;

    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    std::vector<std::string> GetValue(
            const std::string& key, 
            const std::vector<std::string>& default_value) const;

    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    std::vector<int> GetValue(
            const std::string& key, 
            const std::vector<int>& default_value) const;

    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    std::vector<adk::Pointer> GetValue(
            const std::string& key, 
            const std::vector<adk::Pointer>& default_value) const;

    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    std::vector<bool> GetValue(
            const std::string& key, 
            const std::vector<bool>& default_value) const;

    /**
     * @copydoc Propety::GetValue(const std::string&, const std::string&)
     */
    std::vector<Property> GetValue(
            const std::string& key, 
            const std::vector<Property>& default_value) const;

    /**
     * @brief 是否存在指定属性值
     *
     * @param key 属性名称
     *
     * @return 是否存在指定属性值
     */
    bool HasValue(const std::string& key) const;

    /**
     * @brief 删除指定属性值
     *
     * @param key 属性名称
     *
     * @return 删除成功返回true，不存在指定属性值返回false
     */
    bool DeleteValue(const std::string& key);

    /**
     * @brief 获取key/value对序列
     *
     * @return Key/value对序列
     */
    KVPairs GetKVPairs() const;

    /**
     * @brief 输出为JSON字符串
     *
     * @param pretty 是否按易读格式输出，true将在字符串中增加换行及必要的空格
     *
     * @return JSON字符串
     */
    std::string Dump(bool pretty = false) const;

    /**
     * @brief 清空所有属性值
     */
    void Clear();

    /**
     * @brief 属性值设置器
     */
    class ValueSetter
    {
    public:
        /**
         * @brief 构造函数
         *
         * @param property 与其关联的属性对象
         */
        explicit ValueSetter(Property& property)
            : property_(property)
        {
        }

        /**
         * @brief 设置属性值
         *
         * @tparam T 属性值类型
         * @param key 属性名称
         * @param value 属性值
         *
         * @return 自身引用
         */
        template <typename T>
        ValueSetter& operator()(const std::string& key, const T& value)
        {
            property_.SetValue(key, value);
            return *this;
        }
    private:
        Property& property_;
    };

    /**
     * @brief 连续设置属性值
     *
     * 使用示例：property.SetValues()("a", 123)("b", true)("c", "ABC");
     *
     * @return 属性值设置器
     */
    ValueSetter& SetValues()
    {
        return value_setter_;
    }

    /**
     * @brief      将该属性容器中的属性覆盖到props参数指向的容器中
     *
     * @param      props  被覆盖的容器对象
     */
    void OverWriteTo(Property& props) const;

    /**
     * @brief      使用props参数指向的容器中的属性覆盖该属性容器
     *
     * @param[in]  props  指向属性容器，用于覆盖该属性容器
     */
    void OverWriteFrom(const Property& props);

    static boost::property_tree::ptree* GetPtree(const Property& property);

    boost::property_tree::ptree* GetSelfPtree();

    static void SetPtree(Property* property,
                         const boost::property_tree::ptree& pt);

private:
    PropertyContainer* container_;
    ValueSetter        value_setter_;

    friend class PropertyContainer;
};

}

