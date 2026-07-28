#define BOOST_TEST_MODULE record_msg_binary_v2

#include "adk/arch/synchronize.h"
#include <adk/record_msg_binary.h>
#include <adk/record_msg_binary_v2.h>
#include <boost/exception/all.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/test/included/unit_test.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <time.h>

// using namespace adk;

adk_impl::LightWeightSpinLock g_lock;
volatile bool g_begin_consume_msg = false;

const int to_run_times = 8000;

//消息最大设置为2KB
const static uint64_t kMaxDataLen = 1024 * 2;


uint64_t get_ns()
{
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}


inline void MakeContent(uint64_t data_len, std::string& rs, uint64_t& hash_int)
{  // 制造一个随机的字符串, 并计算出它的哈希值
    char temp[kMaxDataLen];
    int fd = open("/dev/random", O_RDONLY, 0755);
    if (fd < 0)
    {
        BOOST_REQUIRE(false);
    }
    read(fd, temp, data_len);
    close(fd);
    rs       = std::move(std::string(temp, data_len));
    hash_int = std::hash<std::string>()(rs);
}

inline void MakeContentRandomLen(uint64_t len_min, uint64_t len_max, std::string& rs, uint64_t& hash_int)
{
    len_max = std::min(len_max, kMaxDataLen);
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    srandom((uint64_t)t.tv_sec * 1000000000 + t.tv_nsec);
    uint64_t data_len = len_min + random() % (len_max - len_min);
    //std::cout << "L44:" << data_len << "|" << std::endl;
    MakeContent(data_len, rs, hash_int);
}

struct TestItem
{
    uint64_t hash_;
    uint64_t used_len_;
    char data_[kMaxDataLen];

    uint64_t RealSize() const
    {
        return sizeof(TestItem) - (kMaxDataLen - used_len_);
    }

    void Init(std::string s, uint64_t h)
    {
        memcpy(data_, s.c_str(), s.length());
        used_len_ = s.length();
        hash_     = h;
    }

    TestItem()                = default;
    TestItem(const TestItem&) = default;
    TestItem(TestItem&&)      = default;
    TestItem& operator=(const TestItem&) = default;
    TestItem& operator=(TestItem&&) = default;
    ~TestItem()                     = default;

    bool CheckContent() const
    {
        //std::cout << "L75:" << used_len_ << "|" << std::endl;
        std::string s(data_, used_len_);
        auto carried_hash    = hash_;
        auto calculated_hash = std::hash<std::string>()(s);
        //std::cout << "carried_hash:" << carried_hash << ",calculated_hash:" << calculated_hash << std::endl;
        return carried_hash == calculated_hash;
    }
};

std::string serialize_func_data_0(const void* data, uint32_t len)
{
    return std::string((const char*)data, len);
}

std::string serialize_func_data_1(const void* data, uint32_t len)
{
    static int c = 0;
    c++;
    std::string result((const char*)data, len);
    auto int_result = atoi(result.c_str());
    // 消费者取出msg的时候是保序的
    //std::cout << c << "|" << int_result << std::endl;
    BOOST_REQUIRE(c == int_result);
    return "v2_" + result;
}

std::string serialize_func_data_2(const void* data, uint32_t len)
{
    TestItem* p_ti = (TestItem*)(data);
    auto ret       = p_ti->CheckContent();
    if(!ret)
    {
        BOOST_REQUIRE(false);
    }
    //std::cout << "L98:" << p_ti->used_len_ << "|" << std::endl;
    return std::string(p_ti->data_, p_ti->used_len_);
}

void thread_do_1(adk_impl::RecordMsgBinaryV2& recorder, std::atomic_int& times, int thread_index)
{
    // 多个线程随机按序插入消息
    while (true)
    {
        {
            std::lock_guard<adk_impl::LightWeightSpinLock> lg(g_lock);
            times++;
            if (times >= to_run_times)
            {
                break;
            }
            std::string info = std::to_string(times);
            char* buf = (char*)recorder.AllocBuffer(info.size());
            memcpy(buf, info.c_str(), info.size());
            recorder.PostBuffer(buf);
            //recorder.PutMsg(info.c_str(), info.size());
        }
        std::this_thread::sleep_for(std::chrono::microseconds(1));  // 让其他线程执行
    }
}

void thread_do_2(adk_impl::RecordMsgBinaryV2& recorder, int total_times, int thread_index)
{
    int t = 0;
    while (t++ <= total_times)
    {
        std::string s;
        uint64_t h = 0;
        MakeContentRandomLen(20, kMaxDataLen - 1, s, h);
        TestItem ti;
        ti.Init(s, h);
        std::string s1;
        auto ret = recorder.PutMsg((const char*)&ti, ti.RealSize(), 0, &s1);
        if (ret != adk_impl::ErrorCode::kSuccess)
        {
            std::cout << "errmsg:"  << s1 << std::endl;
            BOOST_REQUIRE(false);
        }

        std::this_thread::sleep_for(std::chrono::microseconds(1));  // 让其他线程执行
    }
}

BOOST_AUTO_TEST_CASE(Test_V2_Function_MT)
{  // 新的recorder, 多线程生产
    adk_impl::RecordMsgBinaryV2 recorder;
    std::string local_file_name = "./record3.txt";
    unlink(local_file_name.c_str());  //先清空文件

    // 初始化, 传入落盘的文件名;
    std::string s;
    bool init_ret = recorder.Init(local_file_name, &s);
    BOOST_REQUIRE(init_ret == true);

    // 设置反序列化回调函数
    recorder.SetSerializeFunc(serialize_func_data_1);

    // 启动后台的消费线程开始消费
    auto ret = recorder.Start();
    BOOST_REQUIRE(ret == adk_impl::ErrorCode::kSuccess);

    std::atomic_int times {0};
    std::vector<std::thread> tv;

    for (int i = 0; i < 3; i++)
    {  // 启动多个线程来做生产
        std::string thread_name = "producer:" + std::to_string(i);
        tv.emplace_back(std_thread("Test_V2_Function_MT", thread_name.c_str(), std::bind(thread_do_1, std::ref(recorder), std::ref(times), i)));
    }

    for (auto& t : tv)
    {
        if (t.joinable())
        {
            t.join();
        }
    }

    sleep(1);  // 等待后台消费线程落盘完毕
    recorder.Stop();
    unlink(local_file_name.c_str());
}

/**
 * @brief 压力测试需要大概30s, 写文件
*/
BOOST_AUTO_TEST_CASE(Test_V2_Function_Stress)
{  // 新的recorder, 多线程生产
    adk_impl::RecordMsgBinaryV2 recorder;
    std::string local_file_name = "./record4.txt";
    unlink(local_file_name.c_str());  //先清空文件

    // 初始化, 传入落盘的文件名;
    std::string s;
    bool init_ret = recorder.Init(local_file_name, &s);
    BOOST_REQUIRE(init_ret == true);

    // 设置反序列化回调函数
    recorder.SetSerializeFunc(serialize_func_data_2);

    // 启动后台的消费线程开始消费
    auto ret = recorder.Start();
    BOOST_REQUIRE(ret == adk_impl::ErrorCode::kSuccess);

    std::atomic_int times {0};
    std::vector<std::thread> tv;

    for (int i = 0; i < 10; i++)
    {  // 启动多个线程来做生产
        std::string thread_name = "producer:" + std::to_string(i);
        tv.emplace_back(std_thread("Test_V2_Function_Stress", thread_name.c_str(), std::bind(thread_do_2, std::ref(recorder), 80000, i)));
    }

    for (auto& t : tv)
    {
        if (t.joinable())
        {
            t.join();
        }
    }

    sleep(1);  // 等待后台消费线程落盘完毕
    recorder.Stop();
    unlink(local_file_name.c_str());
}


std::string GetInfo0(std::string name, uint32_t age)
{
    std::string result = "CallInfo0:" + name + ",age:" + std::to_string(age);
    return result;
}

std::string GetInfo1(std::string name, uint32_t age)
{
    std::string result = "CallInfo1:" + name + ",age:" + std::to_string(age);
    return result;
}

struct TestStuct
{
    uint32_t age;
    std::string name;
    std::string (*cb)(std::string, uint32_t);
};

std::string serialize_func_data_3(const void* data, uint32_t len)
{
    BOOST_REQUIRE(8 == len);
    auto* p_TestStuct = *((TestStuct**)(data));
    auto ret =  (p_TestStuct->cb)(p_TestStuct->name, p_TestStuct->age);
    delete p_TestStuct;
    return ret;
}


void thread_do_3(adk_impl::RecordMsgBinaryV2& recorder, int total_times, int thread_index)
{
    int t = 0;
    while (t++ <= total_times)
    {
        std::string s1;
        TestStuct* p_ts = new TestStuct;
        p_ts->age       = t;
        p_ts->name      = std::to_string(pthread_self());
        p_ts->cb        = (t % 2) ? &GetInfo0 : &GetInfo1;
        // auto ret        = recorder.PutMsg(&p_ts, 8, 0, &s1);
        auto* buf = recorder.AllocBuffer(8);
        *((TestStuct**)buf) = p_ts;
        recorder.PostBuffer(buf);
        // if (ret != adk_impl::ErrorCode::kSuccess)
        // {
        //     std::cout << "errmsg: " << s1 << std::endl;
        //     BOOST_REQUIRE(false);
        // }

        std::this_thread::sleep_for(std::chrono::microseconds(1));  // 让其他线程执行
    }
}

/**
 * @brief  测试测试携带回调函数的msg
*/
BOOST_AUTO_TEST_CASE(Test_V2_Function_msg_cb)
{
    adk_impl::RecordMsgBinaryV2 recorder;
    std::string local_file_name = "./record5.txt";
    unlink(local_file_name.c_str());  //先清空文件

    // 初始化, 传入落盘的文件名;
    std::string s;
    bool init_ret = recorder.Init(local_file_name, &s);
    BOOST_REQUIRE(init_ret == true);

    // 设置反序列化回调函数
    recorder.SetSerializeFunc(serialize_func_data_3);

    // 启动后台的消费线程开始消费
    auto ret = recorder.Start();
    BOOST_REQUIRE(ret == adk_impl::ErrorCode::kSuccess);

    std::atomic_int times {0};
    std::vector<std::thread> tv;

    for (int i = 0; i < 3; i++)
    {  // 启动多个线程来做生产
        std::string thread_name = "producer:" + std::to_string(i);
        tv.emplace_back(std_thread("Test_V2_Function_msg_cb", 
                                    thread_name.c_str(), 
                                    std::bind(thread_do_3, std::ref(recorder), 800000, i)
                                )
        );
    }

    for (auto& t : tv)
    {
        if (t.joinable())
        {
            t.join();
        }
    }

    sleep(1);  // 等待后台消费线程落盘完毕
    recorder.Stop();
    unlink(local_file_name.c_str());
}

uint64_t total_used_time = 0;
uint64_t total_run_time = 0;

std::mutex lock;

void thread_do_4(adk_impl::RecordMsgBinaryV2& recorder, int total_times, int queue_index)
{
    int t = 0;
    while (t++ <= total_times)
    {
        std::string s1('x', 200);
        // uint64_t h = 0;
        // MakeContentRandomLen(100, 100, s1, h);        
        auto ret        = recorder.PutMsg(s1.c_str(), s1.length(), queue_index, &s1);
        if (ret != adk_impl::ErrorCode::kSuccess)
        {
            std::cout << "errmsg: " << s1 << std::endl;
            BOOST_REQUIRE(false);
        }
        // std::this_thread::sleep_for(std::chrono::microseconds(1));  // 让其他线程执行
    }
}


void run_multi_queue_test(int queue_num)
{

    uint64_t total_times = 8000;
    adk_impl::RecordMsgBinaryV2 recorder{uint8_t(queue_num)};
    std::string local_file_name = "./record6.txt";
    unlink(local_file_name.c_str());  //先清空文件

    // 初始化, 传入落盘的文件名;
    std::string s;
    bool init_ret = recorder.Init(local_file_name, &s);
    BOOST_REQUIRE(init_ret == true);

    // 设置反序列化回调函数
    recorder.SetSerializeFunc(serialize_func_data_0);

    // 启动后台的消费线程开始消费
    auto ret = recorder.Start();
    BOOST_REQUIRE(ret == adk_impl::ErrorCode::kSuccess);

    std::atomic_int times {0};
    std::vector<std::thread> tv;

    // 启动四个线程来写
    int thread_num = 4;
    for (int i = 0; i < thread_num; i++)
    {  
        // 启动多个线程来做生产, 一个线程用一个队列
        std::string thread_name = "producer:" + std::to_string(i);
        tv.emplace_back(std_thread("Test_V2_Function_multi_queue", 
                                    thread_name.c_str(), 
                                    std::bind(thread_do_4, std::ref(recorder), total_times / thread_num, i % queue_num)
                                )
        );
    }

    for (auto& t : tv)
    {
        if (t.joinable())
        {
            t.join();
        }
    }

    //std::cout << "queue_num:" << queue_num << ", avg used time: " << (float)(get_ns() - beg_ns) / total_times  << std::endl;

    sleep(1);  // 等待后台消费线程落盘完毕
    recorder.Stop();
    unlink(local_file_name.c_str());
}

/**
 * @brief  测试多队列的IO文件
*/
BOOST_AUTO_TEST_CASE(Test_V2_Function_multi_queue)
{
    run_multi_queue_test(1);
    run_multi_queue_test(4);
}


struct TTT
{
    int x;
    int y;
    int z;
    char i;
    TTT(): x(0), y(0), z(0), i(0)
    {
    }
};


std::string serialize_func_data_4(const void* data, uint32_t len)
{
    TTT* t = (TTT*)data;
    BOOST_REQUIRE(len == sizeof(TTT));
    std::string ret = "Out_: x is: " + std::to_string(t->x) + 
                    ", y is:" + std::to_string(t->y) +
                    ", z is:" + std::to_string(t->z) +
                    ", i is:" + std::to_string(t->i) + ".";
    //std::cout << ret <<std::endl;
    return ret;
}


BOOST_AUTO_TEST_CASE(Test_V2_AllocBufferT)
{
    adk_impl::RecordMsgBinaryV2 recorder;
    std::string local_file_name = "./record7.txt";
    unlink(local_file_name.c_str());  //先清空文件

    // 初始化, 传入落盘的文件名;
    std::string s;
    bool init_ret = recorder.Init(local_file_name, &s);
    BOOST_REQUIRE(init_ret == true);

    // 设置反序列化回调函数
    recorder.SetSerializeFunc(serialize_func_data_4);


    // 启动后台的消费线程开始消费
    recorder.Start();

    for (int i = 0; i < 100; i++)
    {
        TTT& t = recorder.AllocBuffer<TTT>();
        t.x = i;
        t.y = i * 10;
        t.z = i * 100;
        t.i = 'a';
        recorder.PostBuffer(&t);
    }
    sleep(2);
    recorder.Stop();
    unlink(local_file_name.c_str());
}

std::string serialize_func_data_5(const void* data, uint32_t len)
{
    static uint32_t total_msg_num = 100;

    if (!g_begin_consume_msg)
    {
        g_lock.lock();
        g_begin_consume_msg = true;
    }

    --total_msg_num;
    if (total_msg_num == 0)
    {
        g_lock.unlock();
    }

    return std::string((const char*)data, len);
}

BOOST_AUTO_TEST_CASE(Test_V2_QueueLength)
{
    const char* format_str = "queue_%d"; // 8 Bytes
    adk_impl::RecordMsgBinaryV2 recorder(5, 8 * 100);
    std::string local_file_name = "./record8.txt";
    unlink(local_file_name.c_str());

    std::string err_msg;
    bool init_ret = recorder.Init(local_file_name, &err_msg);
    BOOST_REQUIRE(init_ret == true);

    recorder.SetSerializeFunc(serialize_func_data_5);

    // 主线程获取锁，recorder的序列化函数内无法获取锁，进而阻塞
    g_lock.lock();

    recorder.Start();

    for (uint32_t i = 0; i < 100; ++i)
    {
        uint32_t queue_index = i % 5;
        void* queue_msg = recorder.AllocBuffer(8, queue_index);
        memset(queue_msg, 0, 8);
        sprintf((char*)queue_msg, format_str, queue_index);
        recorder.PostBuffer(queue_msg, queue_index);
    }

    sleep(5);

    boost::property_tree::ptree indicator_pt;
    recorder.CollectIndicator(indicator_pt);
    {
        boost::property_tree::ptree& queue_indi_pt = indicator_pt.get_child("RecordMsgBinaryV2");
        for (auto& queue_pt : queue_indi_pt)
        {
            auto& item = queue_pt.second;
            boost::property_tree::ptree::const_assoc_iterator it1 = item.find("consume_nr");
            boost::property_tree::ptree::const_assoc_iterator it2 = item.find("produce_nr");

            BOOST_REQUIRE(it1 != item.not_found());
            BOOST_REQUIRE(it2 != item.not_found());

            BOOST_REQUIRE(it1->second.data() == "0");
            BOOST_REQUIRE(it2->second.data() == "20");
        }
    }

    // 解锁 让recorder能正常消费消息，recorder会锁住该锁，直至处理完所有消息
    g_lock.unlock();

    while (!g_begin_consume_msg)
    {
        sleep(1);
    }

    // 重新获取到锁后表示recorder已消费完所有消息
    g_lock.lock();
    sleep(2);

    indicator_pt.clear();
    recorder.CollectIndicator(indicator_pt);

    {
        boost::property_tree::ptree& queue_indi_pt = indicator_pt.get_child("RecordMsgBinaryV2");
        for (auto& queue_pt : queue_indi_pt)
        {
            auto& item = queue_pt.second;
            boost::property_tree::ptree::const_assoc_iterator it1 = item.find("consume_nr");
            boost::property_tree::ptree::const_assoc_iterator it2 = item.find("produce_nr");

            BOOST_REQUIRE(it1 != item.not_found());
            BOOST_REQUIRE(it2 != item.not_found());

            BOOST_REQUIRE(it1->second.data() == "20");
            BOOST_REQUIRE(it2->second.data() == "20");
        }
    }


    recorder.Stop();
    unlink(local_file_name.c_str());
}