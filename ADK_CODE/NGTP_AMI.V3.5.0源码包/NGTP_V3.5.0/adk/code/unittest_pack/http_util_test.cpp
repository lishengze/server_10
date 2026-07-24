#define BOOST_TEST_MODULE http_util
#include <boost/test/included/unit_test.hpp>

#include <adk_pack/http_util.h>
#include <unistd.h>
#include <stdlib.h>
#include <iostream>
#include <string>

BOOST_AUTO_TEST_CASE(test_http_util)
{
    std::string origin_str("<tag>abc&def<tag>");
    std::string result_str = adk::http::HtmlEncode(origin_str);

    BOOST_CHECK_EQUAL(result_str, "&lt;tag&gt;abc&amp;def&lt;tag&gt;");

    origin_str = std::string("\"NetworkAddress\":\"127.0.0.3\/24\",\"IsSingleton\":\"false\"");
    result_str = adk::http::JsEncode(origin_str);

    std::string target_str("\\\"NetworkAddress\\\":\\\"127.0.0.3/24\\\",\\\"IsSingleton\\\":\\\"false\\\"");
    BOOST_CHECK_EQUAL(result_str, target_str);

    origin_str = std::string("<tag>abc&def<tag>");
    result_str = adk::http::UrlEncode(origin_str);
    BOOST_CHECK_EQUAL(result_str, "%3Ctag%3Eabc%26def%3Ctag%3E");
    
    result_str = adk::http::UrlDecode(result_str);
    BOOST_CHECK_EQUAL(result_str, origin_str);

    origin_str = std::string("scope=all&utf8=%E2%9C%93&state=opened&author_username=user&not[label_name][]=!%2Fbin%2Fsh");

    std::map<std::string, std::string> result = adk::http::ParsePostData(origin_str);

    BOOST_CHECK_EQUAL(result["scope"], "all");
    BOOST_CHECK_EQUAL(result["author_username"], "user");
    BOOST_CHECK_EQUAL(result["not[label_name][]"], "!/bin/sh");
    BOOST_CHECK_EQUAL(result["state"], "opened");
}

