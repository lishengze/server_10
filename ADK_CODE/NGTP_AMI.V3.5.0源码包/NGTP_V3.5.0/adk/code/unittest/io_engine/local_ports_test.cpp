#define BOOST_TEST_MODULE local_ports

#include <map>
#include <string>
#include <vector>
#include <utility>

#include <stdlib.h>

#include <local_ports.h>
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(test_LocalPorts)
{
    constexpr int32_t kEndpointIdStart = 0;

    constexpr uint16_t kPortLow = 30000;
    constexpr uint16_t kPortHigh = 60000;

    adk::io_engine::LocalPorts local_ports(kPortLow, kPortHigh);

    BOOST_CHECK_EQUAL(local_ports.fail_nr_, 0);
    BOOST_CHECK_EQUAL(local_ports.bind_fail_ports_.size(), 0);
    BOOST_CHECK_EQUAL(local_ports.in_use_ports_.size(), 0);

    const auto ports_range_size = local_ports.local_ports_.size();
    const auto ports_range_low = local_ports.local_ports_.front();
    const auto ports_range_high = local_ports.local_ports_.back();

    BOOST_TEST_MESSAGE(std::string("PortRange<[") + std::to_string(ports_range_low) 
                        + ":" + std::to_string(ports_range_high) + std::string("]>"));

    BOOST_CHECK_LE(ports_range_low, ports_range_high);

    BOOST_CHECK_GE(ports_range_low, kPortLow);
    BOOST_CHECK_LE(ports_range_low, kPortHigh);

    BOOST_CHECK_GE(ports_range_high, kPortLow);
    BOOST_CHECK_LE(ports_range_high, kPortHigh);

    for (uint16_t index = ports_range_low; index <= ports_range_high; ++index)
    {
        const auto allocate_port = local_ports.AllocatePort();

        BOOST_CHECK_GE(allocate_port, ports_range_low);
        BOOST_CHECK_LE(allocate_port, ports_range_high);

        local_ports.BindSuccess(allocate_port, kEndpointIdStart + index);
    }

    const auto allocate_port_failed = local_ports.AllocatePort();
    BOOST_CHECK_EQUAL(allocate_port_failed, 0);

    BOOST_CHECK_EQUAL(local_ports.in_use_ports_.size(), ports_range_size);

    for (uint16_t index = ports_range_low; index <= ports_range_high; ++index)
    {
        local_ports.FreePort(kEndpointIdStart + index);
    }

    BOOST_CHECK_EQUAL(local_ports.fail_nr_, 0);
    BOOST_CHECK_EQUAL(local_ports.local_ports_.size(), ports_range_size);
    BOOST_CHECK_EQUAL(local_ports.bind_fail_ports_.size(), 0);
    BOOST_CHECK_EQUAL(local_ports.in_use_ports_.size(), 0);

    const uint16_t reserve_ports[] = 
    {
        35000, 36000, 37000, 38000, 39000, 50000, 51000, 52000, 53000, 55000, 
        56000, 58000, 59000, 59999, 60000, 33000, 31000, 30010, 32810, 51028,
        16000, 28000, 19000, 29999, 10000, 23000, 11000, 20010, 62810, 61028
    };

    std::vector<std::pair<uint16_t, int32_t>> reserve_ports_vec;
    for (uint32_t index = 0; index < sizeof(reserve_ports) / sizeof(uint16_t); ++index)
    {
        const auto reserve_port = reserve_ports[index];
        const auto endpoint_id = kEndpointIdStart + 1 + index;

        BOOST_CHECK(local_ports.in_use_ports_.end() == local_ports.in_use_ports_.find(endpoint_id));
        local_ports.ReservePort(reserve_port, endpoint_id);

        reserve_ports_vec.push_back(std::make_pair(reserve_port, endpoint_id));
    }
    
    uint32_t bind_success_nr = 0;
    std::map<uint16_t, int32_t> ports_map;
    int32_t endpoint_id_s = kEndpointIdStart + 1 + reserve_ports_vec.size();
    const auto ports_range_size1 = local_ports.local_ports_.size();
    do
    {
        const auto allocate_port = local_ports.AllocatePort();
        if (0 == random() % 8)
        {
            local_ports.BindFail(allocate_port);
        }
        else
        {
            const auto endpoint_id = endpoint_id_s + bind_success_nr++;

            BOOST_CHECK(local_ports.in_use_ports_.end() == local_ports.in_use_ports_.find(endpoint_id));
            local_ports.BindSuccess(allocate_port, endpoint_id);

            ports_map[allocate_port] = endpoint_id;
        }
    } while (bind_success_nr < ports_range_size1);

    BOOST_CHECK_EQUAL(local_ports.local_ports_.size(), 0);
    BOOST_CHECK_EQUAL(local_ports.bind_fail_ports_.size(), 0);
    BOOST_CHECK_EQUAL(local_ports.in_use_ports_.size(), reserve_ports_vec.size() + ports_range_size1);

    for (const auto& node : reserve_ports_vec)
    {
        local_ports.FreePort(node.second);
    }

    for (const auto& node : ports_map)
    {
        local_ports.FreePort(node.second);
    }

    BOOST_CHECK_EQUAL(local_ports.fail_nr_, 0);
    BOOST_CHECK_EQUAL(local_ports.local_ports_.size(), ports_range_size);
    BOOST_CHECK_EQUAL(local_ports.bind_fail_ports_.size(), 0);
    BOOST_CHECK_EQUAL(local_ports.in_use_ports_.size(), 0); 
}