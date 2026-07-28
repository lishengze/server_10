/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_ENCRYPT_CONFIG_H_
#define ADK_ENCRYPT_CONFIG_H_

#include <string>

namespace adk
{

/**
 * @brief      配置加密接口
 */

using std::string;

class ConfigFile
{
public:

	ConfigFile();
	~ConfigFile();

    /**
     * @brief      读取指定的配置文件内容
     *
     * @param      file_name  配置文件名称
     *
     * @return     成功时文件内容，失败返回空字符串
     */
	std::string ReadConfigFile(const std::string &file_name);

private:
	void* config_file_impl_;
};

}


#endif