//
// Created by lzn on 12/2/19.
//

#define BOOST_TEST_MODULE pipeline
#include <boost/test/included/unit_test.hpp>

#include <adk/util.h>

using namespace adk;
namespace Case1
{
    struct AnotherClass
    {
        uint64_t k = 9;
    public:
        virtual void* anotherFunc()
        {
            return nullptr;
        }
    };

    struct BaseClass
    {
        uint64_t i = 0;
    public:
        virtual void* testFunc()
        {
            return this;
        }
    };

    struct DerivedClass : public AnotherClass, public BaseClass
    {
        uint64_t j = 1;
    public:
        void* testFunc() override
        {
            j = 2; //testFunc invoked
            return this;
        }
    };
}

BOOST_AUTO_TEST_CASE(tmf_basic)
{
    using namespace Case1;
    DerivedClass obj;
    auto func = TransformMemberFunction(&obj, &DerivedClass::testFunc);
    auto func_2 = TransformMemberFunction(&obj, &BaseClass::testFunc);
    void* return_value = func();

    BOOST_REQUIRE(return_value == (void*)(&obj));
    BOOST_REQUIRE(obj.j == 2);

    obj.j = 1;
    void* return_value_2 = func_2();

    BOOST_REQUIRE(return_value_2 == (void*)(&obj));
    BOOST_REQUIRE(obj.j == 2);
}

namespace Case2
{
    struct BaseClass
    {
        int i = 0;
        virtual void* testFunc()
        {
            return this;
        }
    };

    struct DerivedClass_1 : public virtual BaseClass
    {
        int j = 1;
    };

    struct DerivedClass_2 : public virtual BaseClass
    {
        int k = 2;
    };

    struct FinalClass : public DerivedClass_1, public DerivedClass_2
    {
        int l = 3;
    };
}


BOOST_AUTO_TEST_CASE(tmf_virtual_derive)
{
    using namespace Case2;
    FinalClass obj;
    auto func = TransformMemberFunction(&obj, &FinalClass::testFunc);

    auto ret = func();

    BOOST_REQUIRE(ret == static_cast<BaseClass*>(&obj));
}

namespace Case3
{
    struct BaseClass
    {
        int i = 1;
        void* testFunc()
        {
            return this;
        }
    };

    struct DerivedClass : public BaseClass
    {
        int j = 1;
    };
}

BOOST_AUTO_TEST_CASE(tmf_member_function)
{
    using namespace Case3;
    DerivedClass obj;
    auto func = TransformMemberFunction(&obj, &DerivedClass::testFunc);

    auto ret = func();

    BOOST_REQUIRE(ret == static_cast<BaseClass*>(&obj));
}