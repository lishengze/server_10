/** 
 *  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
 *  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
 *  For more information about Archforce, welcome to archforce.cn.
 **/

#ifndef ADK_HTTP_UTIL_H_
#define ADK_HTTP_UTIL_H_

#include <stdint.h>

#include <map>
#include <string>

namespace adk
{

/**
 * @brief HTTP服务相关工具
 */
namespace http
{

/**
 * @brief 将字符串转换为HTML输出格式
 *
 * 输入字符串中的“&”、“<”、“>”、“ ”将被分别替换为HTML字符实体。
 *
 * @param str 原始字符串
 *
 * @return 转换过的HTML格式字符串
 */
std::string HtmlEncode(const std::string& str);

/**
 * @brief 将字符串转换为Javascript输出格式
 *
 * 输入字符串中的“\”、换行符、“"”、“'”将被分别替换为Javascript转义字符串。
 *
 * @param str   原始字符串
 * @param quot  外层引号类型，默认为'\0'（无引号），可设置为'\''或'\"'
 *              如果输出的Javascript代码是放在引号中的，则应设置该输入参数，否则不必设置
 *
 * @return 转换过的Javascript格式字符串
 */
std::string JsEncode(const std::string& str, char quot = '\0');

/**
 * @brief URL编码
 *
 * 输入字符串中的特殊字符将被转换为%xx格式以便在URL中传输
 *
 * @param str 原始字符串
 *
 * @return 转换过的URL格式字符串
 */
std::string UrlEncode(const std::string& str);

/**
 * @brief URL解码
 *
 * 将URL格式字符串转换为原始字符串
 *
 * @param str URL格式字符串
 *
 * @return 解码后的原始字符串
 */
std::string UrlDecode(const std::string& str);

/**
 * @brief 解析POST数据
 *
 * @param data 将POST数据字符串转换为key/value映射表
 *
 * @return POST数据的key/value映射表
 */
std::map<std::string, std::string> ParsePostData(const std::string& data);

} // namespace http
} // namespace adk_impl

#endif /* AMI_HTTP_UTIL_H_ */
