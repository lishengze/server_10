/**
 * @file ptree_helper.cpp
 * @brief Property Tree工具方法
 * @author Li Yunchong
 * @version 0.1
 * @date 2017-06-26
 */
#include <adk_pack/ptree_helper.h>

namespace adk
{

std::string PtreeToJson(const boost::property_tree::ptree &pt, bool pretty)
{
    using namespace boost::property_tree::json_parser;
    if (!verify_json(pt, 0))
    {
        BOOST_PROPERTY_TREE_THROW(json_parser_error(
                    "ptree contains data that cannot be represented in JSON format",
                    std::string(),
                    0));
    }
    std::ostringstream oss;
    write_json_helper(oss, pt, 0, pretty);
    if (!oss.good())
    {
        BOOST_PROPERTY_TREE_THROW(json_parser_error("write error", std::string(), 0));
    }
    return oss.str();
}

} // namespace adk
