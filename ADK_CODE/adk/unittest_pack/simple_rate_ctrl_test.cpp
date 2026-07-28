#define BOOST_TEST_MODULE simple_rate_ctrl
#include <boost/test/included/unit_test.hpp>

#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

#include <adk_pack/simple_rate_controller.h>
#include <unistd.h>
#include <stdlib.h>

volatile bool g_is_test_start = false;

void Run(int32_t rate, uint32_t& cnt)
{
    adk::SimpleRateCtrl controller(rate);
    int i = 0;
    for (i = 0; i < 1000; ++i)
    {
        controller.Wait();
        ++cnt;
    }
}


BOOST_AUTO_TEST_CASE(test_Random)
{
    // Following test requires accurate usleep. disabled in test pipeline.
    bool is_ignore = false;
    if (::getenv("AMI_DOCKER_UNIT_TEST") != nullptr)
        is_ignore = true;
    #define TEST_ITER 2000000
    uint32_t counter1 = 0;
    uint32_t counter2 = 0;
    // 1000/s => 1 ms
    boost::thread r1 = boost::thread(boost::bind(Run, 1000, boost::ref(counter1)));
    // 500/s => 2 ms
    boost::thread r2 = boost::thread(boost::bind(Run, 500, boost::ref(counter2)));
    
    // first wait
    usleep(100);
    if(!is_ignore)
    {
        BOOST_CHECK_EQUAL(counter1, 0);
        BOOST_CHECK_EQUAL(counter2, 0);
    }

    usleep(1000);
    if(!is_ignore)
    {
        // r1 wait finish
        BOOST_CHECK_EQUAL(counter1, 1);
        // r2 still waitting
        BOOST_CHECK_EQUAL(counter2, 0);
    }
    
    sleep(1);
    if(!is_ignore)
    {
        // r1 finished
        BOOST_CHECK_EQUAL(counter1, 1000);
        sleep(1);
        // r2 finished
        BOOST_CHECK_EQUAL(counter2, 1000);
    }

    r1.join();
    r2.join();
    
}

