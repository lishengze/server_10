#define BOOST_TEST_MODULE rw_lock_pack

#include "adk_pack/entry_wrapper.h"
#include "adk_pack/rw_lock.h"
#include "adk_pack/thread.h"
#include <boost/test/included/unit_test.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <thread>

/**
 *  验证用的数据结构, Inc操作用来更改成员
 *  多线程同步成功的情况下 a_,b_,c_始终满足1:2:3的关系; 否则不满足, Check会失败;
 */
struct A
{
    A() = default;

    void Inc()
    {
        a_ += 1;
        b_ += 2;
        c_ += 3;
    }
    void Check() const
    {
        if(b_ / a_ != 2)
        {
            BOOST_REQUIRE(false);
        }
        if(c_ / a_ != 3)
        {
            BOOST_REQUIRE(false);
        }
    }
    void Print() const
    {
        std::cout << a_ << "," << b_ << "," << c_ << std::endl;
    }
    uint64_t a_{1};
    uint64_t b_{2};
    uint64_t c_{3};
};

// 临界区, 需要用读写锁保护;
A g_A; 

// 每个线程的操作次数(或读或写) 
constexpr static int total_times = 1000000;

// 读线程的数量
constexpr static int read_thread_num = 10;

// 写线程的数量
constexpr static int write_thread_num = 5;

// 每个读线程的total_times次读里面有这么多升级为写锁
constexpr static int to_update_num = 7; 

// 每个写线程的total_times次写里面有这么多降级为读锁
constexpr static int to_decay_num = 10; 

void test_write_to(adk::RWLock& lock)
{
    int count   = 0;
    while (count++ < total_times)
    {
        lock.lock_w();

        // 选出 to_decay_num 个写锁降级成读锁
        if (count % (total_times / to_decay_num))
        {
            g_A.Inc();
            lock.unlock_w();
        }
        else
        {
            // 降级成读锁
            lock.wlock_decay_r(); 
            A temp = g_A;
            lock.unlock_r();
            temp.Check();
        }
    }
}

void test_read_from_then_assert(adk::RWLock& lock)
{
    int count   = 0;
    while (count++ < total_times)
    {
        // 加上读锁
        lock.lock_r();
        // 选出 to_update_num 个读锁升级成写锁
        if (count % (total_times / to_update_num))
        {
            A temp = g_A;
            lock.unlock_r();
            temp.Check();
        }
        else
        {
            // 升级成写锁
            lock.rlock_upgrade_w(); 
            g_A.Inc();
            lock.unlock_w();
        }
    }
}

BOOST_AUTO_TEST_CASE(rw_lock)
{
    adk::RWLock lock;

    std::vector<std::thread> thread_vec;

    for (int y = 0; y < read_thread_num; y++)
    {
        std::string thread_name = "test_read_thread:" + std::to_string(y);
        thread_vec.emplace_back(adk::std_thread(thread_name.c_str(), thread_name.c_str(), std::bind(test_read_from_then_assert, std::ref(lock))));
    }

    for (int x = 0; x < write_thread_num; x++)
    {
        std::string thread_name = "test_write_thread:" + std::to_string(x);
        thread_vec.emplace_back(adk::std_thread(thread_name.c_str(), thread_name.c_str(), std::bind(test_write_to, std::ref(lock))));
    }

    for (auto& th : thread_vec)
    {
        if (th.joinable())
        {
            th.join();
        }
    }

    g_A.Check();

    /**
     * g_A.a_的初始值是1, 每个写线程会写 total_times - to_decay_num 次(其中to_decay_num次会降级为读锁不写), 每个读线程会写 to_update_num 次
     */
    uint64_t expected_a = 1 + (total_times - to_decay_num) * (uint64_t)write_thread_num + 
                            (uint64_t)read_thread_num * to_update_num;

    BOOST_REQUIRE(g_A.a_ == expected_a);

}
