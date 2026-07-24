/** 
 *  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
 *  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
 *  For more information about Archforce, welcome to archforce.cn.
 **/
#ifndef ADK_PTREE_HELPER_H_
#define ADK_PTREE_HELPER_H_

#include <string>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/json_parser/detail/write.hpp>

namespace adk
{

/**
 * @brief 从Json字符串构造Property Tree
 *
 * @param[in] str Json格式字符串
 * @param[out] pt 输出的Property Tree
 *
 * @exception boost::property_tree::json_parser::json_parser_error 参见boost::property_tree文档
 */
inline void JsonToPtree(const std::string& str, boost::property_tree::ptree* pt)
{
    std::istringstream iss(str);
    boost::property_tree::json_parser::read_json(iss, *pt);
}

/**
 * @brief 生成Property Tree对应的Json字符串
 *
 * @param pt Property Tree
 * @param pretty 是否以易读格式输出（带有换行及缩进）
 *
 * @return Json字符串
 *
 * @exception boost::property_tree::json_parser::json_parser_error 参见boost::property_tree文档
 */
std::string PtreeToJson(const boost::property_tree::ptree &pt, bool pretty = false);

} // namespace adk

#endif /* ADK_PTREE_HELPER_H_ */
