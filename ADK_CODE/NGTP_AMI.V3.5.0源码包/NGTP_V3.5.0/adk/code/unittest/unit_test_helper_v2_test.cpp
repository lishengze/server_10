#define BOOST_TEST_MODULE monitor
#include <boost/test/included/unit_test.hpp>

#include <iostream>
#include <string>
#include <stdexcept>

#include <adk/unit_test_helper.h>

class TestClassBasic
{
public:
    TestClassBasic()
    {}

    ~TestClassBasic()
    {}

    ADK_MOCKABLE_METHOD std::string Foo()
    {
        return "hello";
    }

private:

};

const std::string kBarExp("bar_exp");
const std::string kFuncExp("func_exp");
const std::string kTestExe("test_exp");
const std::string kTestExeV2("test_exp_v2");
const std::string kTestExeV3("test_exp_v3");

class TestClass : public TestClassBasic
{
public:
    TestClass()   {}
    virtual std::string Foo()
    {
        return "world";
    }

    bool Bar()
    {
        if (ADK_MOCKABLE_EXPRESSION("bar_exp", bar_ret == true))
            return true;
        return false;
    }

    bool Func()
    {
        if (ADK_MOCKABLE_EXPRESSION(kFuncExp, func_ret_1 == true, boost::any(this)))
            return true;
        return false;
    }

    TestClass* ExeTest()
    {
        ADK_MOCKABLE_RETURN(kTestExe, adk::unittest::place_holder, TestClass*);
        return this;
    }

    TestClass* ExeTestV2()
    {
        ADK_MOCKABLE_RETURN(kTestExeV2, boost::any(this), TestClass*);
        return this;
    }

    void ExeTestV3(bool& ret)
    {
        ADK_MOCKABLE_RETURN(kTestExeV3, boost::any(&ret));
        ret = true;
    }

    void set_bar_ret(bool ret) { bar_ret = ret; }
    void set_func_ret_1(bool ret) { func_ret_1 = ret; }
    void set_func_ret_2(bool ret) { func_ret_2 = ret; }
    bool get_func_ret_2() { return func_ret_2; }

private:
    bool bar_ret;
    bool func_ret_1;
    bool func_ret_2;
};


BOOST_AUTO_TEST_CASE(test_mockable)
{
    TestClassBasic* base_ptr = new TestClass();
    BOOST_CHECK_EQUAL(base_ptr->Foo(), "hello");
}


BOOST_AUTO_TEST_CASE(test_mockable_expression)
{
    TestClass test;
    test.set_bar_ret(true);
    BOOST_CHECK_EQUAL(test.Bar(), true);

    adk::RegExpHandler("bar_exp", adk::unittest::kAlwaysFalse);
    BOOST_CHECK_EQUAL(test.Bar(), true);
    BOOST_CHECK_EQUAL(test.Bar(), true);
    BOOST_CHECK_EQUAL(test.Bar(), true);

    adk::RegExpHandler("bar_exp", adk::unittest::kAlwaysIgnore);
    BOOST_CHECK_EQUAL(test.Bar(), true);
    BOOST_CHECK_EQUAL(test.Bar(), true);
    BOOST_CHECK_EQUAL(test.Bar(), true);

    test.set_bar_ret(false);
    BOOST_CHECK_EQUAL(test.Bar(), false);

    adk::RegExpHandler("bar_exp", adk::unittest::kAlwaysTrue);
    BOOST_CHECK_EQUAL(test.Bar(), false);
    BOOST_CHECK_EQUAL(test.Bar(), false);
    BOOST_CHECK_EQUAL(test.Bar(), false);

    adk::RegExpHandler("bar_exp", adk::unittest::kAlwaysIgnore);
    BOOST_CHECK_EQUAL(test.Bar(), false);
    BOOST_CHECK_EQUAL(test.Bar(), false);
    BOOST_CHECK_EQUAL(test.Bar(), false);

    bool bar_ret;
    adk::RegExpHandler(kBarExp, [&bar_ret](bool& exp_val, const boost::any&){
        exp_val = bar_ret;
    });

    bar_ret = true;
    BOOST_CHECK_EQUAL(test.Bar(), false);
    BOOST_CHECK_EQUAL(test.Bar(), false);

    bar_ret = false;
    BOOST_CHECK_EQUAL(test.Bar(), false);
    BOOST_CHECK_EQUAL(test.Bar(), false);

    // test handler with context();
    test.set_func_ret_1(false);
    BOOST_CHECK_EQUAL(test.Func(), false);
    BOOST_CHECK_EQUAL(test.Func(), false);

    test.set_func_ret_1(true);
    BOOST_CHECK_EQUAL(test.Func(), true);
    BOOST_CHECK_EQUAL(test.Func(), true);

    test.set_func_ret_2(false);
    BOOST_CHECK_EQUAL(test.Func(), true);
    BOOST_CHECK_EQUAL(test.Func(), true);

    adk::RegExpHandler(kFuncExp, [](bool& exp_val, const boost::any& exp_ctx){
        TestClass& test = *boost::any_cast<TestClass*>(exp_ctx);
        exp_val = test.get_func_ret_2();
    });

    test.set_func_ret_2(false);
    BOOST_CHECK_EQUAL(test.Func(), true);
    BOOST_CHECK_EQUAL(test.Func(), true);

    test.set_func_ret_2(true);
    BOOST_CHECK_EQUAL(test.Func(), true);    
    BOOST_CHECK_EQUAL(test.Func(), true); 

    test.set_func_ret_1(false);
    BOOST_CHECK_EQUAL(test.Func(), false);
    BOOST_CHECK_EQUAL(test.Func(), false);
}

BOOST_AUTO_TEST_CASE(test_mockable_return)
{
    TestClass test;
    BOOST_CHECK_EQUAL(test.ExeTest(), &test);

    adk::RegRetHandler(kTestExe, [](const boost::any& exe_ctx, boost::any& exe_ret){
        exe_ret = new TestClass();
        return ADK_EXECUTION_RETURN;
    });
    BOOST_CHECK_EQUAL(test.ExeTest(), (&test));

    BOOST_CHECK_EQUAL(test.ExeTestV2(), (&test));

    bool ret_orig;
    adk::RegRetHandler(kTestExeV2, [&ret_orig](const boost::any& exe_ctx, boost::any& exe_ret){
        if (ret_orig)
            exe_ret = exe_ctx;
        else
            exe_ret = new TestClass();
        return ADK_EXECUTION_RETURN;
    });

    ret_orig = false;
    BOOST_CHECK_EQUAL(test.ExeTestV2(), (&test));

    ret_orig = true;
    BOOST_CHECK_EQUAL(test.ExeTestV2(), (&test));

    ret_orig = false;
    BOOST_CHECK_EQUAL(test.ExeTestV2(), (&test));

    adk::RegRetHandler(kTestExeV2, adk::unittest::kAlwaysContinue);
    BOOST_CHECK_EQUAL(test.ExeTestV2(), (&test));

    bool ret_output = false;
    test.ExeTestV3(ret_output);
    BOOST_CHECK_EQUAL(ret_output, true);
    BOOST_CHECK_EQUAL(ret_output, true);

    adk::RegRetHandler(kTestExeV3, [](const boost::any& exe_ctx, boost::any& exe_ret){
        auto ret_output = boost::any_cast<bool*>(exe_ctx);
        *ret_output = false;
        return ADK_EXECUTION_RETURN;
    });
    test.ExeTestV3(ret_output);
    BOOST_CHECK_EQUAL(ret_output, true);
    BOOST_CHECK_EQUAL(ret_output, true);
}
