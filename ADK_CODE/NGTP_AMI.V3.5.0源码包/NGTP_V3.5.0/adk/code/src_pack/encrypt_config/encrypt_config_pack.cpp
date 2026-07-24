/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*  @file encrypt_config.cpp
*  @brief 用于对配置文件进行加密和解密
**/
#include "adk/encrypt_config.h"
#include "adk_pack/encrypt_config.h"
#include <cassert>

namespace adk
{
    using EncryptConfigImpl = adk_impl::ConfigFile;

    ConfigFile::ConfigFile()
    {
        config_file_impl_ = (void*)(new EncryptConfigImpl());
    }

	std::string ConfigFile::ReadConfigFile(const std::string& file_name)
    {
        assert(config_file_impl_);
        return reinterpret_cast<EncryptConfigImpl*>(config_file_impl_)->ReadConfigFile(file_name);
    }

	ConfigFile::~ConfigFile()
    {
       assert(config_file_impl_);
       delete reinterpret_cast<EncryptConfigImpl*>(config_file_impl_);
    }
}