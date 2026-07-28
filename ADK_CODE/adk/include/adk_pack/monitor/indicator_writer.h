/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef AMI_INDICATOR_WRITER_H_
#define AMI_INDICATOR_WRITER_H_

#include <string>

#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>

namespace adk
{

class IndicatorWriter
{
public:
    IndicatorWriter();

    ~IndicatorWriter();

    int32_t Init(const boost::filesystem::path& dir_path, const std::string& app_name);

    int32_t Write(const std::string& key, const std::string& desc, const boost::property_tree::ptree& ptree);

private:
    void* indicator_writer_impl_;
};

} // adk

#endif // AMI_INDICATOR_WRITER_H_
