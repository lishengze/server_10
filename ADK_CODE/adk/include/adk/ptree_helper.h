/**
 * @file ptree_helper.h
 * @brief Property Tree工具方法
 * @author Li Yunchong
 * @version 0.1
 * @date 2017-06-26
 */
#ifndef ADK_IMPL_PTREE_HELPER_H_
#define ADK_IMPL_PTREE_HELPER_H_

#include <string>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/json_parser/detail/write.hpp>

namespace adk_impl
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

} // namespace adk_impl

#endif /* ADK_PTREE_HELPER_H_ */
