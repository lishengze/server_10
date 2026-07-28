/**
 * @file property.cpp
 * @brief 配置属性类
 * @author Li Yunchong
 * @version 0.1
 * @date 2016-11-28
 */

#include <adk/property.h>
#include "property_container.h"
#include <iostream>

using std::string;
using std::vector;

namespace adk_impl
{

Property::Property()
    : container_(new PropertyContainer),
      value_setter_(*this)
{
}

Property::Property(const Property& property)
    : container_(new PropertyContainer(*property.container_)),
      value_setter_(*this)
{
}

Property::Property(const std::string& json_str)
    : container_(new PropertyContainer(json_str)),
      value_setter_(*this)
{
}

Property::Property(std::basic_istream<char> &istream)
    : container_(new PropertyContainer(istream)),
      value_setter_(*this)
{    
}

Property::~Property()
{
    delete container_;
}

Property& Property::operator=(const Property& property)
{
    *container_ = *(property.container_);
    return *this;
}

void Property::SetValue(const string& key, const char* value)
{
    container_->SetValue(key, string(value));
}

void Property::SetValue(const string& key, const string& value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const string& key, short value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const string& key, unsigned short value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const string& key, int value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const string& key, unsigned int value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const string& key, long value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const string& key, unsigned long value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const string& key, long long value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const string& key, unsigned long long value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const string& key, bool value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const string& key, float value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const string& key, double value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const string& key, long double value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const string& key, const Property& value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const string& key, const Pointer& value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const std::string& key, void* pointer)
{
    container_->SetValue(key, MakePointer(pointer));
}

void Property::SetValue(const std::string& key, std::nullptr_t pointer)
{
    // do nothing
}

void Property::SetValue(const string& key, const vector<string>& value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const string& key, const vector<int>& value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const std::string& key, const std::vector<long int>& value)
{
	container_->SetValue(key, value);
}

void Property::SetValue(const string& key, const vector<bool>& value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const string& key, const vector<Property>& value)
{
    container_->SetValue(key, value);
}

void Property::SetValue(const string& key, const vector<Pointer>& value)
{
    container_->SetValue(key, value);
}

string Property::GetStringValue(const string& key) const
{
    return container_->GetValue<string>(key);
}

int Property::GetIntValue(const string& key) const
{
    return container_->GetValue<int>(key);
}

bool Property::GetBoolValue(const string& key) const
{
    return container_->GetValue<bool>(key);
}

Property Property::GetPropertyValue(const string& key) const
{
    return container_->GetPropertyValue(key);
}

Pointer Property::GetPointerValue(const string& key) const
{
    return container_->GetPointerValue(key);
}

vector<string> Property::GetStringVectorValue(const string& key) const
{
    return container_->GetVectorValue<string>(key);
}

vector<int> Property::GetIntVectorValue(const string& key) const
{
    return container_->GetVectorValue<int>(key);
}

vector<bool> Property::GetBoolVectorValue(const string& key) const
{
    return container_->GetVectorValue<bool>(key);
}

vector<Property> Property::GetPropertyVectorValue(const string& key) const
{
    return container_->GetPropertyVectorValue(key);
}

vector<Pointer> Property::GetPointerVectorValue(const string& key) const
{
    return container_->GetPointerVectorValue(key);
}

string Property::GetValue(const string& key, const char* default_value) const
{
    return container_->GetValue(key, string(default_value));
}

string Property::GetValue(const string& key, const string& default_value) const
{
    return container_->GetValue(key, default_value);
}

short Property::GetValue(const string& key, short default_value) const
{
    return container_->GetValue(key, default_value);
}

unsigned short Property::GetValue(const string& key, unsigned short default_value) const
{
    return container_->GetValue(key, default_value);
}

int Property::GetValue(const string& key, int default_value) const
{
    return container_->GetValue(key, default_value);
}

unsigned int Property::GetValue(const string& key, unsigned int default_value) const
{
    return container_->GetValue(key, default_value);
}

long Property::GetValue(const string& key, long default_value) const
{
    return container_->GetValue(key, default_value);
}

unsigned long Property::GetValue(const string& key, unsigned long default_value) const
{
    return container_->GetValue(key, default_value);
}

long long Property::GetValue(const string& key, long long default_value) const
{
    return container_->GetValue(key, default_value);
}

unsigned long long Property::GetValue(const string& key, unsigned long long default_value) const
{
    return container_->GetValue(key, default_value);
}

bool Property::GetValue(const string& key, bool default_value) const
{
    return container_->GetValue(key, default_value);
}

float Property::GetValue(const string& key, float default_value) const
{
    return container_->GetValue(key, default_value);
}

double Property::GetValue(const string& key, double default_value) const
{
    return container_->GetValue(key, default_value);
}

long double Property::GetValue(const string& key, long double default_value) const
{
    return container_->GetValue(key, default_value);
}

Property Property::GetValue(const string& key, const Property& default_value) const
{
    return container_->GetValue(key, default_value);
}

Pointer Property::GetValue(const string& key, const Pointer& default_value) const
{
    return container_->GetValue(key, default_value);
}

vector<string> Property::GetValue(
        const string& key,
        const vector<string>& default_value) const
{
    return container_->GetValue(key, default_value);
}

vector<int> Property::GetValue(
        const string& key,
        const vector<int>& default_value) const
{
    return container_->GetValue(key, default_value);
}

vector<bool> Property::GetValue(
        const string& key,
        const vector<bool>& default_value) const
{
    return container_->GetValue(key, default_value);
}

vector<Property> Property::GetValue(
        const string& key,
        const vector<Property>& default_value) const
{
    return container_->GetValue(key, default_value);
}

vector<Pointer> Property::GetValue(
        const string& key,
        const vector<Pointer>& default_value) const
{
    return container_->GetValue(key, default_value);
}

bool Property::HasValue(const string& key) const
{
    return container_->HasValue(key);
}

bool Property::DeleteValue(const string& key)
{
    return container_->DeleteValue(key);
}

KVPairs Property::GetKVPairs() const
{
    return container_->GetKVPairs();
}

string Property::Dump(bool pretty) const
{
    return container_->Dump(pretty);
}

void Property::Clear()
{
    return container_->Clear();
}

void Property::OverWriteTo(Property& props) const
{
    container_->OverWriteTo(*props.container_);
}

void Property::OverWriteFrom(const Property& props)
{
    container_->OverWriteFrom(*props.container_);
}

void Property::MergeFrom(const Property& props)
{
    container_->MergeFrom(*props.container_);
}

boost::property_tree::ptree* Property::GetPtree(const Property& property)
{
    return PropertyContainer::GetPtree(property);
}

boost::property_tree::ptree* Property::GetSelfPtree()
{
    return container_->GetSelfPtree();
}

void Property::SetPtree(Property* property,
                        const boost::property_tree::ptree& pt)
{
    PropertyContainer::SetPtree(property, pt);
}
}
