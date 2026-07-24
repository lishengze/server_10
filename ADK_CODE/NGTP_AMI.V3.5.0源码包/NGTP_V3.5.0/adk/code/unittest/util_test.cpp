#define BOOST_TEST_MODULE util
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk/util.h>
#include <adk/error_code.h>

#include <set>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <thread>
#include <chrono>
#include <fstream>

#include <adk/json/json.hpp>
#include <adk/inotify_wrapper.h>

BOOST_AUTO_TEST_CASE(test_GetElementList)
{
    std::set<std::string> a = {"1", "2", "3"};
    BOOST_CHECK_EQUAL(adk::GetElementList(a), "[1, 2, 3]");

    std::vector<std::string> b = {"3", "2", "1"};
    BOOST_CHECK_EQUAL(adk::GetElementList(b), "[3, 2, 1]");

    std::set<std::string> c;
    BOOST_CHECK_EQUAL(adk::GetElementList(c), "");

    std::vector<std::string> d;
    BOOST_CHECK_EQUAL(adk::GetElementList(d), "");

    std::set<std::string> e = {"1"};
    BOOST_CHECK_EQUAL(adk::GetElementList(e), "[1]");

    std::vector<std::string> f = {"2"};
    BOOST_CHECK_EQUAL(adk::GetElementList(f), "[2]");

    std::map<std::string, int> map_a = { {"1", 1}, {"2", 2}, {"3", 3} };
    BOOST_CHECK_EQUAL(adk::GetElementList(map_a), "[1, 2, 3]");

    std::map<std::string, int> map_b;
    BOOST_CHECK_EQUAL(adk::GetElementList(map_b), "");

    std::map<std::string, int> map_c = { {"1", 1} };
    BOOST_CHECK_EQUAL(adk::GetElementList(map_c), "[1]");
}

static bool is_pass = false;
static void TestGetCpuAffinity()
{
    int32_t ec;
    std::string cpu_list;

    adk::SetCpuAffinity("3-4,6-7");
    ec = adk::GetCpuAffinity(cpu_list);
    if (ec != adk::ErrorCode::kSuccess)
    {
        is_pass = false;
        return;
    }

    if (cpu_list != "3,4,6,7")
    {
        is_pass = false;
        return;
    }

    is_pass = true;
}

BOOST_AUTO_TEST_CASE(test_GetCpuAffinity)
{
    int32_t ec;
    std::string cpu_list;

    adk::SetCpuAffinity("0");
    ec = adk::GetCpuAffinity(cpu_list);
    BOOST_CHECK_EQUAL(ec, adk::ErrorCode::kSuccess);
    BOOST_CHECK_EQUAL(cpu_list, "0");

    ec = adk::SetCpuAffinity("7");
    ec = adk::GetCpuAffinity(cpu_list);
    BOOST_CHECK_EQUAL(ec, adk::ErrorCode::kSuccess);
    BOOST_CHECK_EQUAL(cpu_list, "7");

    adk::SetCpuAffinity("0-7");
    ec = adk::GetCpuAffinity(cpu_list);
    BOOST_CHECK_EQUAL(ec, adk::ErrorCode::kSuccess);
    BOOST_CHECK_EQUAL(cpu_list, "0,1,2,3,4,5,6,7");

    adk::SetCpuAffinity("1,3-5");
    ec = adk::GetCpuAffinity(cpu_list);
    BOOST_CHECK_EQUAL(ec, adk::ErrorCode::kSuccess);
    BOOST_CHECK_EQUAL(cpu_list, "1,3,4,5");

    adk::SetCpuAffinity("3-4,6-7");
    ec = adk::GetCpuAffinity(cpu_list);
    BOOST_CHECK_EQUAL(ec, adk::ErrorCode::kSuccess);
    BOOST_CHECK_EQUAL(cpu_list, "3,4,6,7");

    boost::thread third_thread = boost::thread(TestGetCpuAffinity);
    third_thread.join();
    BOOST_CHECK_EQUAL(is_pass, true);
}

struct TestClass
{
    uint32_t a;
};

BOOST_AUTO_TEST_CASE(test_aligned_new)
{
    TestClass* ptr1 = adk::aligned_new<TestClass>(4);
    BOOST_CHECK_EQUAL(((uint64_t)ptr1 % 4), 0);

    TestClass* ptr2 = adk::aligned_new<TestClass>(8);
    BOOST_CHECK_EQUAL(((uint64_t)ptr2 % 8), 0);

    TestClass* ptr3 = adk::aligned_new<TestClass>(16);
    BOOST_CHECK_EQUAL(((uint64_t)ptr3 % 16), 0);

    TestClass* ptr4 = adk::aligned_new<TestClass>(32);
    BOOST_CHECK_EQUAL(((uint64_t)ptr4 % 32), 0);

    TestClass* ptr5 = adk::aligned_new<TestClass>(64);
    BOOST_CHECK_EQUAL(((uint64_t)ptr5 % 64), 0);
}

struct TestClass1
{
    TestClass1(uint32_t a_arg)
        :   a(a_arg)
    {}
    uint32_t a;
};

struct TestClass2
{
    TestClass2(uint32_t a1_arg, const std::string& a2_arg)
        :   a1(a1_arg), a2(a2_arg)
    {}

    uint32_t a1;
    std::string a2;
};

BOOST_AUTO_TEST_CASE(test_aligned_new_1)
{
    TestClass1* ptr1 = adk::aligned_new<TestClass1>(4, 4);
    BOOST_CHECK_EQUAL(((uint64_t)ptr1 % 4), 0);

    TestClass2* ptr5 = adk::aligned_new<TestClass2>(64, 4, "hello");
    BOOST_CHECK_EQUAL(((uint64_t)ptr5 % 64), 0);
}

BOOST_AUTO_TEST_CASE(test_aligned_new_3)
{
    TestClass1* ptr1 = adk::cache_aligned_new<TestClass1>(4);
    BOOST_CHECK_EQUAL(((uint64_t)ptr1 % ADK_CACHE_LINE_SIZE), 0);

    TestClass* ptr2 = adk::cache_aligned_new<TestClass>();
    BOOST_CHECK_EQUAL(((uint64_t)ptr2 % ADK_CACHE_LINE_SIZE), 0);

    TestClass2* ptr5 = adk::cache_aligned_new<TestClass2>(4, "hello");
    BOOST_CHECK_EQUAL(((uint64_t)ptr5 % ADK_CACHE_LINE_SIZE), 0);
}

enum TestInstanceId{
    kMinTestInstanceId = 0,
    kTestIntFirstId = 1,
    kTestIntSecondId = 2,
    kTestStringFirstId = 3,
    kTestStringSecondId = 4,
};

BOOST_AUTO_TEST_CASE(test_local_get_instance)
{
    auto& test_num_1 = adk::LocalGetInstance<uint32_t, kTestIntFirstId>();
    test_num_1 = 1;
    auto& test_num_2 = adk::LocalGetInstance<uint32_t, kTestIntSecondId>();
    test_num_2 = 2;
    BOOST_CHECK_EQUAL(test_num_1, 1);
    BOOST_CHECK_EQUAL(test_num_2, 2);

    test_num_2 = adk::LocalGetInstance<uint32_t, kTestIntSecondId>();
    BOOST_CHECK_EQUAL(test_num_2, 2);

    auto& test_str_1 = adk::LocalGetInstance<std::string, kTestStringFirstId>();
    BOOST_CHECK_EQUAL(test_str_1.size(), 0);

    test_str_1 = "hello";
    test_str_1 = adk::LocalGetInstance<std::string, kTestStringFirstId>();
    BOOST_CHECK_EQUAL(test_str_1, "hello");

    auto& test_str_2 = adk::LocalGetInstance<std::string, kTestStringSecondId>();
    BOOST_CHECK_EQUAL(test_str_2.size(), 0);

    test_str_2 = "world";
    test_str_2 = adk::LocalGetInstance<std::string, kTestStringSecondId>();
    BOOST_CHECK_EQUAL(test_str_2, "world");
}

BOOST_AUTO_TEST_CASE(test_compare_two_json)
{
    // 元素都是简单类型
    nlohmann::json json_value_1;
    json_value_1["first"] = 1;
    json_value_1["second"] = "second";

    nlohmann::json json_value_2;
    json_value_2["first"] = 1;
    json_value_2["second"] = "second";

    BOOST_CHECK_EQUAL(adk::CompareTwoJson(json_value_1, json_value_2), true);

    // 元素是array，顺序不一致
    json_value_1["array_1"] = {1,9,2,3,5,4};
    json_value_2["array_1"] = {9,2,3,1,4,5};
    BOOST_CHECK_EQUAL(adk::CompareTwoJson(json_value_1, json_value_2), true);

    // 元素是array，array内的元素是array，顺序不一致
    json_value_1["array_2"] = {{1,3,2},{4,6,5},{7,8,9}};
    json_value_2["array_2"] = {{5,6,4},{8,7,9},{3,1,2}};
    BOOST_CHECK_EQUAL(adk::CompareTwoJson(json_value_1, json_value_2), true);

    // 元素是array，array内的元素是object，顺序不一致
    nlohmann::json sub_json_1_value_1;
    sub_json_1_value_1["a"] = "a";
    sub_json_1_value_1["b"] = {1,2,7,9,10,8};
    sub_json_1_value_1["c"] = {{1,2,3},{4,5,6},{7,8,9}};
    sub_json_1_value_1["d"] = json_value_1;
    nlohmann::json sub_json_1_value_2;
    sub_json_1_value_2["e"] = "e";
    sub_json_1_value_2["f"] = {3.5, 4.2, 1.1};
    sub_json_1_value_2["g"] = {{1.2,2.2,3.3},{4.1,5.2,6.8},{7.9,8.1,9.2}};
    sub_json_1_value_2["h"] = json_value_1;

    json_value_1["array_3"] = {sub_json_1_value_1, sub_json_1_value_2};

    nlohmann::json sub_json_2_value_1;
    sub_json_2_value_1["d"] = json_value_2;
    sub_json_2_value_1["b"] = {1,9,10,8,2,7};
    sub_json_2_value_1["c"] = {{5,4,6},{1,3,2},{7,9,8}};
    sub_json_2_value_1["a"] = "a";
    nlohmann::json sub_json_2_value_2;
    sub_json_2_value_2["f"] = {4.2, 3.5, 1.1};
    sub_json_2_value_2["e"] = "e";
    sub_json_2_value_2["h"] = json_value_2;
    sub_json_2_value_2["g"] = {{4.1,6.8,5.2},{9.2,7.9,8.1},{1.2,3.3,2.2}};

    json_value_2["array_3"] = {sub_json_2_value_2, sub_json_2_value_1};

    BOOST_CHECK_EQUAL(adk::CompareTwoJson(json_value_1, json_value_2), true);

    // 元素是object，顺序不一致
    json_value_1["object_value_1"] = sub_json_1_value_1;
    json_value_1["object_value_2"] = sub_json_1_value_2;
    json_value_1["object_value_3"] = {{sub_json_1_value_1, sub_json_1_value_2},{sub_json_2_value_2,sub_json_2_value_1}};

    json_value_2["object_value_3"] = {{sub_json_2_value_1, sub_json_2_value_2},{sub_json_1_value_2,sub_json_1_value_1},};
    json_value_2["object_value_2"] = sub_json_2_value_2;
    json_value_2["object_value_1"] = sub_json_2_value_1;

    BOOST_CHECK_EQUAL(adk::CompareTwoJson(json_value_1, json_value_2), true);

    // json1比json2内容多
    json_value_1["array_2"] = {{1,3,2},{4,6,5},{7,8,9,10}};
    BOOST_CHECK_EQUAL(adk::CompareTwoJson(json_value_1, json_value_2), false);

    // json2补充缺少的元素后json1和json2相等
    json_value_2["array_2"] = {{1,3,2},{4,6,5},{10,7,8,9}};
    BOOST_CHECK_EQUAL(adk::CompareTwoJson(json_value_1, json_value_2), true);

    // json2比json1内容多
    json_value_2["array_2"] = {{1,3,2},{4,6,5},{10,7,8,9,5}};
    BOOST_CHECK_EQUAL(adk::CompareTwoJson(json_value_1, json_value_2), false);

    // json1补充缺少的元素后json1和json2相等
    json_value_1["array_2"] = {{1,3,2},{7,8,9,10,5},{4,6,5}};
    BOOST_CHECK_EQUAL(adk::CompareTwoJson(json_value_1, json_value_2), true);

    // 一个是空的json
    BOOST_CHECK_EQUAL(adk::CompareTwoJson(json_value_1, nlohmann::json()), false);

    // json1比json2元素多
    json_value_1["object_4"] = {1,2,3};
    BOOST_CHECK_EQUAL(adk::CompareTwoJson(json_value_1, json_value_2), false);

    // json2补充元素，和json1相同
    json_value_2["object_4"] = {1,3,2};
    BOOST_CHECK_EQUAL(adk::CompareTwoJson(json_value_1, json_value_2), true);

    // json2比json1元素多
    json_value_2["z_object_5"] = {1,2,3};
    BOOST_CHECK_EQUAL(adk::CompareTwoJson(json_value_1, json_value_2), false);

    // json1补充元素，和json2相同
    json_value_1["z_object_5"] = {1,3,2};
    BOOST_CHECK_EQUAL(adk::CompareTwoJson(json_value_1, json_value_2), true);

    // json1 和 json2都是string
    BOOST_CHECK_EQUAL(adk::CompareTwoJson(json_value_1.dump(), json_value_2.dump()), true);

    // json1 是string， json2是json
    BOOST_CHECK_EQUAL(adk::CompareTwoJson(json_value_1.dump(), json_value_2), true);
}

BOOST_AUTO_TEST_CASE(test_get_ami_home)
{
    std::string path = adk::GetInstallPath("libxxxxxxxxxxxx.so");
    BOOST_CHECK_EQUAL(path.empty(), true);

    path = adk::GetInstallPath("libadk.so");
    BOOST_CHECK_EQUAL(path.empty(), true);

    path = adk::GetInstallPath("");
    BOOST_CHECK_EQUAL(path.empty(), true);

    // path = adk::GetInstallPath("*");
    // BOOST_CHECK_EQUAL(path.empty(), true);

    // GetInstallPath函数内查找库的匹配规则不再是正则匹配, 这里需显示指定查找libadk.so库
    std::string subpath;
    path = adk::GetInstallPath("libadk.so", subpath);
    BOOST_CHECK_EQUAL(!path.empty(), true);

    subpath = "/code/";
    path = adk::GetInstallPath("libadk.so", subpath);
    BOOST_CHECK_EQUAL(!path.empty(), true);
}

BOOST_AUTO_TEST_CASE(test_inotify_wrapper)
{
    std::atomic<int> modify_file_count = {0};
    std::atomic<int> delete_file_count = {0};

    auto on_watch = [](const std::string& path, 
                       const adk_impl::inotify_event_t& event, 
                       const std::string& watch_name,
                       std::atomic<int>* modify_file_count,
                       std::atomic<int>* delete_file_count){

        if (event == adk_impl::inotify_event_t::in_modify)
        {
            ++(*modify_file_count);
        }
        else if(event == adk_impl::inotify_event_t::in_delete)
        {
            ++(*delete_file_count);
        }

        return adk_impl::ErrorCode::kSuccess;
    };

    auto test_func = [&](const int watch_timeout_milli){
        adk_impl::Inotify inotify;
        
        int wait_sleep_seconds = 0;
        inotify.Init(watch_timeout_milli);

        inotify.Start();

        modify_file_count = 0;
        delete_file_count = 0;

        // 测试文件修改和删除
        system("touch test.txt");
        
        auto ec = inotify.AddWatch("test.txt", 
                                   adk_impl::inotify_event_t::in_modify, 
                                   "test_watch", 
                                   std::bind(on_watch, 
                                             std::placeholders::_1, 
                                             std::placeholders::_2, 
                                             std::placeholders::_3,
                                             &modify_file_count,
                                             &delete_file_count));
        BOOST_CHECK(ec == adk_impl::ErrorCode::kSuccess);

        // 测试删除回调函数是否正常
        ec = inotify.AddWatch("test.txt", 
                              adk_impl::inotify_event_t::in_delete, 
                              "test_watch", 
                              std::bind(on_watch, 
                                        std::placeholders::_1, 
                                        std::placeholders::_2, 
                                        std::placeholders::_3,
                                        &modify_file_count,
                                        &delete_file_count));
        BOOST_CHECK(ec == adk_impl::ErrorCode::kSuccess);
        
        // 添加另一个回调函数
        ec = inotify.AddWatch("test.txt", 
                              adk_impl::inotify_event_t::in_modify, 
                              "cat_watch",
                              std::bind(on_watch, 
                                        std::placeholders::_1, 
                                        std::placeholders::_2, 
                                        std::placeholders::_3,
                                        &modify_file_count,
                                        &delete_file_count));
        BOOST_CHECK(ec == adk_impl::ErrorCode::kSuccess);
        
        ec = inotify.AddWatch("test.txt", 
                              adk_impl::inotify_event_t::in_modify, 
                              "cat_watch_once", 
                              std::bind(on_watch, 
                                        std::placeholders::_1, 
                                        std::placeholders::_2, 
                                        std::placeholders::_3,
                                        &modify_file_count,
                                        &delete_file_count), 
                              true);
        BOOST_CHECK(ec == adk_impl::ErrorCode::kSuccess);

        for (int i = 0; i < 100; ++i)
        {
            system("echo test >> test.txt");
        }

        ec = inotify.RemoveWatch("test.txt", adk_impl::inotify_event_t::in_modify, "cat_watch");
        BOOST_CHECK(ec == adk_impl::ErrorCode::kSuccess);
        
        system("echo test > test.txt");
        system("rm test.txt");

        // 如果60秒内还没有触发删除就绪事件就认为测试失败
        while (delete_file_count == 0
               && wait_sleep_seconds < 60)
        {
            ++wait_sleep_seconds;
            sleep(1);
        }

        inotify.Stop();

        BOOST_CHECK(modify_file_count > 0);
        BOOST_CHECK(delete_file_count > 0);
    };
    
    
    // 超时时间为-1
    test_func(-1);

    // 超时时间为0
    test_func(0);
    
    // 超时时间大于0
    test_func(10);
}

BOOST_AUTO_TEST_CASE(test_inotify_wrapper_spent_time)
{
    std::atomic<int> modify_file_count = {0};
    std::atomic<int> delete_file_count = {0};

    auto on_watch = [](const std::string& path, 
                       const adk_impl::inotify_event_t& event, 
                       const std::string& watch_name,
                       std::atomic<int>* modify_file_count,
                       std::atomic<int>* delete_file_count){

        if (event == adk_impl::inotify_event_t::in_modify)
        {
            ++(*modify_file_count);
        }
        else if(event == adk_impl::inotify_event_t::in_delete)
        {
            ++(*delete_file_count);
        }

        return adk_impl::ErrorCode::kSuccess;
    };

    auto test_func = [&](const int watch_timeout_milli){
        adk_impl::Inotify inotify;
        
        int wait_sleep_seconds = 0;
        inotify.Init(watch_timeout_milli);

        inotify.Start();

        modify_file_count = 0;
        delete_file_count = 0;

        std::vector<std::thread> test_threads;
        // 创建1000个文件
        std::map<std::string, std::fstream*> file_streams_map;
        for (size_t index = 0; index < 1000; ++index)
        {
            std::string file_name = "test_" + std::to_string(index) + ".txt";
            auto* file_stream = new std::fstream;
            BOOST_CHECK(file_stream != nullptr);
            file_stream->open(file_name, std::fstream::out);
            BOOST_CHECK(file_stream->is_open());
            file_streams_map[file_name] = file_stream;
        }

        // 开启十个线程，每个线程都会更新文件，后续的AddWatch调用和RemoveWatch调用时间不超过100毫秒
        for (size_t i = 0; i < 10; ++i)
        {
            test_threads.emplace_back([&file_streams_map](){
                for (size_t i = 0; i < 1000; ++i)
                {
                    for (auto& file_stream: file_streams_map)
                    {
                        *(file_stream.second) << i << std::endl;
                        file_stream.second->flush();
                    }
                }
            });
        }

        for (auto& file_stream: file_streams_map)
        {
            auto begin_time = std::chrono::high_resolution_clock::now();
            auto ec = inotify.AddWatch(file_stream.first, 
                                    adk_impl::inotify_event_t::in_modify, 
                                    "test_watch", 
                                    std::bind(on_watch, 
                                                std::placeholders::_1, 
                                                std::placeholders::_2, 
                                                std::placeholders::_3,
                                                &modify_file_count,
                                                &delete_file_count));
            BOOST_CHECK(ec == adk_impl::ErrorCode::kSuccess);
            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> spend_time = end_time - begin_time;
            BOOST_CHECK(spend_time.count() <= 100);

            begin_time = std::chrono::high_resolution_clock::now();
            ec = inotify.AddWatch(file_stream.first, 
                                adk_impl::inotify_event_t::in_delete, 
                                "test_watch", 
                                std::bind(on_watch, 
                                            std::placeholders::_1, 
                                            std::placeholders::_2, 
                                            std::placeholders::_3,
                                            &modify_file_count,
                                            &delete_file_count));
            BOOST_CHECK(ec == adk_impl::ErrorCode::kSuccess);
            end_time = std::chrono::high_resolution_clock::now();
            spend_time = end_time - begin_time;
            BOOST_CHECK(spend_time.count() <= 100);

            // 添加另一个回调函数
            begin_time = std::chrono::high_resolution_clock::now();
            ec = inotify.AddWatch(file_stream.first,
                                adk_impl::inotify_event_t::in_modify, 
                                "cat_watch",
                                std::bind(on_watch, 
                                            std::placeholders::_1, 
                                            std::placeholders::_2, 
                                            std::placeholders::_3,
                                            &modify_file_count,
                                            &delete_file_count));
            BOOST_CHECK(ec == adk_impl::ErrorCode::kSuccess);
            end_time = std::chrono::high_resolution_clock::now();
            spend_time = end_time - begin_time;
            BOOST_CHECK(spend_time.count() <= 100);

            begin_time = std::chrono::high_resolution_clock::now();
            ec = inotify.AddWatch(file_stream.first,
                                adk_impl::inotify_event_t::in_modify, 
                                "cat_watch_once", 
                                std::bind(on_watch, 
                                            std::placeholders::_1, 
                                            std::placeholders::_2, 
                                            std::placeholders::_3,
                                            &modify_file_count,
                                            &delete_file_count), 
                                true);
            BOOST_CHECK(ec == adk_impl::ErrorCode::kSuccess);
            end_time = std::chrono::high_resolution_clock::now();
            spend_time = end_time - begin_time;
            BOOST_CHECK(spend_time.count() <= 100);
        }


        for (auto& file_stream: file_streams_map)
        {
            auto begin_time = std::chrono::high_resolution_clock::now();
            auto ec = inotify.RemoveWatch(file_stream.first, adk_impl::inotify_event_t::in_modify, "cat_watch");
            BOOST_CHECK(ec == adk_impl::ErrorCode::kSuccess);

            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> spend_time = end_time - begin_time;
            BOOST_CHECK(spend_time.count() <= 100);
        }

        // 如果60秒内还没有触发删除就绪事件就认为测试失败
        while (delete_file_count == 0
               && wait_sleep_seconds < 60)
        {
            ++wait_sleep_seconds;
            sleep(1);
        }

        for (auto& thread: test_threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }

        for (auto& file_stream: file_streams_map)
        {
            file_stream.second->close();
            std::string cmd = "rm " + file_stream.first;
            system(cmd.c_str());
        }

        inotify.Stop();

        BOOST_CHECK(modify_file_count > 0);
        BOOST_CHECK(delete_file_count > 0);
    };

    // 超时时间为-1
    test_func(-1);

    // 超时时间为0
    test_func(0);

    // 超时时间大于0
    test_func(10);
}