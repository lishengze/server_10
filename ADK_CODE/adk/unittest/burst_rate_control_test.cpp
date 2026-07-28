#define BOOST_TEST_MODULE BurstRateController
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk/util.h>

BOOST_AUTO_TEST_CASE(BurstRateController)
{
    {
        adk_impl::BurstRateController rc1(1, 1);
        struct timespec begin, end;
        clock_gettime(CLOCK_REALTIME, &begin);
        uint64_t counter = 0;
        do
        {
            rc1.Wait();
        }
        while (++counter != 10);

        clock_gettime(CLOCK_REALTIME, &end);

        BOOST_REQUIRE((end.tv_sec - begin.tv_sec) >= 10);
        BOOST_REQUIRE((end.tv_sec - begin.tv_sec) < 13);
    }

    {
        adk_impl::BurstRateController rc1(4, 2);
        struct timespec begin, end;
        clock_gettime(CLOCK_REALTIME, &begin);
        uint64_t counter = 0;
        do
        {
            rc1.Wait();
        }
        while (++counter != 40);

        clock_gettime(CLOCK_REALTIME, &end);

        BOOST_REQUIRE((end.tv_sec - begin.tv_sec) >= 10);
        BOOST_REQUIRE((end.tv_sec - begin.tv_sec) < 13);
    }
}
