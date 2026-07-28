#define BOOST_TEST_MODULE adk_logger
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>
#include <thread>

#include <adk_pack/logger.h>

#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>

/**
 * @brief 无锁队列的创建接口测试
 * 
 */
BOOST_AUTO_TEST_CASE(adk_Logger)
{
    std::vector<std::thread> threads_vec;
    std::cout << "timestring:" << adk::log::Logger::TimeString() << std::endl;

    for (int i = 0; i < 10; ++i)
    {
        threads_vec.push_back(std::thread([] {
            int i = 20;
            while (--i)
            {
                std::string str = adk::log::Logger::TimeString();
                BOOST_REQUIRE_MESSAGE(str.size() != 0,
                                      std::string("invalid timestring:") + str + "\n");

                adk::log::Logger::ConsoleLog(2,
                                             300101,
                                             "LoggerTest",
                                             "BOOST_AUTO_TEST_CASE",
                                             __LINE__,
                                             "log title",
                                             "log message");
                sleep(1);
            }
        }));
    }

    // sleep(20);
    for (auto& thrd : threads_vec)
    {
        if (thrd.joinable())
            thrd.join();
    }
}
