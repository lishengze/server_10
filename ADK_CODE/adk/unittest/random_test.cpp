#define BOOST_TEST_MODULE random
#include <boost/test/included/unit_test.hpp>

#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

#include <adk/random.h>
#include <unistd.h>
#include <stdlib.h>

volatile bool g_is_test_start = false;

void RunRandom(uint32_t* vec, uint32_t size)
{
    while (!g_is_test_start);

    uint32_t counter = 0;
    while (counter != size)
    {
        vec[counter] = adk::Random(10u, 100u);
        ++counter;
    }
}


BOOST_AUTO_TEST_CASE(test_GetElementList)
{
    #define TEST_ITER 2000000
    uint32_t* buffer1 = new uint32_t[TEST_ITER];
    uint32_t* buffer2 = new uint32_t[TEST_ITER];
    boost::thread r1 = boost::thread(boost::bind(RunRandom, buffer1, TEST_ITER));
    boost::thread r2 = boost::thread(boost::bind(RunRandom, buffer2, TEST_ITER));
    usleep(10);
    g_is_test_start = true;
    r1.join();
    r2.join();
    for (uint32_t i = 0; i < TEST_ITER; ++i)
    {
        BOOST_REQUIRE(buffer1[i] <= 100u && buffer1[i] >= 10u);
        BOOST_REQUIRE(buffer2[i] <= 100u && buffer2[i] >= 10u);
    }
}

template<typename T>
void CheckType()
{   
    // distributed in the range [min, max]
    T v1 = adk::Random<T>(std::numeric_limits<T>::min(), std::numeric_limits<T>::min());
    BOOST_CHECK_MESSAGE(v1 == std::numeric_limits<T>::min(), "value=" + std::to_string(v1));

    T v2 = adk::Random<T>(std::numeric_limits<T>::max(), std::numeric_limits<T>::max());
    BOOST_CHECK_MESSAGE(v2 == std::numeric_limits<T>::max(), "value=" + std::to_string(v2));

    T v3 = adk::Random<T>(std::numeric_limits<T>::min(), 0);
    BOOST_CHECK_MESSAGE(v3 <= 0 && v3 >= std::numeric_limits<T>::min(), "value=" + std::to_string(v3));

    T v4 = adk::Random<T>(1, std::numeric_limits<T>::max());
    BOOST_CHECK_MESSAGE(v4 >= 1 && v4 <= std::numeric_limits<T>::max(), "value=" + std::to_string(v4));

    T v5 = adk::Random<T>(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
    BOOST_CHECK_MESSAGE(v5 >= std::numeric_limits<T>::min() && v5 <= std::numeric_limits<T>::max(),
                        "value=" + std::to_string(v5));
}

BOOST_AUTO_TEST_CASE(test_Type)
{
    CheckType<int8_t>();
    CheckType<uint8_t>();

    CheckType<int16_t>();
    CheckType<uint16_t>();

    CheckType<int32_t>();
    CheckType<uint32_t>();

    CheckType<int64_t>();
    CheckType<uint64_t>();

}