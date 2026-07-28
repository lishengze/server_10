#define BOOST_TEST_MODULE util
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk/arch/generic.h>

#include <set>
#include <string>
#include <vector>
#include <map>

BOOST_AUTO_TEST_CASE(test_rdtscp)
{
    uint64_t a = adk::SyncGetTSC();
    uint64_t b = adk::SyncGetTSC();
    BOOST_REQUIRE(b > a);
}
