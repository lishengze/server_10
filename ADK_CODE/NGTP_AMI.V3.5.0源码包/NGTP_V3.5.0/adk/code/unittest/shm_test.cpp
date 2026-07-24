#define BOOST_TEST_MODULE util
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk/shm.h>

#include <set>
#include <string>
#include <vector>
#include <map>

using namespace adk;

BOOST_AUTO_TEST_CASE(test_shm_map)
{   
    // clear evn
    ShmFactory::Destroy("abc");

    // test start
    std::string shm_name = "abc";
    void* addr1 = ShmFactory::Create("abc", 1024*1024);
    BOOST_REQUIRE(addr1 != nullptr);
    BOOST_REQUIRE(ADK_IS_PAGE_ALIGN(addr1));

    void* addr2 = ShmFactory::Create("abc", 1024*1024);
    BOOST_REQUIRE(addr2 == nullptr);

    void* addr3 = ShmFactory::Attach("abc");
    BOOST_REQUIRE(addr3 != nullptr);    
    BOOST_REQUIRE(ADK_IS_PAGE_ALIGN(addr3));
    BOOST_REQUIRE(addr3 != addr1);

    ((char*)addr1)[(1024*1024)-1] = 'x';
    BOOST_REQUIRE(((char*)addr3)[(1024*1024)-1] == 'x');

    ((char*)addr3)[0] = 'z';
    BOOST_REQUIRE(((char*)addr1)[0] == 'z');

    BOOST_REQUIRE(ShmFactory::Detach("abc") == ErrorCode::kSuccess);

    addr2 = ShmFactory::Create("abc", 1024*1024);
    BOOST_REQUIRE(addr2 == nullptr);

    BOOST_REQUIRE(ShmFactory::Destroy("abc") == ErrorCode::kSuccess);
    addr2 = ShmFactory::Create("abc", 1024*1024);
    BOOST_REQUIRE(addr2 != nullptr);    
    BOOST_REQUIRE(ADK_IS_PAGE_ALIGN(addr2));
}
