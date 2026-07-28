/**
 * @file property_container.h
 * @brief 配置属性容器类
 * @author Li Yunchong
 * @version 0.1
 * @date 2016-11-28
 */
#ifndef ADK_IMPL_IMPL_PROPERTY_CONTAINER_H_
#define AKD_IMPL_PROPERTY_CONTAINER_H_

#include <string>
#include <utility>
#include <vector>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <adk_pack/property.h>

namespace adk
{

class PropertyContainer
{
public:
    PropertyContainer()
    {
    }

    PropertyContainer(const PropertyContainer& container)
        : ptree_(container.ptree_)
    {
    }

    PropertyContainer(const std::string& json_str)
    {
        if (json_str.empty())
        {
            return;
        }
        std::istringstream iss;
        iss.str(json_str);
        try
        {
            boost::property_tree::json_parser::read_json(iss, ptree_);
        }
        catch (boost::property_tree::json_parser::json_parser_error& err)
        {
            throw Property::InvalidJsonString(json_str);
        }
    }

    template<typename CharType>
    PropertyContainer(std::basic_istream<CharType> &istream)
    {
        auto pos = istream.tellg();
        try
        {
            boost::property_tree::json_parser::read_json(istream, ptree_);
        }
        catch (boost::property_tree::json_parser::json_parser_error& err)
        {
            istream.seekg(pos);
            throw Property::InvalidJsonString(std::string(std::istream_iterator<CharType>(istream), std::istream_iterator<CharType>()));
        }
    }

    inline static boost::property_tree::ptree* GetPtree(const Property& property);

    inline boost::property_tree::ptree* GetSelfPtree();

    inline static void SetPtree(Property* property,
                                const boost::property_tree::ptree& pt);

    inline static PropertyContainer* GetContainer(const Property& property);

    /**
     * @brief 设置属性值
     *
     * @param key 属性名称
     * @param value 属性值
     */
    template <typename T>
    inline void SetValue(const std::string& key, const T& value);

    inline void SetValue(const std::string& key, const Property& value);

    inline void SetValue(const std::string& key, const adk::Pointer& value);

    /**
     * @brief 设置属性值
     *
     * @param key 属性名称
     * @param value 属性值
     */
    template <typename T>
    inline void SetValue(const std::string& key, const std::vector<T>& value);

    inline void SetValue(const std::string& key, const std::vector<Property>& value);

    inline void SetValue(const std::string& key, const std::vector<adk::Pointer>& value);

    /**
     * @brief 获取属性值
     *
     * @param key 属性名称
     *
     * @return 属性值
     */
    template <typename T>
    inline T GetValue(const std::string& key) const;

    inline Property GetPropertyValue(const std::string& key) const;

    inline adk::Pointer GetPointerValue(const std::string& key) const;

    /**
     * @brief 获取属性值
     *
     * @param key 属性名称
     *
     * @return 属性值
     */
    template <typename T>
    inline std::vector<T> GetVectorValue(const std::string& key) const;

    inline std::vector<Property> GetPropertyVectorValue(const std::string& key) const;

    inline std::vector<adk::Pointer> GetPointerVectorValue(const std::string& key) const;

    /**
     * @brief 获取属性值，如属性不存在则返回默认值
     *
     * @param key 属性名称
     * @param default_value 默认值
     *
     * @return 属性值
     */
    template <typename T>
    inline T GetValue(const std::string& key, const T& default_value) const;

    inline Property GetValue(
            const std::string& key,
            const Property& default_value) const;

    inline adk::Pointer GetValue(
            const std::string& key,
            const adk::Pointer& default_value) const;

    /**
     * @brief 获取属性值，如属性不存在则返回默认值
     *
     * @param key 属性名称
     * @param default_value 默认值
     *
     * @return 属性值
     */
    template <typename T>
    inline std::vector<T> GetValue(
            const std::string& key,
            const std::vector<T>& default_value) const;

    inline std::vector<Property> GetValue(
            const std::string& key,
            const std::vector<Property>& default_value) const;

    inline std::vector<adk::Pointer> GetValue(
            const std::string& key,
            const std::vector<adk::Pointer>& default_value) const;

    /**
     * @brief 是否存在指定属性值
     *
     * @param key 属性名称
     *
     * @return 是否存在指定属性值
     */
    inline bool HasValue(const std::string& key) const;

    inline bool DeleteValue(const std::string& key);

    /**
     * @brief 获取key/value对序列
     *
     * @return Key/value对序列
     */
    inline KVPairs GetKVPairs() const;

    inline std::string Dump(bool pretty) const;

    inline void Clear();

    inline void Override(const PropertyContainer &reference);

    inline void OverWriteFrom(const PropertyContainer &reference);

    inline void OverWriteTo(PropertyContainer &reference) const;

    inline boost::property_tree::ptree ptree() const
    {
        return ptree_;
    }

    static boost::property_tree::ptree ptree(const Property& props)
    {
		if (props.container_ != nullptr)
        	return props.container_->ptree_;
		return boost::property_tree::ptree();
    }

private:
    boost::property_tree::ptree ptree_;
};

boost::property_tree::ptree* PropertyContainer::GetPtree(const Property& property)
{
    return &(property.container_->ptree_);
}

boost::property_tree::ptree* PropertyContainer::GetSelfPtree()
{
    return &ptree_;
}

void PropertyContainer::SetPtree(Property* property,
                                 const boost::property_tree::ptree& pt)
{
    property->container_->ptree_ = pt;
}

PropertyContainer* PropertyContainer::GetContainer(const Property& property)
{
    return property.container_;
}

template <typename T>
void PropertyContainer::SetValue(const std::string& key, const T& value)
{
    ptree_.put(key, value);
}

void PropertyContainer::SetValue(const std::string& key, const Property& value)
{
    ptree_.put_child(key, value.container_->ptree_);
}

void PropertyContainer::SetValue(const std::string& key, const adk::Pointer& value)
{
    ptree_.put(key, value.get_value());
}

template <typename T>
void PropertyContainer::SetValue(const std::string& key, const std::vector<T>& value)
{
    using boost::property_tree::ptree;
    ptree& node = ptree_.put_child(key, ptree());
    for (auto it = value.begin(); it != value.end(); ++it)
    {
        node.push_back(ptree::value_type("", ptree()))->second.put_value(*it);
    }
}

void PropertyContainer::SetValue(
        const std::string& key,
        const std::vector<Property>& value)
{
    using boost::property_tree::ptree;
    ptree& node = ptree_.put_child(key, ptree());
    for (auto it = value.begin(); it != value.end(); ++it)
    {
        node.push_back(ptree::value_type("", ptree(it->container_->ptree_)));
    }
}

void PropertyContainer::SetValue(
        const std::string& key,
        const std::vector<adk::Pointer>& value)
{
    using boost::property_tree::ptree;
    ptree& node = ptree_.put_child(key, ptree());
    for (auto it = value.begin(); it != value.end(); ++it)
    {
        node.push_back(ptree::value_type("", ptree()))->second.put_value((*it).get_value());
    }
}

template <typename T>
T PropertyContainer::GetValue(const std::string& key) const
{
    try
    {
        return ptree_.get<T>(key);
    }
    catch (boost::property_tree::ptree_bad_path& err)
    {
        throw Property::InvalidKey(key);
    }
    catch (boost::property_tree::ptree_bad_data& err)
    {
        throw Property::InvalidValue(err.data<std::string>());
    }
}

Property PropertyContainer::GetPropertyValue(const std::string& key) const
{
    try
    {
        Property p;
        p.container_->ptree_ = ptree_.get_child(key);
        return p;
    }
    catch (boost::property_tree::ptree_bad_path& err)
    {
        throw Property::InvalidKey(key);
    }
}

adk::Pointer PropertyContainer::GetPointerValue(const std::string& key) const
{
    try
    {
        unsigned long ptr_value = ptree_.get<unsigned long>(key);
        return ptr_value;
    }
    catch (boost::property_tree::ptree_bad_path& err)
    {
        throw Property::InvalidKey(key);
    }
    catch (boost::property_tree::ptree_bad_data& err)
    {
        throw Property::InvalidValue(err.data<std::string>());
    }
}

template <typename T>
std::vector<T> PropertyContainer::GetVectorValue(const std::string& key) const
{
    try
    {
        using boost::property_tree::ptree;
        std::vector<T> v;
        const ptree& node = ptree_.get_child(key);
        for (auto it = node.begin(); it != node.end(); ++it)
        {
            v.push_back(it->second.get_value<T>());
        }
        return v;
    }
    catch (boost::property_tree::ptree_bad_path& err)
    {
        throw Property::InvalidKey(key);
    }
    catch (boost::property_tree::ptree_bad_data& err)
    {
        throw Property::InvalidValue(err.data<std::string>());
    }
}

std::vector<adk::Pointer> PropertyContainer::GetPointerVectorValue(const std::string& key) const
{
    try
    {
        using boost::property_tree::ptree;
        std::vector<adk::Pointer> v;
        const ptree& node = ptree_.get_child(key);
        for (auto it = node.begin(); it != node.end(); ++it)
        {
            unsigned long ptr_value = it->second.get_value<unsigned long>();
            v.push_back(ptr_value);
        }
        return v;
    }
    catch (boost::property_tree::ptree_bad_path& err)
    {
        throw Property::InvalidKey(key);
    }
    catch (boost::property_tree::ptree_bad_data& err)
    {
        throw Property::InvalidValue(err.data<std::string>());
    }
}


std::vector<Property> PropertyContainer::GetPropertyVectorValue(
        const std::string& key) const
{
    try
    {
        using boost::property_tree::ptree;
        std::vector<Property> v;
        const ptree& node = ptree_.get_child(key);
        Property p;
        for (auto it = node.begin(); it != node.end(); ++it)
        {
            p.container_->ptree_ = it->second;
            v.push_back(p);
        }
        return v;
    }
    catch (boost::property_tree::ptree_bad_path& err)
    {
        throw Property::InvalidKey(key);
    }
    catch (boost::property_tree::ptree_bad_data& err)
    {
        throw Property::InvalidValue(err.data<std::string>());
    }
}

template <typename T>
T PropertyContainer::GetValue(const std::string& key, const T& default_value) const
{
    return ptree_.get(key, default_value);
}

adk::Pointer PropertyContainer::GetValue(
        const std::string& key,
        const adk::Pointer& default_value) const
{
    try
    {
        return GetPointerValue(key);
    }
    catch (...)
    {
        return default_value;
    }
}

Property PropertyContainer::GetValue(
        const std::string& key,
        const Property& default_value) const
{
    try
    {
        return GetPropertyValue(key);
    }
    catch (...)
    {
        return default_value;
    }
}

template <typename T>
std::vector<T> PropertyContainer::GetValue(
        const std::string& key,
        const std::vector<T>& default_value) const
{
    try
    {
        return GetVectorValue<T>(key);
    }
    catch (...)
    {
        return default_value;
    }
}

std::vector<Property> PropertyContainer::GetValue(
        const std::string& key,
        const std::vector<Property>& default_value) const
{
    try
    {
        return GetPropertyVectorValue(key);
    }
    catch (...)
    {
        return default_value;
    }
}

std::vector<adk::Pointer> PropertyContainer::GetValue(
        const std::string& key,
        const std::vector<adk::Pointer>& default_value) const
{
    try
    {
        return GetPointerVectorValue(key);
    }
    catch (...)
    {
        return default_value;
    }
}

bool PropertyContainer::HasValue(const std::string& key) const
{
    return ptree_.find(key) != ptree_.not_found();
}

bool PropertyContainer::DeleteValue(const std::string& key)
{
    return ptree_.erase(key) > 0;
}

KVPairs PropertyContainer::GetKVPairs() const
{
    std::vector< std::pair<std::string, std::string> > v;
    for (auto it = ptree_.begin(); it != ptree_.end(); ++it)
    {
        v.push_back(KVPairs::value_type(it->first, it->second.data()));
    }
    return v;
}

std::string PropertyContainer::Dump(bool pretty) const
{
    std::ostringstream oss;
    boost::property_tree::json_parser::write_json_helper(oss, ptree_, 0, pretty);
    return oss.str();
}

void PropertyContainer::Override(const PropertyContainer &reference)
{
    boost::property_tree::ptree new_pt = reference.ptree_;
    for (auto item : ptree_)
    {
        new_pt.put_child(item.first, item.second);
    }
    ptree_ = new_pt;
}

void PropertyContainer::OverWriteTo(PropertyContainer &reference) const
{
    for (auto& item : ptree_)
    {
        reference.ptree_.put_child(item.first, item.second);
    }
}

void PropertyContainer::OverWriteFrom(const PropertyContainer &reference)
{
    for (auto item : reference.ptree_)
    {
        ptree_.put_child(item.first, item.second);
    }
}

void PropertyContainer::Clear()
{
    ptree_.clear();
}

}

#endif /* AMI_PROPERTY_CONTAINER_H_ */
