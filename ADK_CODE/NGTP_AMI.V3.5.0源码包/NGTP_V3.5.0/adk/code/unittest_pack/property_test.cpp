#define BOOST_TEST_MODULE property_test
#include <adk/property.h>
#include <adk_pack/property.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/test/included/unit_test.hpp>
#include <sstream>

#define CHECK_VECTOR(src, dst)                                                     \
    do                                                                             \
    {                                                                              \
        auto s = src;                                                              \
        BOOST_CHECK_EQUAL_COLLECTIONS(s.begin(), s.end(), dst.begin(), dst.end()); \
    } while (false);

BOOST_AUTO_TEST_CASE(Initialization)
{
    adk::Property property;

    adk::Property* prop = new adk::Property();
    delete prop;

    BOOST_CHECK_EQUAL(property.GetKVPairs().size(), 0);
    BOOST_CHECK_EQUAL(property.HasValue("key"), false);
    BOOST_CHECK_EQUAL(property.HasValue(std::string()), false);
    BOOST_CHECK_THROW(property.GetStringValue("key"), adk::Property::InvalidKey);
    BOOST_CHECK_EQUAL(property.GetValue("key", "value"), "value");
    BOOST_CHECK_EQUAL(property.Dump(), "{}");

    // 不符合格规范
    BOOST_CHECK_THROW(property = adk::Property("abc"), adk::Property::InvalidJsonString);
}

BOOST_AUTO_TEST_CASE(Assignment)
{
    adk::Property property;
    adk::Property prop1;

    prop1.SetValue(std::string("c_1"), "123");
    prop1.SetValue(std::string("c_2"), 123);
    prop1.SetValue(std::string("c_3"), double(2.0));
    prop1.SetValue(std::string("c_4"), (uint64_t)(0xf0f0f0f0f0));

    // clang-format off
    property(std::string("a"), "123")
            (std::string("b"), "true")
            (std::string("d"), "")
            (std::string("c"), prop1);
    // clang-format on

    adk::KVPairs kv = property.GetKVPairs();
    BOOST_CHECK_EQUAL(kv.size(), 4);
    BOOST_CHECK_EQUAL(kv[0].first, "a");
    BOOST_CHECK_EQUAL(kv[0].second, "123");
    BOOST_CHECK_EQUAL(kv[1].first, "b");
    BOOST_CHECK_EQUAL(kv[1].second, "true");
    BOOST_CHECK_EQUAL(kv[2].first, "d");
    BOOST_CHECK_EQUAL(kv[2].second, "");
    BOOST_CHECK_EQUAL(kv[3].first, "c");
    BOOST_CHECK_EQUAL(kv[3].second, "");
    BOOST_CHECK_EQUAL((property.GetPropertyValue("c")).Dump(),
                      "{\"c_1\":\"123\",\"c_2\":\"123\","
                      "\"c_3\":\"2\",\"c_4\":\"1034834473200\"}");

    BOOST_CHECK_EQUAL(prop1.GetValue("c_1", 0), 123);
    BOOST_CHECK_EQUAL(property.GetValue("c.c_1", 0), 123);

    BOOST_CHECK_EQUAL(property.HasValue("key"), false);
    BOOST_CHECK_EQUAL(property.HasValue("a"), true);
    BOOST_CHECK_EQUAL(property.HasValue(std::string()), false);
    BOOST_CHECK_EQUAL(property.GetIntValue("a"), 123);
    BOOST_CHECK_EQUAL(property.GetStringValue("a"), "123");
    BOOST_CHECK_EQUAL(property.GetBoolValue("b"), true);
    BOOST_CHECK_EQUAL(property.GetStringValue("b"), "true");
    BOOST_CHECK_THROW(prop1.GetIntValue("c_4"), adk::Property::InvalidValue);
    BOOST_CHECK_EQUAL(prop1.GetStringValue("c_4"), "1034834473200");

    BOOST_CHECK_THROW(property.GetIntValue("b"), adk::Property::InvalidValue);
    BOOST_CHECK_EQUAL(property.GetValue("a", 123), 123);
    BOOST_CHECK_EQUAL(property.GetValue("b", false), true);
    BOOST_CHECK_EQUAL(property.GetValue("b", 123), 123);
    std::vector<std::string> empty;
    BOOST_CHECK(property.GetValue("d", empty).empty());
    BOOST_CHECK_EQUAL(property.Dump(), "{\"a\":\"123\",\"b\":\"true\",\"d\":\"\",\"c\":{\"c_1\":\"123\","
                                       "\"c_2\":\"123\",\"c_3\":\"2\",\"c_4\":\"1034834473200\"}}");
}

BOOST_AUTO_TEST_CASE(BasicTypeCheck)
{
    adk::Property prop1;

    int16_t i16_int = ((1 << 15) - 1);
    uint16_t u16_int = ((1 << 16) - 1);
    int32_t i32_int = ((1LL << 31) - 1);
    uint32_t u32_int = ((1LL << 32) - 1);
    int64_t i64_int = ((1ULL << 63) - 1);
    uint64_t u64_int = (i64_int - 1) + i64_int;

    prop1.SetValue("a", (short)i16_int);
    prop1.SetValue("b", (unsigned short)u16_int);
    prop1.SetValue("c", (long)i32_int);
    prop1.SetValue("d", (unsigned long)u32_int);
    prop1.SetValue("e", (long long)i64_int);
    prop1.SetValue("f", (unsigned long long)u64_int);

    BOOST_CHECK_EQUAL(prop1.GetValue("a", (short)0), i16_int);
    BOOST_CHECK_EQUAL(prop1.GetIntValue("a"), i16_int);
    BOOST_CHECK_EQUAL(prop1.GetValue("b", (unsigned short)0), u16_int);
    BOOST_CHECK_EQUAL(prop1.GetIntValue("b"), u16_int);
    BOOST_CHECK_EQUAL(prop1.GetValue("c", (long)0), i32_int);
    BOOST_CHECK_EQUAL(prop1.GetIntValue("c"), i32_int);
    BOOST_CHECK_EQUAL(prop1.GetValue("d", (unsigned long)0), u32_int);

    // 数值溢出 非法类型
    BOOST_CHECK_THROW(prop1.GetIntValue("d"), adk::Property::InvalidValue);

    BOOST_CHECK_EQUAL(prop1.GetValue("e", (long long)0), i64_int);
    BOOST_CHECK_EQUAL(prop1.GetValue("f", (unsigned long long)0), u64_int);
}

BOOST_AUTO_TEST_CASE(FloatTypeCheck)
{
    adk::Property prop1;
    float f_val = 100.100;
    double d_val = (double)10E300 + (double)0.01;
    long double ld_val = (long double)10E-300 + (long double)0.02;

    prop1.SetValue("a", f_val);
    prop1.SetValue("b", d_val);
    prop1.SetValue("c", ld_val);

    BOOST_CHECK_EQUAL(prop1.GetValue("a", (float)0), f_val);
    BOOST_CHECK_EQUAL(prop1.GetValue("b", (double)0), d_val);

    // ld_val = inf
    BOOST_CHECK_EQUAL(prop1.GetValue("c", (long double)0), ld_val);
}

/**
 * @brief 测试数组类型
 * 
 */
BOOST_AUTO_TEST_CASE(VectorTypeCheck)
{
    adk::Property prop1;
    adk::Property prop2;

    std::vector<bool> bool_vec({true, false});
    std::vector<int> int_vec({10, 100});
    std::vector<std::string> str_vec({"s1", "s2"});

    prop1.SetValue("a", bool_vec);
    prop1.SetValue("b", int_vec);
    prop2.SetValue("a", str_vec);

    CHECK_VECTOR(prop1.GetValue("a", std::vector<bool>()), bool_vec);
    CHECK_VECTOR(prop1.GetBoolVectorValue("a"), bool_vec);

    CHECK_VECTOR(prop1.GetValue("b", std::vector<int>()), int_vec);
    CHECK_VECTOR(prop1.GetIntVectorValue("b"), int_vec);

    CHECK_VECTOR(prop2.GetValue("a", std::vector<std::string>()), str_vec);
    CHECK_VECTOR(prop2.GetStringVectorValue("a"), str_vec);

    adk::Property prop3;
    prop3.SetValue("ab", std::vector<adk::Property>({prop1, prop2}));
    auto temp_prop = prop3.GetValue("ab", std::vector<adk::Property>());
    auto temp_prop2 = prop3.GetPropertyVectorValue("ab");
    BOOST_CHECK_EQUAL(temp_prop.size(), 2);
    BOOST_CHECK_EQUAL(temp_prop[0].Dump(), prop1.Dump());
    BOOST_CHECK_EQUAL(temp_prop[1].Dump(), prop2.Dump());
    BOOST_CHECK_EQUAL(temp_prop2.size(), 2);
    BOOST_CHECK_EQUAL(temp_prop2[0].Dump(), prop1.Dump());
    BOOST_CHECK_EQUAL(temp_prop2[1].Dump(), prop2.Dump());

    adk::Property prop4;
    prop4.SetValue("c", prop2);
    auto prop = prop4.GetValue("d", prop1);
    BOOST_CHECK_EQUAL(prop.Dump(), prop1.Dump());
    prop = prop4.GetPropertyValue("c");
    BOOST_CHECK_EQUAL(prop.Dump(), prop2.Dump());
}

BOOST_AUTO_TEST_CASE(BoostPtree)
{
    adk::Property prop1;
    adk::Property property;

    prop1.SetValue(std::string("c_1"), "123");
    prop1.SetValue(std::string("c_2"), 123);
    prop1.SetValue(std::string("c_3"), double(2.0));
    prop1.SetValue(std::string("c_4"), (uint64_t)(0xf0f0f0f0f0));

    // 获取ptree 裸对象
    boost::property_tree::ptree* ptree = adk::Property::GetPtree(prop1);
    BOOST_ASSERT(ptree != nullptr);

    // clang-format off
    property(std::string("a"), "123")
            (std::string("b"), "true")
            (std::string("d"), "");
            // (std::string("c"), prop1);
    // clang-format on

    boost::property_tree::ptree* ptree2 = property.GetSelfPtree();
    // boost::property_tree::ptree* ptree2 = adk::Property::GetPtree(property);
    BOOST_ASSERT(ptree2 != nullptr);
    // 使用ptree 接口操作
    ptree2->put_child("c", *ptree);

    // 操作完成后， 使用property 检查内容是否改变，是否正确
    adk::KVPairs kv = property.GetKVPairs();
    BOOST_CHECK_EQUAL(kv.size(), 4);
    BOOST_CHECK_EQUAL(kv[0].first, "a");
    BOOST_CHECK_EQUAL(kv[0].second, "123");
    BOOST_CHECK_EQUAL(kv[1].first, "b");
    BOOST_CHECK_EQUAL(kv[1].second, "true");
    BOOST_CHECK_EQUAL(kv[2].first, "d");
    BOOST_CHECK_EQUAL(kv[2].second, "");
    BOOST_CHECK_EQUAL(kv[3].first, "c");
    BOOST_CHECK_EQUAL(kv[3].second, "");
    BOOST_CHECK_EQUAL((property.GetPropertyValue("c")).Dump(),
                      "{\"c_1\":\"123\",\"c_2\":\"123\","
                      "\"c_3\":\"2\",\"c_4\":\"1034834473200\"}");

    BOOST_CHECK_EQUAL(prop1.GetValue("c_1", 0), 123);
    BOOST_CHECK_EQUAL(property.GetValue("c.c_1", 0), 123);

    BOOST_CHECK_EQUAL(property.HasValue("key"), false);
    BOOST_CHECK_EQUAL(property.HasValue("a"), true);
    BOOST_CHECK_EQUAL(property.HasValue(std::string()), false);
    BOOST_CHECK_EQUAL(property.GetIntValue("a"), 123);
    BOOST_CHECK_EQUAL(property.GetStringValue("a"), "123");
    BOOST_CHECK_EQUAL(property.GetBoolValue("b"), true);
    BOOST_CHECK_EQUAL(property.GetStringValue("b"), "true");
    BOOST_CHECK_THROW(prop1.GetIntValue("c_4"), adk::Property::InvalidValue);
    BOOST_CHECK_EQUAL(prop1.GetStringValue("c_4"), "1034834473200");

    BOOST_CHECK_THROW(property.GetIntValue("b"), adk::Property::InvalidValue);
    BOOST_CHECK_EQUAL(property.GetValue("a", 123), 123);
    BOOST_CHECK_EQUAL(property.GetValue("b", false), true);
    BOOST_CHECK_EQUAL(property.GetValue("b", 123), 123);
    std::vector<std::string> empty;
    BOOST_CHECK(property.GetValue("d", empty).empty());
    BOOST_CHECK_EQUAL(property.Dump(), "{\"a\":\"123\",\"b\":\"true\",\"d\":\"\",\"c\":{\"c_1\":\"123\","
                                       "\"c_2\":\"123\",\"c_3\":\"2\",\"c_4\":\"1034834473200\"}}");
}

BOOST_AUTO_TEST_CASE(ExceptionCheck)
{
    adk::Property prop1;
    prop1.SetValue("key", std::string("values"));

    BOOST_CHECK_THROW(prop1.GetPropertyValue("a"), adk::Property::InvalidKey);

    auto prop2 = prop1.GetPropertyValue("key");
    BOOST_CHECK_EQUAL(prop2.GetValue("key", std::string("")), std::string(""));

    BOOST_CHECK_THROW(prop1.GetStringVectorValue("a"), adk::Property::InvalidKey);
    auto str_vec = prop1.GetStringVectorValue("key");
    BOOST_CHECK_EQUAL(str_vec.size(), 0);

    BOOST_CHECK_THROW(prop1.GetPointerVectorValue("a"), adk::Property::InvalidKey);
    prop1.GetPointerVectorValue("key");
    auto ptr_vec = prop1.GetPointerVectorValue("key");
    BOOST_CHECK_EQUAL(ptr_vec.size(), 0);

    BOOST_CHECK_THROW(prop1.GetPropertyVectorValue("a"), adk::Property::InvalidKey);
    prop1.GetPropertyVectorValue("key");
    auto prop_vec = prop1.GetPropertyVectorValue("key");
    BOOST_CHECK_EQUAL(prop_vec.size(), 0);
}

BOOST_AUTO_TEST_CASE(PointerTypeCheck)
{
    adk::Property prop1;

    // Pointer
    adk::Pointer p1 = (unsigned long)new char[1];
    adk::Pointer p2 = (unsigned long)new char[2];
    prop1.SetValue("a", p1);
    prop1.SetValue("b", std::vector<adk::Pointer>({p1, p2}));
    prop1.SetValue("c", nullptr);  // do nothing

    BOOST_CHECK_EQUAL(prop1.GetValue("a", p2).get_value(), p1.get_value());
    BOOST_CHECK_EQUAL(prop1.GetPointerValue("a").get_value(), p1.get_value());
    // 非法类型获取
    BOOST_CHECK_THROW(prop1.GetPointerValue("b").get_value(), adk::Property::InvalidValue);

    auto vec = prop1.GetPointerVectorValue("b");
    BOOST_CHECK_EQUAL(vec[0].get_value(), p1.get_value());
    BOOST_CHECK_EQUAL(vec[1].get_value(), p2.get_value());

    auto vec2 = prop1.GetValue("b", std::vector<adk::Pointer>());
    BOOST_CHECK_EQUAL(vec2[0].get_value(), p1.get_value());
    BOOST_CHECK_EQUAL(vec2[1].get_value(), p2.get_value());

    auto vec3 = prop1.GetValue("a", std::vector<adk::Pointer>());
    BOOST_CHECK_EQUAL(vec3.size(), 0);
}

BOOST_AUTO_TEST_CASE(Delete)
{
    adk::Property prop1;

    prop1.SetValue("a", "string1");
    prop1.SetValue("b", "string2");

    BOOST_CHECK_EQUAL(prop1.GetStringValue("a"), "string1");
    BOOST_CHECK_EQUAL(prop1.DeleteValue("a"), true);
    BOOST_CHECK_THROW(prop1.GetStringValue("a"), adk::Property::InvalidKey);
    prop1.SetValue("a", "string3");
    BOOST_CHECK_EQUAL(prop1.GetStringValue("a"), "string3");
}

BOOST_AUTO_TEST_CASE(OverWrite)
{
    adk::Property prop1;
    adk::Property prop2;

    prop1.SetValue("a", "string1");
    prop1.SetValue("b", "string2");

    prop2.SetValue("a", "string3");
    prop2.SetValue("c", "string4");
    // 用 prop2 覆盖 prop1
    prop1.OverWriteFrom(prop2);

    // 覆盖后 没有的值会合并
    // 已经存在的会以 prop2 为准
    BOOST_CHECK_EQUAL(prop1.GetStringValue("a"), "string3");
    BOOST_CHECK_EQUAL(prop1.GetStringValue("b"), "string2");
    BOOST_CHECK_EQUAL(prop1.GetStringValue("c"), "string4");

    BOOST_CHECK_EQUAL(prop2.GetStringValue("a"), "string3");
    BOOST_CHECK_EQUAL(prop2.GetStringValue("c"), "string4");
    BOOST_CHECK_THROW(prop2.GetStringValue("b"), adk::Property::InvalidKey);

    BOOST_CHECK_EQUAL(prop1.DeleteValue("a"), true);
    BOOST_CHECK_THROW(prop1.GetStringValue("a"), adk::Property::InvalidKey);

    adk::Property prop3;
    adk::Property prop4;

    prop3.SetValue("a", "string1");
    prop3.SetValue("b", "string2");

    prop4.SetValue("a", "string3");
    prop4.SetValue("c", "string4");

    // 用 prop4 覆盖 prop3
    prop4.OverWriteTo(prop3);

    // 覆盖后 没有的值会合并
    // 已经存在的会以 prop4 为准
    BOOST_CHECK_EQUAL(prop3.GetStringValue("a"), "string3");
    BOOST_CHECK_EQUAL(prop3.GetStringValue("b"), "string2");
    BOOST_CHECK_EQUAL(prop3.GetStringValue("c"), "string4");

    BOOST_CHECK_EQUAL(prop4.GetStringValue("a"), "string3");
    BOOST_CHECK_EQUAL(prop4.GetStringValue("c"), "string4");
    BOOST_CHECK_THROW(prop4.GetStringValue("b"), adk::Property::InvalidKey);

    // 对发送覆盖的值执行删除操作
    BOOST_CHECK_EQUAL(prop3.DeleteValue("a"), true);
    BOOST_CHECK_THROW(prop3.GetStringValue("a"), adk::Property::InvalidKey);
}
