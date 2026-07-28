#define BOOST_TEST_MODULE TokenBucket

#include <adk_pack/error_code.h>
#include <adk_pack/arch/generic.h>
#include <adk_pack/token_buckets.h>
#include <boost/test/included/unit_test.hpp>

#include <malloc.h>

#include <iostream>
#include <thread>

#include <boost/format.hpp>

using namespace adk;

constexpr uint32_t kTestThreadNum = 3;
constexpr uint32_t kRateBaseUnit = 1000000;
constexpr uint64_t kTestLoopNumber = 10000000;

void Stream(uint64_t* byte_counter, adk::TokenBucket* token_bucket, uint64_t test_num)
{
    while (test_num)
    {
        if (adk::ErrorCode::kSuccess == token_bucket->TryAcquire(1))
        {
            (*byte_counter) += 1;
            --test_num;
        }
        else
        {
            ADK_PAUSE();
        }
    }
}

void RateStream(uint64_t* byte_counter, ConstRateCtrl* rate_ctrl, uint64_t test_num)
{
    while (test_num)
    {
        if (adk::ErrorCode::kSuccess == rate_ctrl->TryAcquire(1))
        {
            (*byte_counter) += 1;
            --test_num;
        }
        else
        {
            ADK_PAUSE();
        }
    }
}

BOOST_AUTO_TEST_SUITE(TokenBucket)

BOOST_AUTO_TEST_CASE(TokenTest)
{
    volatile bool is_running = false;
    std::thread thread_hdl[kTestThreadNum];
    volatile uint64_t* volatile counters[kTestThreadNum] = {0};
    for (uint32_t index = 0; index < kTestThreadNum; ++index)
    {   
        thread_hdl[index] = std::thread([index, &counters, &is_running]() {
            auto* const tb = RateControl::GetInstance<rate_unit::Second>(static_cast<double>((index + 1) * kRateBaseUnit));
            BOOST_REQUIRE(tb);

            volatile uint64_t counter = 0;
            counters[index] = &counter;

            while (!is_running);
            while (is_running)
            {
                if (adk::ErrorCode::kSuccess == tb->TryAcquire(1))
                {
                    ++counter;
                }
                else
                {
                    usleep(0);
                }
            }

            tb->Release();
        });
    }
    
    while (true)
    {
        bool is_all_init = true;
        for (uint32_t index = 0; index < kTestThreadNum; ++index)
        {
            if (nullptr == counters[index])
            {
                is_all_init = false;
                break;
            }
        }

        if (is_all_init)
        {
            break;
        }

        usleep(0);
    }

    boost::format fmt;
    is_running = true;
    uint64_t counters_rec[kTestThreadNum] = {0};
    for (uint32_t loop_index = 0; loop_index < 10; ++loop_index)
    {
        struct timespec current_time1;
        struct timespec current_time2;
        clock_gettime(CLOCK_MONOTONIC_RAW, &current_time1);
        sleep(1);
        clock_gettime(CLOCK_MONOTONIC_RAW, &current_time2);

        const uint64_t time_diff = current_time2.tv_nsec + current_time2.tv_sec * 1000000000
                                 - current_time1.tv_nsec - current_time1.tv_sec * 1000000000;
        std::cout << "time diff = " << time_diff << std::endl;
        for (uint32_t index = 0; index < kTestThreadNum; ++index)
        {
            const uint64_t counter = *(counters[index]);            
            const uint64_t diff = counter - counters_rec[index];
            counters_rec[index] = counter;
            
            const uint64_t expect_rate = (index + 1) * kRateBaseUnit;
            
            fmt = boost::format("Thread %1% token diff = %2% expect = %3%") 
                                % index
                                % diff 
                                % expect_rate;
                                
            std::cout << fmt.str() << std::endl;
            
            // check rate
            BOOST_REQUIRE_GT(diff, static_cast<uint64_t>(static_cast<double>(expect_rate) * 0.85));
            BOOST_REQUIRE_LT(diff, static_cast<uint64_t>(static_cast<double>(expect_rate) * 1.15));
        }
    }

    is_running = false;
    for (uint32_t index = 0; index < kTestThreadNum; ++index)
    {
        thread_hdl[index].join();
    }
}

BOOST_AUTO_TEST_CASE(TokenTestV2)
{
    volatile bool is_running = false;
    std::thread thread_hdl[kTestThreadNum];
    volatile uint64_t* volatile counters[kTestThreadNum] = {0};

    auto* const tb = RateControl::GetInstance<rate_unit::Second>(static_cast<double>(kRateBaseUnit));
    BOOST_REQUIRE(tb);
    
    for (uint32_t index = 0; index < kTestThreadNum; ++index)
    {
        thread_hdl[index] = std::thread([index, tb, &counters, &is_running]() {
            volatile uint64_t counter = 0;
            counters[index] = &counter;
            
            while (!is_running);
            
            while (is_running)
            {
                if (adk::ErrorCode::kSuccess == tb->TryAcquire(1))
                {
                    ++counter;
                }
                else
                {
                    usleep(0);
                }
            }
        });
    }
    
    while (true)
    {
        bool is_all_init = true;
        for (uint32_t index = 0; index < kTestThreadNum; ++index)
        {
            if (nullptr == counters[index])
            {
                is_all_init = false;
                break;
            }
        }

        if (is_all_init)
        {
            break;
        }

        usleep(0);
    }

    boost::format fmt;
    is_running = true;
    uint64_t counters_rec[kTestThreadNum] = {0};
    for (uint32_t loop_index = 0; loop_index < 10; ++loop_index)
    {
        sleep(1);
        uint64_t diff_total = 0;
        for (uint32_t index = 0; index < kTestThreadNum; ++index)
        {
            const uint64_t counter = *(counters[index]);            
            const uint64_t diff = counter - counters_rec[index];
            counters_rec[index] = counter;
            diff_total += diff;
        }
        
        const uint64_t expect_rate = kRateBaseUnit;
            
        fmt = boost::format("Token diff = %1% expect = %2%") 
                            % diff_total 
                            % expect_rate;
                            
        std::cout << fmt.str() << std::endl;
        
        // check rate
        BOOST_REQUIRE_GT(diff_total, static_cast<uint64_t>(static_cast<double>(expect_rate) * 0.85));
        BOOST_REQUIRE_LT(diff_total, static_cast<uint64_t>(static_cast<double>(expect_rate) * 1.15));
    }

    is_running = false;
    for (uint32_t index = 0; index < kTestThreadNum; ++index)
    {
        thread_hdl[index].join();
    }
    
    tb->Release();
}

BOOST_AUTO_TEST_CASE(RateCtrl)
{
    std::cout << "Test RateControl" << std::endl;
    char* test_buffer = (char*)memalign(4096, kTestThreadNum * 64);
    assert(test_buffer);

    uint64_t* byte_counter[kTestThreadNum];
    std::thread thread_hdl[kTestThreadNum];
    for (uint32_t index = 0; index < kTestThreadNum; ++index)
    {
        ConstRateCtrl* const tb_temp =
            RateControl::GetInstance<rate_unit::Microsecond>((uint32_t)1000 * (index + 1), (uint32_t)1024);
        BOOST_REQUIRE(tb_temp);
        uint64_t* counter = (uint64_t*)(test_buffer + index * 64);
        *counter = 0;
        byte_counter[index] = counter;
        thread_hdl[index] = std::thread(RateStream, counter, tb_temp, kTestLoopNumber);
    };

    // boost::format fmt;
    // uint64_t total_cnt = 0;
    // uint64_t record_counter[kTestThreadNum] = { 0 };
    // while (total_cnt < kTestLoopNumber * kTestThreadNum)
    // {
    //     total_cnt = 0;
    //     sleep(1);

    //     for (uint32_t index = 0; index < kTestThreadNum; ++index)
    //     {
    //         const uint64_t counter = *(byte_counter[index]);
    //         const uint64_t diff = counter - record_counter[index];
    //         const uint64_t expect_rate = 1000000 * (index + 1);
    //         total_cnt += counter;
    //         fmt = boost::format("Thread %1% token diff = %2% expect = %3%") % index
    //             % diff % expect_rate;
    //         record_counter[index] = counter;
    //         std::cout << fmt.str() << std::endl;
    //     }
    // }

    for (uint32_t thread_index=0; thread_index<kTestThreadNum; ++thread_index)
    {
        thread_hdl[thread_index].join();
    }
    free(test_buffer);
}

BOOST_AUTO_TEST_SUITE_END();
