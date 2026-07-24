#define BOOST_TEST_MODULE UnformClock
#include <adk/uniform_clock.h>
#include <boost/test/included/unit_test.hpp>
#include <ctime>
#include <iostream>
#include <thread>

void stat()
{
#ifndef NDEBUG
    std::cout << "\nstat: " << std::endl;
    std::cout << "adjust: <forward>  " << adk::uniform_clock::adjust_forward << std::endl;
    std::cout << "adjust: <backward> " << adk::uniform_clock::adjust_backward << std::endl;
    std::cout << "adjust: <negtive>  " << adk::uniform_clock::adjust_negtive << std::endl;
#endif
}

BOOST_AUTO_TEST_CASE(UniformClockBasicTest)
{
    std::cout << "\nUniformClockBasicTest:" << std::endl;
    // now to time_t
    std::time_t base_line;
    {
        // base line
        std::time_t tp = adk::uniform_clock::to_time_t(adk::uniform_clock::now());
        std::cout << ctime(&tp);
        base_line = tp;
    }

    // offset positive
    {
        adk::uniform_clock::offset(15);  // 15s
        adk::uniform_clock::offset(static_cast<std::time_t>(10));  // 10s
        adk::uniform_clock::offset(5);  // 5s
        std::time_t tp1 = adk::uniform_clock::to_time_t(adk::uniform_clock::now());
        std::cout << ctime(&tp1);

        BOOST_REQUIRE_GE(tp1, base_line + 30);

        adk::uniform_clock::offset(timespec {15, 0});  // 15s
        adk::uniform_clock::offset(std::chrono::seconds(10));  // 10 s
        adk::uniform_clock::offset(adk::uniform_clock::duration(5L * ADK_NANOSECONDS));  // 5s

        std::time_t tp2 = adk::uniform_clock::to_time_t(adk::uniform_clock::now());
        std::cout << ctime(&tp2);

        BOOST_REQUIRE_GE(tp2, base_line + 60);
    }

    // offset negtive
    {
        adk::uniform_clock::offset(-15);  // 15s
        adk::uniform_clock::offset(static_cast<std::time_t>(-10));  // 10s
        adk::uniform_clock::offset(-5);  // 5s
        std::time_t tp1 = adk::uniform_clock::to_time_t(adk::uniform_clock::now());
        std::cout << ctime(&tp1);

        BOOST_REQUIRE_GE(tp1, base_line - 30);

        adk::uniform_clock::offset(timespec {-15, 0});  // 15s
        adk::uniform_clock::offset(std::chrono::seconds(-10));  // 10 s
        adk::uniform_clock::offset(adk::uniform_clock::duration(-5L * ADK_NANOSECONDS));  // 5s

        std::time_t tp2 = adk::uniform_clock::to_time_t(adk::uniform_clock::now());
        std::cout << ctime(&tp2);

        BOOST_REQUIRE_GE(tp2, base_line - 60);
    }

    // reset
    {
        adk::uniform_clock::reset();

        // after reset, should be the same with base line
        std::time_t tp = adk::uniform_clock::to_time_t(adk::uniform_clock::now());
        std::cout << ctime(&tp);
        BOOST_REQUIRE_GE(base_line, tp);
    }

    // from time_t
    {
        std::time_t t1                    = time(nullptr);
        adk::uniform_clock::time_point tp = adk::uniform_clock::from_time_t(t1);
        BOOST_REQUIRE_EQUAL(std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count(), t1);
    }
}

// delta: 2s interval
BOOST_AUTO_TEST_CASE(UniformClockCorrect)
{
    adk::uniform_clock::reset();

    int cnt = 1000000;
    while (cnt--)
    {
        time_t t   = adk::uniform_clock::to_time_t(adk::uniform_clock::now());
        time_t cmp = time(nullptr);
        BOOST_REQUIRE_GE(t, cmp);
    }
}

BOOST_AUTO_TEST_CASE(UniformClockPressureTest)
{
    std::cout << "\nUniformClockPressureTest:" << std::endl;
    int64_t counter = 10000000;
    for (int64_t i = 0; i < counter; i++)
    {
        struct timespec ts;
        ::clock_gettime(CLOCK_REALTIME, &ts);

        auto tp              = adk::uniform_clock::now();
        int64_t self_clock   = tp.time_since_epoch().count();
        int64_t system_clock = ts.tv_sec * ADK_NANOSECONDS + ts.tv_nsec;

        // delat: 1s
        if (!getenv("AMI_DOCKER_UNIT_TEST"))
        {
            BOOST_REQUIRE_GE(1000000000, std::abs(self_clock - system_clock));
        }
    }
}

BOOST_AUTO_TEST_CASE(UniformClockBench)
{
    std::cout << "\nUniformClockBench:" << std::endl;
    {
        // bench now()
        std::thread bench([]() {
            auto start      = std::chrono::steady_clock::now();
            int64_t counter = 1000000;
            for (int64_t i = 0; i < counter; i++)
            {
                adk::uniform_clock::now();
            }
            auto end = std::chrono::steady_clock::now();

            int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            std::cout << "now() elapsed  : " << elapsed << " ms" << std::endl;
            std::cout << "now() benchmark: " << counter * 1000.0 / elapsed << " times/s" << std::endl;
        });

        bench.join();
    }

    {
        // bench offset() with std::chrono::duration
        std::thread bench([]() {
            auto start      = std::chrono::steady_clock::now();
            int64_t counter = 1000000;
            for (int64_t i = 0; i < counter; i++)
            {
                adk::uniform_clock::offset(std::chrono::milliseconds(1));  // 1ms
            }
            auto end = std::chrono::steady_clock::now();

            int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            std::cout << "offset() elapsed  : " << elapsed << " ms" << std::endl;
            std::cout << "offset() benchmark: " << counter * 1000.0 / elapsed << " times/s" << std::endl;
        });

        bench.join();
    }

    {
        adk::uniform_clock::reset();  // reset is required

        std::time_t tp                 = adk::uniform_clock::to_time_t(adk::uniform_clock::now());
        std::time_t seconds_from_epoch = time(nullptr);
        BOOST_REQUIRE_EQUAL(tp, seconds_from_epoch);

        // bench offset() with std::size_t and negtive seconds
        std::thread bench([=]() {
            int64_t counter = seconds_from_epoch + 1;
            auto start      = std::chrono::steady_clock::now();
            for (int64_t i = 0; i < counter; i++)
            {
                adk::uniform_clock::offset(std::time_t(-1));  // 1s
            }
            auto end        = std::chrono::steady_clock::now();
            int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            std::cout << "offset() elapsed  : " << elapsed << " ms" << std::endl;
            std::cout << "offset() benchmark: " << counter * 1000.0 / elapsed << " times/s" << std::endl;
        });
        bench.join();
    }

    stat();
}

BOOST_AUTO_TEST_CASE(UniformClockHowLong)
{
    struct timespec n = adk::uniform_clock::current();

    {
        struct timespec f
        {
            n.tv_sec + 11, n.tv_nsec
        };

        struct timespec d = adk::uniform_clock::how_long_from_now(f);
        std::cout << d.tv_sec << " " << d.tv_nsec << std::endl;

        BOOST_REQUIRE_GE(d.tv_sec, 10);
    }

    {
        struct timespec f, d;
        f = n;
        d = adk::uniform_clock::diff(n, f);
        BOOST_REQUIRE_EQUAL(d.tv_sec, 0);
        BOOST_REQUIRE_EQUAL(d.tv_nsec, 0);

        f = {n.tv_sec + 1, n.tv_nsec + 1};
        d = adk::uniform_clock::diff(n, f);
        BOOST_REQUIRE_EQUAL(d.tv_sec, 1);
        BOOST_REQUIRE_EQUAL(d.tv_nsec, 1);
    }
}