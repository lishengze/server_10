#define BOOST_TEST_MODULE property
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk/error_code.h>
#include <adk/property.h>

#include <map>
#include <set>
#include <string>
#include <vector>

//Arguments.
static std::string testVal1("First");
static std::string testVal2("Second");
static std::string testVal3("Third");
static std::string testVal4("Forth");
static std::string testVal5("Fifth");
static std::string testVal6("Sixth");
static std::string testVal7("Seventh");
static std::string testVal8("Eighth");
static std::string testVal9("Ninth");
static std::string testVal10("Tenth");
static std::string testVal11("Eleventh");

BOOST_AUTO_TEST_CASE(test_property_checkvalues)
{
    adk::Property test_prop;
    test_prop.SetValues()(testVal1, "123")(testVal2, "123")(testVal3, "123")(testVal4, "123")(testVal5, "123");
    //Basic Use
    auto ret = test_prop.CheckRequiredValues(testVal1,
                    testVal2,
                    testVal3,
                    testVal4,
                    testVal5,
                    testVal6,
                    testVal7,
                    testVal8,
                    testVal9,
                    testVal10,
                    testVal11);
    BOOST_CHECK_EQUAL(ret, false);
    std::vector<std::string> missings;
    ret = test_prop.CheckRequiredValues(missings,
                                                testVal1,
                                                testVal2,
                                                testVal3,
                                                testVal4,
                                                testVal5,
                                                testVal6,
                                                testVal7,
                                                testVal8,
                                                testVal9,
                                                testVal10,
                                                testVal11); // returns missing values.
    BOOST_CHECK_EQUAL(ret, false);
    BOOST_CHECK_EQUAL(missings.size(), 6);
    ret = test_prop.CheckRequiredValues(
                    testVal1,
                    testVal2,
                    testVal3
            );// all requires values is provided.

    BOOST_CHECK_EQUAL(ret, true);

    ret = test_prop.CheckRequiredValues(
            "First",
            testVal2,
            "Third"
    );// all requires values is provided.

    BOOST_CHECK_EQUAL(ret, true);
}