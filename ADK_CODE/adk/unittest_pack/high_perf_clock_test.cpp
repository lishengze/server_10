#define BOOST_TEST_MODULE high_perf_clock
#include <adk_pack/high_performance_clock.h>
#include <boost/test/included/unit_test.hpp>
#include <chrono>
#include <thread>
#include <unistd.h>

/**
 * @brief 测试 adk high_performance_clock Read 接口
 * 
 */
BOOST_AUTO_TEST_CASE(high_perf_clock)
{
    // 第一次读取的偏差比较大  忽略
    uint64_t t0 = adk::tick::Read();
    adk::tick::ndelay(1);
    (void)(t0);  // 消除未使用变量告警

    auto start = std::chrono::high_resolution_clock::now();
    uint64_t t1 = adk::tick::Read();

    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    auto mid = std::chrono::high_resolution_clock::now();
    uint64_t t2 = adk::tick::Read();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto end = std::chrono::high_resolution_clock::now();
    uint64_t t3 = adk::tick::Read();

    std::chrono::duration<double, std::nano> elapsed1 = mid - start;
    std::chrono::duration<double, std::nano> elapsed2 = end - mid;
    std::cout << "1020 ms, t2 - t1: " << std::endl;
    std::cout << "std clock: " << static_cast<uint64_t>(elapsed1.count())
              << "\nadk clock: " << adk::tick::Diff<adk::tick::kNanoseconds>(t2, t1)
              << std::endl;

    std::cout << "200 ms, t2 - t1: " << std::endl;
    std::cout << "std clock: " << static_cast<uint64_t>(elapsed2.count())
              << "\nadk clock: " << adk::tick::Diff<adk::tick::kNanoseconds>(t3, t2)
              << std::endl;

    BOOST_REQUIRE(adk::tick::Diff<adk::tick::kSecond>(t2, t1) == 1);
    BOOST_REQUIRE(adk::tick::Diff<adk::tick::kSecond>(t3, t2) == 0);
    BOOST_REQUIRE(adk::tick::Diff<adk::tick::kMicroseconds>(t3, t2) >= 180);
}

/**
 * @brief 测试 adk high_performance_clock 的 ndelay 接口
 * 
 */
BOOST_AUTO_TEST_CASE(adk_delay)
{
    using namespace adk::tick;

    // 第一次读取的偏差比较大  忽略
    uint64_t t0 = adk::tick::Read();
    adk::tick::ndelay(1);

    t0 = adk::tick::Read();

    // 1ns
    adk::tick::ndelay(1);
    uint64_t t1 = adk::tick::Read();

    // 1us
    adk::tick::ndelay(1000);
    uint64_t t2 = adk::tick::Read();

    // 1ms
    adk::tick::ndelay(1000 * 1000);
    uint64_t t3 = adk::tick::Read();

    // 1s
    adk::tick::ndelay(1000 * 1000 * 1000);
    uint64_t t4 = adk::tick::Read();

    std::cout << "\nt1 - t0 (ns) = " << adk::tick::Diff<kNanoseconds>(t1, t0)
              << "\nt1 - t0 (us) = " << adk::tick::Diff<kMicroseconds>(t1, t0)
              << "\nt1 - t0 (ms) = " << adk::tick::Diff<kMilliseconds>(t1, t0)
              << "\nt2 - t1 (ns) = " << adk::tick::Diff<kNanoseconds>(t2, t1)
              << "\nt2 - t1 (us) = " << adk::tick::Diff<kMicroseconds>(t2, t1)
              << "\nt2 - t1 (ms) = " << adk::tick::Diff<kMilliseconds>(t2, t1)
              << "\nt3 - t2 (ns) = " << adk::tick::Diff<kNanoseconds>(t3, t2)
              << "\nt3 - t2 (us) = " << adk::tick::Diff<kMicroseconds>(t3, t2)
              << "\nt3 - t2 (ms) = " << adk::tick::Diff<kMilliseconds>(t3, t2)
              << "\nt4 - t3 (ms) = " << adk::tick::Diff<kMilliseconds>(t4, t3)
              << std::endl;

#if (defined __GNUC__ && defined __x86_64__)
    //检查 delay的时间 是否有较大的偏差
    // 1ns
    BOOST_REQUIRE(adk::tick::Diff<kMicroseconds>(t1, t0) == 0);

    if (::getenv("AMI_DOCKER_UNIT_TEST") != nullptr)
        return;
    BOOST_REQUIRE(adk::tick::Diff<kNanoseconds>(t1, t0) <= 100);
    BOOST_REQUIRE(adk::tick::Diff<kNanoseconds>(t1, t0) > 0);

    // 1us
    BOOST_REQUIRE(adk::tick::Diff<kMicroseconds>(t2, t1) == 1);

    // 1ms
    BOOST_REQUIRE(adk::tick::Diff<kMilliseconds>(t3, t2) == 1);

    // 1s
    BOOST_REQUIRE(adk::tick::Diff<kMilliseconds>(t4, t3) == 1000);
#elif (defined __GNUC__ && defined __aarch64__)
    // 1ns
    BOOST_REQUIRE(adk::tick::Diff<kNanoseconds>(t1, t0) > 0);
    BOOST_REQUIRE(adk::tick::Diff<kMicroseconds>(t1, t0) <= 1);
    BOOST_REQUIRE(adk::tick::Diff<kMilliseconds>(t1, t0) == 0);

    // 1us
    BOOST_REQUIRE(adk::tick::Diff<kMicroseconds>(t2, t1) <= 2);
    BOOST_REQUIRE(adk::tick::Diff<kMilliseconds>(t2, t1) == 0);

    // 1ms
    BOOST_REQUIRE(adk::tick::Diff<kMilliseconds>(t3, t2) == 1);

    // 1s
    BOOST_REQUIRE(adk::tick::Diff<kMilliseconds>(t4, t3) == 1000);
#endif
}
