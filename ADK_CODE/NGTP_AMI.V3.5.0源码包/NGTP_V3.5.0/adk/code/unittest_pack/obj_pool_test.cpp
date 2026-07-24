#define BOOST_TEST_MODULE util
#include <boost/test/included/unit_test.hpp>

#include <adk_pack/error_code.h>
#include <adk_pack/object_pool.h>
#include <thread>

template<typename Key = int>
class ObjectTypeTest : public adk::IObject
{
public:
    Key value;
    void Reset() override
    {
    }
};

BOOST_AUTO_TEST_SUITE(SingleThread)
/**
 * @brief 对象池申请释放接口基本测试
 * 
 */
BOOST_AUTO_TEST_CASE(basic)
{
    // create a pool
    int32_t ec;
    auto* pool = adk::ObjectPool<ObjectTypeTest<int>>::Create("obj_pool_test1", 128);
    ObjectTypeTest<int>* a = pool->NewObject();
    assert(pool != nullptr);
    BOOST_REQUIRE(a != nullptr);
    a->value = 1;
    ec = a->Delete();
    BOOST_REQUIRE(ec == adk::ErrorCode::kSuccess);

    for (int i = 0; i < 1000000; ++i)
    {
        auto* ptr = pool->NewObjectEx();
        BOOST_REQUIRE(ptr != nullptr);
        ptr->value = i;
        ec = ptr->Delete();
        BOOST_REQUIRE(ec == adk::ErrorCode::kSuccess);
    }
}

/**
 * @brief 持有对象 统一释放
 * 
 */
BOOST_AUTO_TEST_CASE(hold)
{
    auto* pool = adk::ObjectPool<ObjectTypeTest<std::string>>::Create("obj_pool_test2", 128);
    std::vector<ObjectTypeTest<std::string>*> storage;

    for (int i = 0; i < 1000000; ++i)
    {
        auto* ptr = pool->NewObjectEx();
        ptr->value = std::to_string(i) + "multi-threading";
        storage.push_back(ptr);
    }

    // release
    for (auto p : storage)
    {
        p->Delete();
    }
}

BOOST_AUTO_TEST_SUITE_END()