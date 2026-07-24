#define BOOST_TEST_MODULE net_util
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk/net_utils.h>
#include <adk/error_code.h>

#include <set>
#include <string>
#include <vector>
#include <map>

using namespace adk;

BOOST_AUTO_TEST_CASE(test_GetInterfaceMtu)
{
    // 测试正常情况
    uint32_t mtu = 0;
    BOOST_REQUIRE(GetInterfaceMtu("127.0.0.1", mtu) == ErrorCode::kSuccess);
    BOOST_REQUIRE(mtu == 65536);

    // 测试异常情况，这里提供一个不存在的网卡IP
    mtu = 0;
    BOOST_REQUIRE(GetInterfaceMtu("127.0.0.2", mtu) == ErrorCode::kFailure);
    BOOST_REQUIRE(mtu == 0);
}
