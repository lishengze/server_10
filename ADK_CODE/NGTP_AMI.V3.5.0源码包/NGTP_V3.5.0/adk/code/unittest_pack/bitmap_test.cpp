#define BOOST_TEST_MODULE test_bitmap
// #define BOOST_TEST_ALTERNATIVE_INIT_API

#include <iostream>
#include <unistd.h>
#include <stdlib.h>
#include <vector>

//#include <boost/test/unit_test.hpp>
#include <boost/test/included/unit_test.hpp>
#include <boost/smart_ptr.hpp>

#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>

#include <adk_pack/bitmap.h>

// 2的n次方简易算法
uint64_t MyTwoPow(int n)
{
    uint64_t value = 1;
    for(int i = 0; i < n; ++i)
    {
        value <<= 1;
    }
    return value;
}

// 1、测试接口功能套件
BOOST_AUTO_TEST_SUITE(s_bitmap_functional)

// 测试Create接口
BOOST_AUTO_TEST_CASE(t_create)
{
    BOOST_TEST_MESSAGE("test_bitmap_functional: -------------------");
    std::vector<boost::shared_ptr<adk::BitMap>> v_ptr;
    // std::vector<boost::scoped_ptr<adk::BitMap>> v_ptr;

    // BitMap的空间大小限定为2^(8~32)位，则对该范围内并且越界取值进行Create接口测试
    for(uint32_t itor = 0; itor <= 34; ++itor)
    {
        v_ptr.emplace_back(boost::shared_ptr<adk::BitMap>((adk::BitMap::Create(itor)), adk::BitMap::Destroy));
        // adk::BitMap* obj_ptr = adk::BitMap::Create(10);
        std::size_t num = itor + 1;        
        BOOST_REQUIRE_EQUAL(v_ptr.size(), num);
        // BOOST_CHECK(obj_ptr != nullptr);
    }
    BOOST_TEST_MESSAGE("test_create DONE");
}


// 测试指定地址Create接口
BOOST_AUTO_TEST_CASE(t_addr_create)
{
    std::allocator<uint32_t> int_alloc;
    std::size_t num = 0;
    // BitMap的空间大小限定为2^(8~32)位，则对该范围内并且越界取值进行Create接口测试
    for(uint32_t itor = 0; itor <= 34; ++itor)
    {
        // 并对创建的BitMap的对象，进行简单操作进行测试
        // num = pow(2, (itor < 8)? 8 : itor -5);
        uint64_t maxlen = 0;
        if (itor < 8)
        {
            maxlen = MyTwoPow(8);
        }
        else if (itor > 32)
        {
            maxlen = MyTwoPow(32);
        }
        else
        {
            maxlen = MyTwoPow(itor);
        }
        
        // std::cout << num << std::endl;
        BOOST_CHECK_NE(maxlen, 0);

        auto ptr =  int_alloc.allocate(maxlen);
        BOOST_CHECK(ptr != nullptr);

        adk::BitMap* bitmap_ptr = adk::BitMap::Create(ptr, itor);
        BOOST_CHECK(bitmap_ptr != nullptr);

        uint64_t start = 0;
        // uint64_t maxlen = MyTwoPow(itor);
        bitmap_ptr->ClearRange(start, maxlen);

        uint64_t map_size = 10;
        bitmap_ptr->SetRange(start, map_size);
        for (auto pos = start; pos < map_size; ++pos)
        {
            BOOST_CHECK_NE(bitmap_ptr->Get(pos), 0);
        }

        for (uint64_t pos = map_size; pos < 100 ; ++pos)
        {
            BOOST_CHECK_EQUAL(bitmap_ptr->Get(pos), 0);
        }
        
        adk::BitMap::Destroy(bitmap_ptr);

        int_alloc.deallocate(static_cast<uint32_t*>(ptr), num);
        // bitmap_ptr->Get(0);
    }
    BOOST_TEST_MESSAGE("test_addr_create DONE");
}

// 测试set和clear接口
BOOST_AUTO_TEST_CASE(t_operator)
{
    adk::BitMap* bitmap_ptr = adk::BitMap::Create(21);
    uint64_t maxlen = MyTwoPow(21);
    // for (uint64_t pos = 0; pos < maxlen; ++pos)
    // {
        // BitMap对象内存空间未初始化，使用前需要clear
        // BOOST_CHECK_EQUAL(bitmap_ptr->Get(pos), 0);
    // }

    // BOOST_TEST_MESSAGE("clean初始化");
    bitmap_ptr->ClearRange(0, maxlen);
    for (uint64_t pos = 0; pos < maxlen; ++pos)
    {
        BOOST_CHECK_EQUAL(bitmap_ptr->Get(pos), 0);
    }
    
    // 测试set和clear接口
    for (uint64_t pos = 0; pos < maxlen; ++pos)
    {
        bitmap_ptr->Set(pos);
        BOOST_CHECK_NE(bitmap_ptr->Get(pos), 0);

        bitmap_ptr->ClearUnsafe(pos);
        BOOST_CHECK_EQUAL(bitmap_ptr->Get(pos), 0);
        
        bitmap_ptr->SetUnsafe(pos);
        BOOST_CHECK_NE(bitmap_ptr->Get(pos), 0);

        bitmap_ptr->Clear(pos);
        BOOST_CHECK_EQUAL(bitmap_ptr->Get(pos), 0);
    }
    BOOST_TEST_MESSAGE("test_operator DONE");

}

BOOST_AUTO_TEST_SUITE_END()



// 2、多线程测试套件
BOOST_AUTO_TEST_SUITE(s_thread)

/**
 * @brief               多线程函数，置位和清除操作，不同线程分别负责奇数偶数位
 * @param[in]  map_instptr          Bitmap对象的指针
 * @param[in]  len                  区间长度
 * @param[in]  set_or_clear         置位 true           clear false
 * @param[in]  flag                 负责偶数位 true      负责奇数位 false 
 * @param[in]  is_safe              是否使用线程安全的接口
 */
void test_is_safe(adk::BitMap* map_instptr, uint64_t len, bool set_or_clear, bool flag, bool is_safe)
{
    for (uint64_t pos = 0; pos < len; ++pos)
    {
        if(flag && ((pos & 1) == 0))
        {
            // BOOST_CHECK_EQUAL(pos & 1, 0);
            if (set_or_clear)
            {
                if(is_safe){ map_instptr->Set(pos); }
                else { map_instptr->SetUnsafe(pos); }
            }
            else
            {
                if(is_safe){ map_instptr->Clear(pos); }
                else { map_instptr->ClearUnsafe(pos); }
            }
        }
        if(!flag && ((pos & 1) == 1))
        {
            if (set_or_clear)
            {
                if(is_safe){ map_instptr->Set(pos); }
                else { map_instptr->SetUnsafe(pos); }
            }
            else
            {
                if(is_safe){ map_instptr->Clear(pos); }
                else { map_instptr->ClearUnsafe(pos); }
            }
        }
    } 
}

// 测试set和setUnsafe接口
BOOST_AUTO_TEST_CASE(t_thread_set)
{
    BOOST_TEST_MESSAGE("t_thread_set : -------------------");
    // BOOST_TEST_MESSAGE("t_thread_set\n");
    // set接口是线程安全的
    adk::BitMap* bitmap_ptr = adk::BitMap::Create(10);
    uint64_t maxlen = MyTwoPow(10);
    bitmap_ptr->ClearRange(0, maxlen);

    boost::thread ts_o = boost::thread(boost::bind(test_is_safe, bitmap_ptr, maxlen, true, true, true));
    boost::thread ts_a = boost::thread(boost::bind(test_is_safe, bitmap_ptr, maxlen, true, false, true));

    ts_a.join();
    ts_o.join();

    for (uint64_t pos = 0; pos < maxlen; ++pos)
    {
        BOOST_CHECK_MESSAGE(bitmap_ptr->Get(pos) != 0, pos);
    }


    // 当数据量足够大时，unsafe的接口在多线程则会出现资源竞争，不合预期的情况
    bitmap_ptr->ClearRange(0, maxlen);
    boost::thread ts_c = boost::thread(boost::bind(test_is_safe, bitmap_ptr, maxlen, true, true, false));
    boost::thread ts_d = boost::thread(boost::bind(test_is_safe, bitmap_ptr, maxlen, true, false, false));

    ts_c.join();
    ts_d.join();

    for (uint64_t pos = 0; pos < maxlen; ++pos)
    {
        BOOST_CHECK_MESSAGE(bitmap_ptr->Get(pos) != 0, pos);
    }
    BOOST_TEST_MESSAGE("test_thread_set DONE");
}

// 使用多线程进行置位和清除操作
BOOST_AUTO_TEST_CASE(t_thread_clear)
{
    // clear接口是线程安全的
    adk::BitMap* bitmap_ptr = adk::BitMap::Create(10);
    uint64_t maxlen = MyTwoPow(10);
    
    // 初始化后启动两个线程，一个将奇数位清除，另一个清偶数位
    bitmap_ptr->SetRange(0, maxlen);
    boost::thread ts_o = boost::thread(boost::bind(test_is_safe, bitmap_ptr, maxlen, false, true, true));
    boost::thread ts_a = boost::thread(boost::bind(test_is_safe, bitmap_ptr, maxlen, false, false, true));

    ts_a.join();
    ts_o.join();

    // 线程安全情况下，应已全部清除
    for (uint64_t pos = 0; pos < maxlen; ++pos)
    {
        BOOST_CHECK_MESSAGE(bitmap_ptr->Get(pos) == 0, pos);
    }


    // 当数据量足够大时，unsafe的接口在多线程则会出现资源竞争，不合预期的情况
    bitmap_ptr->SetRange(0, maxlen);
    boost::thread ts_c = boost::thread(boost::bind(test_is_safe, bitmap_ptr, maxlen, false, true, false));
    boost::thread ts_d = boost::thread(boost::bind(test_is_safe, bitmap_ptr, maxlen, false, false, false));

    ts_c.join();
    ts_d.join();

    for (uint64_t pos = 0; pos < maxlen; ++pos)
    {
        BOOST_CHECK_MESSAGE(bitmap_ptr->Get(pos) == 0, pos);
    }
    BOOST_TEST_MESSAGE("test_thread_clear DONE");

}

BOOST_AUTO_TEST_SUITE_END()



// 3、测试各个接口的耗时套件
BOOST_AUTO_TEST_SUITE(s_time)

BOOST_AUTO_TEST_CASE(t_elapsed)
{
    BOOST_TEST_MESSAGE("s_time: -------------------");

    adk::BitMap* bitmap_ptr = adk::BitMap::Create(15);
    uint64_t maxlen = MyTwoPow(15);
    struct timespec ts;
    struct timespec te;
    BOOST_TEST_MESSAGE("maxlen: ");
    BOOST_TEST_MESSAGE(maxlen);

    // SetRange接口区段置位，执行10000次取平均值
    clock_gettime(CLOCK_REALTIME, &ts);
    // usleep(10);
    for(int cnt = 0; cnt < 10000; ++cnt)
    {
        bitmap_ptr->SetRange(0, maxlen);
    }
    clock_gettime(CLOCK_REALTIME, &te);
    auto timediff = static_cast<float>((te.tv_sec - ts.tv_sec) * 1000000000 + (te.tv_nsec - ts.tv_nsec)) / 10000 ;
    BOOST_TEST_MESSAGE("\nSetRange time: ");
    BOOST_TEST_MESSAGE(timediff);

    // 调用Set循环置位，执行10000次取平均耗时
    clock_gettime(CLOCK_REALTIME, &ts);
    for (int cnt = 0; cnt < 10000; ++cnt)
    for (uint64_t pos = 0; pos < maxlen; ++pos)
    {
        bitmap_ptr->Set(pos);
    }
    clock_gettime(CLOCK_REALTIME, &te);
    timediff = static_cast<float>((te.tv_sec - ts.tv_sec) * 1000000000 + (te.tv_nsec - ts.tv_nsec)) / 10000;
    // std::cout << timediff << std::endl;
    BOOST_TEST_MESSAGE("\nSet time: ");
    BOOST_TEST_MESSAGE(timediff);
    // 对一个位的操作耗时
    BOOST_TEST_MESSAGE("Once Set time: ");
    BOOST_TEST_MESSAGE(timediff / maxlen);

    // 调用Setunsafe循环置位，执行10000次取平均耗时
    clock_gettime(CLOCK_REALTIME, &ts);
    for (int cnt = 0; cnt < 10000; ++cnt)
    for (uint64_t pos = 0; pos < maxlen; ++pos)
    {
        bitmap_ptr->SetUnsafe(pos);
    }
    clock_gettime(CLOCK_REALTIME, &te);
    timediff = (static_cast<float>(te.tv_sec - ts.tv_sec) * 1000000000 + (te.tv_nsec - ts.tv_nsec)) / 10000;
    // std::cout << timediff << std::endl;
    BOOST_TEST_MESSAGE("\nSetUnsafe time: ");
    BOOST_TEST_MESSAGE(timediff);
    BOOST_TEST_MESSAGE("Once SetUnsafe time: ");
    BOOST_TEST_MESSAGE(timediff / maxlen);

    // 调用clearRange循环置位，执行10000次取平均耗时
    clock_gettime(CLOCK_REALTIME, &ts);
    for (int cnt = 0; cnt < 10000; ++cnt)
    {
        bitmap_ptr->ClearRange(0, maxlen);
    }
    clock_gettime(CLOCK_REALTIME, &te);
    timediff = static_cast<float>((te.tv_sec - ts.tv_sec) * 1000000000 + (te.tv_nsec - ts.tv_nsec)) / 10000;
    BOOST_TEST_MESSAGE("\nClearRange time: ");
    BOOST_TEST_MESSAGE(timediff);
    BOOST_TEST_MESSAGE("Once Clear on Range time: ");
    BOOST_TEST_MESSAGE((float)timediff / maxlen);

    // 调用Clear循环置位，执行10000次取平均耗时
    clock_gettime(CLOCK_REALTIME, &ts);
    for (int cnt = 0; cnt < 10000; ++cnt)
    for (uint64_t pos = 0; pos < maxlen; ++pos)
    {
        bitmap_ptr->Clear(pos);
    }
    clock_gettime(CLOCK_REALTIME, &te);
    timediff = static_cast<float>((te.tv_sec - ts.tv_sec) * 1000000000 + (te.tv_nsec - ts.tv_nsec)) / 10000;
    BOOST_TEST_MESSAGE("\nClear time: ");
    BOOST_TEST_MESSAGE(timediff);
    BOOST_TEST_MESSAGE("Once Clear time: ");
    BOOST_TEST_MESSAGE(timediff / maxlen);

    // 调用ClearUnsafe 循环置位，执行10000次取平均耗时
    clock_gettime(CLOCK_REALTIME, &ts);
    for (int cnt = 0; cnt < 10000; ++cnt)
    for (uint64_t pos = 0; pos < maxlen; ++pos)
    {
        bitmap_ptr->ClearUnsafe(pos);
    }
    clock_gettime(CLOCK_REALTIME, &te);
    timediff = float((te.tv_sec - ts.tv_sec) * 1000000000 + (te.tv_nsec - ts.tv_nsec)) / 10000;
    BOOST_TEST_MESSAGE("\nClearUnsafe time: ");
    BOOST_TEST_MESSAGE(timediff);
    BOOST_TEST_MESSAGE("Once ClearUnsafe time: ");
    BOOST_TEST_MESSAGE(timediff / maxlen);


    // 调用 Get接口 循环置位，执行10000次取平均耗时
    clock_gettime(CLOCK_REALTIME, &ts);
    for (int cnt = 0; cnt < 10000; ++cnt)
    for (uint64_t pos = 0; pos < maxlen; ++pos)
    {
        bitmap_ptr->Get(pos);
    }
    clock_gettime(CLOCK_REALTIME, &te);
    timediff = static_cast<float>((te.tv_sec - ts.tv_sec) * 1000000000 + (te.tv_nsec - ts.tv_nsec)) / 10000;
    BOOST_TEST_MESSAGE("\nGet time: ");
    BOOST_TEST_MESSAGE(timediff);
    BOOST_TEST_MESSAGE("Once Get time: ");
    BOOST_TEST_MESSAGE(timediff / maxlen);

    BOOST_TEST_MESSAGE("\ntest_elapsed Done");
}

BOOST_AUTO_TEST_SUITE_END()
