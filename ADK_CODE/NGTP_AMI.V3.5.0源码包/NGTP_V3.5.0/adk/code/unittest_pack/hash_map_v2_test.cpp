#define BOOST_TEST_MODULE hash_map_test
#include <boost/test/included/unit_test.hpp>

#include <stdlib.h>

#include <iostream>
#include <mutex>
#include <utility>

#include <array>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <boost/unordered_map.hpp>

#include <adk_pack/error_code.h>
#include <adk_pack/hash_map.h>
#include <adk_pack/lock_free_unbounded_queue_variant.h>

using std::mutex;
using std::pair;

using std::array;
using boost::unordered_map;

using adk::ErrorCode;
using adk::HashMap;
using adk::variant::MPSCUnboundedQueue;

/**
 * @brief 随机数生成类
 *
 * @tparam _Type 随机值的类型
 */
template<typename _Type>
struct RandomFactory
{
    static _Type GetRandomValue()
    {
        static_assert(std::is_enum<_Type>::value, "Not support");
    }
};

/**
 * @brief hash map 中元素存在时回调接口
 *
 * @tparam _Vty     值类型
 * @param value     冲突的值
 * @return false    不覆盖原始值
 */
template<typename _Vty>
static bool IsConver(const _Vty& value)
{
    return false;
}

/**
 * @see static bool IsConver(const _Vty& value)
 *
 * @param is_called 是否调用过该接口
 */
template<typename _Vty>
static bool IsConver(const _Vty& value, bool* is_called)
{
    *is_called = true;
    return false;
}

template<>
inline array<char, 6> RandomFactory<array<char, 6>>::GetRandomValue()
{
    int32_t value = random() % 1000000;

    array<char, 6> temp;
    char* head = temp.data();
    snprintf(head, 6, "%d", value);
    return temp;
}

template<>
inline array<char, 8> RandomFactory<array<char, 8>>::GetRandomValue()
{
    int32_t value = random() % 100000000;

    array<char, 8> temp;
    char* head = temp.data();
    snprintf(head, 8, "%d", value);

    return temp;
}

template<>
inline array<char, 10> RandomFactory<array<char, 10>>::GetRandomValue()
{
    int64_t value = random() % 10000000000;

    array<char, 10> temp;
    char* head = temp.data();
    snprintf(head, 10, "%ld", value);

    return temp;
}

template<>
inline array<char, 12> RandomFactory<array<char, 12>>::GetRandomValue()
{
    int64_t value = random() % 1000000000000;

    array<char, 12> temp;
    char* head = temp.data();
    snprintf(head, 12, "%ld", value);

    return temp;
}

template<>
inline int64_t RandomFactory<int64_t>::GetRandomValue()
{
    return random();
}

template<>
inline void* RandomFactory<void*>::GetRandomValue()
{
    return (void*)random();
}

struct InsertTrdInfo
{
    int32_t trd_index;
    int32_t intensity;
    mutex* std_hm_lock;
    volatile bool* is_block;
    boost::thread thread_handle;
};

// 向 HashMap 和 std::unordered_map 同时插入， 对比结果
template<typename _Kty1, typename _Kty2, typename _Vty>
void InsertThread1(InsertTrdInfo* trd_info, HashMap<_Kty1, _Kty2, _Vty>* hm_object, unordered_map<pair<_Kty1, _Kty2>, _Vty>* std_hm)
{
    std::cout << "Insert thread with thread index<" << trd_info->trd_index << "> start to run" << std::endl;

    // 等待信号，一起开始任务
    while (*(trd_info->is_block))
        ;

    boost::function<bool(const _Vty&)> is_cover = boost::bind(IsConver<_Vty>, _1);
    for (int32_t index = 0; index < trd_info->intensity; ++index)
    {
        const _Kty1 key1 = RandomFactory<_Kty1>::GetRandomValue();
        const _Kty2 key2 = RandomFactory<_Kty2>::GetRandomValue();
        const _Vty value = RandomFactory<_Vty>::GetRandomValue();

        // 如果随机生成的元素已经存在，则跳过
        // 由于随机后冲突的概率本身就很低， 忽略多线程的冲突
        _Vty* result = nullptr;
        if (hm_object->Find(key1, key2, &result) == adk::ErrorCode::kSuccess)
            continue;

        hm_object->Insert(key1, key2, value);

        {
            // 将元素放入 std::unordered_map 中
            std::lock_guard<mutex> lock(*trd_info->std_hm_lock);
            std_hm->insert(std::make_pair(std::make_pair(key1, key2), value));
        }
    }

    std::cout << "Thread index<" << trd_info->trd_index << "> exit" << std::endl;
}

template<typename _Kty1, typename _Kty2, typename _Vty>
struct KeyValuePair
{
    _Kty1 key1;
    _Kty2 key2;
    _Vty value;

    KeyValuePair& operator=(const KeyValuePair& other)
    {
        this->key1 = other.key1;
        this->key2 = other.key2;
        this->value = other.value;
        return *this;
    }
};

// 向 HashMap 和 MPSCUnboundedQueue 同时插入， 对比结果
template<typename _Kty1, typename _Kty2, typename _Vty>
void InsertThread2(InsertTrdInfo* trd_info, HashMap<_Kty1, _Kty2, _Vty>* hm_object, MPSCUnboundedQueue<KeyValuePair<_Kty1, _Kty2, _Vty>>* unbounded_queue)
{
    std::cout << "Insert thread with thread index<" << trd_info->trd_index << "> start to run" << std::endl;

    // 等待信号，一起开始任务
    while (*(trd_info->is_block))
        ;

    bool is_called = false;
    // 对应接口由于未封装
    boost::function<bool(const _Vty&)> is_cover = boost::bind(IsConver<_Vty>, _1, &is_called);
    for (int32_t index = 0; index < trd_info->intensity; ++index)
    {
        const _Kty1 key1 = RandomFactory<_Kty1>::GetRandomValue();
        const _Kty2 key2 = RandomFactory<_Kty2>::GetRandomValue();
        const _Vty value = RandomFactory<_Vty>::GetRandomValue();

        is_called = false;
        _Vty* result = nullptr;
        if (hm_object->Find(key1, key2, &result) == adk::ErrorCode::kSuccess)
            continue;

        hm_object->Insert(key1, key2, value);

        // 如果未产生冲突，放入队列中。 由于接口未封装实际不生效
        if (!is_called)
        {
            const int32_t result = unbounded_queue->Push({key1, key2, value});
            if (ADK_UNLIKELY(ErrorCode::kSuccess != result))
            {
                std::cout << "Push key1-key2-value pair into unbounded queue failed" << std::endl;
            }
        }
    }

    std::cout << "Thread index<" << trd_info->trd_index << "> exit" << std::endl;
}

BOOST_AUTO_TEST_CASE(mulit_thread_insert_check_result)
{
    constexpr int32_t kInsertThreadNum = 8;
    constexpr int32_t kTestIntensity = 1000000;
    constexpr int32_t kMapReserveSize = kTestIntensity * kInsertThreadNum;

    HashMap<array<char, 6>, array<char, 10>, int64_t>* hm_object = HashMap<array<char, 6>, array<char, 10>, int64_t>::Create(kMapReserveSize);
    BOOST_CHECK(hm_object);

    mutex std_hm_lock;
    unordered_map<pair<array<char, 6>, array<char, 10>>, int64_t> std_hm;
    std_hm.reserve(kMapReserveSize);

    bool is_block = true;
    InsertTrdInfo insert_trd_info[kInsertThreadNum];
    // 创建多个测试线程，执行插入操作
    for (int32_t index = 0; index < kInsertThreadNum; ++index)
    {
        InsertTrdInfo& trd_info = insert_trd_info[index];
        trd_info.trd_index = index;
        trd_info.is_block = &is_block;
        trd_info.std_hm_lock = &std_hm_lock;
        trd_info.intensity = kTestIntensity;
        trd_info.thread_handle =
            boost::thread(boost::bind(InsertThread1<array<char, 6>, array<char, 10>, int64_t>,
                                      &trd_info,
                                      hm_object,
                                      &std_hm));
    }

    // 所有线程创建完成后， 一起开始插入 提高并发度
    is_block = false;

    for (int32_t index = 0; index < kInsertThreadNum; ++index)
    {
        InsertTrdInfo& trd_info = insert_trd_info[index];
        trd_info.thread_handle.join();
    }

    std::cout << "Start to compare the elements" << std::endl;

    int64_t element_index = 0;

    int64_t* value_ptr = nullptr;
    // 检查 HashMap 与 unordered_map 中的内容是否一致
    for (auto iter = std_hm.begin(); iter != std_hm.end(); ++iter, ++element_index)
    {
        const int32_t result = hm_object->Find(iter->first.first, iter->first.second, &value_ptr);
        BOOST_CHECK_EQUAL(result, ErrorCode::kSuccess);
        BOOST_CHECK_EQUAL(*value_ptr, iter->second);
    }

    std::cout << "Compare completely, size = <" << element_index << ">" << std::endl;
    // HashMap 没有销毁接口， 生命周期与进程一样
}

BOOST_AUTO_TEST_CASE(mulit_thread_insert_and_read)
{
    constexpr int32_t kTestIntensity = 1000000;
    constexpr int32_t kInsertThreadNum = 8;
    constexpr int32_t kMapReserveSize = kTestIntensity * kInsertThreadNum;

    HashMap<array<char, 6>, array<char, 10>, int64_t>* hm_object = HashMap<array<char, 6>, array<char, 10>, int64_t>::Create(kMapReserveSize);
    BOOST_CHECK(hm_object);

    using ElemType = KeyValuePair<array<char, 6>, array<char, 10>, int64_t>;
    MPSCUnboundedQueue<ElemType>* unbounded_queue =
        MPSCUnboundedQueue<ElemType>::Create("mulit_thread_insert_and_read");
    BOOST_CHECK(unbounded_queue);

    bool is_block = true;
    InsertTrdInfo insert_trd_info[kInsertThreadNum];

    std::vector<boost::thread*> joinable_thread;
    joinable_thread.reserve(kInsertThreadNum);

    for (int32_t index = 0; index < kInsertThreadNum; ++index)
    {
        InsertTrdInfo& trd_info = insert_trd_info[index];
        trd_info.trd_index = index;
        trd_info.is_block = &is_block;
        trd_info.intensity = kTestIntensity;
        trd_info.thread_handle = boost::thread(boost::bind(InsertThread2<array<char, 6>, array<char, 10>, int64_t>, &trd_info, hm_object, unbounded_queue));
        joinable_thread.push_back(&(trd_info.thread_handle));
    }

    is_block = false;

    int64_t element_index = 0;
    int64_t* value_ptr = nullptr;
    // adk::variant::VariantEntry* entry_ptr = nullptr;
    ElemType* element = new ElemType();
    // 检查 HashMap 与队列中的内容是否一致
    while (true)
    {
        // 一边入队、一边出队比较， 测试 Find 和 Insert 操作同时进行的线程安全性
        if (ErrorCode::kSuccess != unbounded_queue->Pop(*element))
        {
            for (auto iter = joinable_thread.begin(); iter != joinable_thread.end();)
            {
                if ((*iter)->timed_join(boost::posix_time::milliseconds(0)))
                {
                    iter = joinable_thread.erase(iter);
                }
                else
                {
                    ++iter;
                }
            }

            // 如果队列为空 且所有线程都结束了，跳出循环
            if (ADK_UNLIKELY(0 == joinable_thread.size()))
            {
                break;
            }

            continue;
        }

        const int32_t result = hm_object->Find(element->key1, element->key2, &value_ptr);
        BOOST_CHECK_EQUAL(result, ErrorCode::kSuccess);
        BOOST_CHECK_EQUAL(*value_ptr, element->value);
        ++element_index;
    }

    std::cout << "Compare completely, size = <" << element_index << ">" << std::endl;
}

BOOST_AUTO_TEST_CASE(mulit_thread_insert_check_result_char_12_8)
{
    constexpr int32_t kInsertThreadNum = 8;
    constexpr int32_t kTestIntensity = 1000000;
    constexpr int32_t kMapReserveSize = kTestIntensity * kInsertThreadNum;

    HashMap<array<char, 12>, array<char, 8>, void*>* hm_object = HashMap<array<char, 12>, array<char, 8>, void*>::Create(kMapReserveSize);
    BOOST_CHECK(hm_object);

    mutex std_hm_lock;
    unordered_map<pair<array<char, 12>, array<char, 8>>, void*> std_hm;
    std_hm.reserve(kMapReserveSize);

    bool is_block = true;
    InsertTrdInfo insert_trd_info[kInsertThreadNum];
    // 创建多个测试线程，执行插入操作
    for (int32_t index = 0; index < kInsertThreadNum; ++index)
    {
        InsertTrdInfo& trd_info = insert_trd_info[index];
        trd_info.trd_index = index;
        trd_info.is_block = &is_block;
        trd_info.std_hm_lock = &std_hm_lock;
        trd_info.intensity = kTestIntensity;
        trd_info.thread_handle =
            boost::thread(boost::bind(InsertThread1<array<char, 12>, array<char, 8>, void*>,
                                      &trd_info,
                                      hm_object,
                                      &std_hm));
    }

    // 所有线程创建完成后， 一起开始插入 提高并发度
    is_block = false;

    for (int32_t index = 0; index < kInsertThreadNum; ++index)
    {
        InsertTrdInfo& trd_info = insert_trd_info[index];
        trd_info.thread_handle.join();
    }

    std::cout << "Start to compare the elements" << std::endl;

    int64_t element_index = 0;

    void** value_ptr = nullptr;
    // 检查 HashMap 与 unordered_map 中的内容是否一致
    for (auto iter = std_hm.begin(); iter != std_hm.end(); ++iter, ++element_index)
    {
        const int32_t result = hm_object->Find(iter->first.first, iter->first.second, &value_ptr);
        BOOST_CHECK_EQUAL(result, ErrorCode::kSuccess);
        BOOST_CHECK_EQUAL(*value_ptr, iter->second);
    }

    std::cout << "Compare completely, size = <" << element_index << ">" << std::endl;
    // HashMap 没有销毁接口， 生命周期与进程一样
}

BOOST_AUTO_TEST_CASE(mulit_thread_insert_and_read_char_12_8)
{
    constexpr int32_t kTestIntensity = 1000000;
    constexpr int32_t kInsertThreadNum = 8;
    constexpr int32_t kMapReserveSize = kTestIntensity * kInsertThreadNum;

    HashMap<array<char, 12>, array<char, 8>, void*>* hm_object = HashMap<array<char, 12>, array<char, 8>, void*>::Create(kMapReserveSize);
    BOOST_CHECK(hm_object);

    using ElemType = KeyValuePair<array<char, 12>, array<char, 8>, void*>;
    MPSCUnboundedQueue<ElemType>* unbounded_queue =
        MPSCUnboundedQueue<ElemType>::Create("mulit_thread_insert_and_read");
    BOOST_CHECK(unbounded_queue);

    bool is_block = true;
    InsertTrdInfo insert_trd_info[kInsertThreadNum];

    std::vector<boost::thread*> joinable_thread;
    joinable_thread.reserve(kInsertThreadNum);

    for (int32_t index = 0; index < kInsertThreadNum; ++index)
    {
        InsertTrdInfo& trd_info = insert_trd_info[index];
        trd_info.trd_index = index;
        trd_info.is_block = &is_block;
        trd_info.intensity = kTestIntensity;
        trd_info.thread_handle = boost::thread(boost::bind(InsertThread2<array<char, 12>, array<char, 8>, void*>, &trd_info, hm_object, unbounded_queue));
        joinable_thread.push_back(&(trd_info.thread_handle));
    }

    is_block = false;

    int64_t element_index = 0;
    void** value_ptr = nullptr;
    // adk::variant::VariantEntry* entry_ptr = nullptr;
    ElemType* element = new ElemType();
    // 检查 HashMap 与队列中的内容是否一致
    while (true)
    {
        // 一边入队、一边出队比较， 测试 Find 和 Insert 操作同时进行的线程安全性
        if (ErrorCode::kSuccess != unbounded_queue->Pop(*element))
        {
            for (auto iter = joinable_thread.begin(); iter != joinable_thread.end();)
            {
                if ((*iter)->timed_join(boost::posix_time::milliseconds(0)))
                {
                    iter = joinable_thread.erase(iter);
                }
                else
                {
                    ++iter;
                }
            }

            // 如果队列为空 且所有线程都结束了，跳出循环
            if (ADK_UNLIKELY(0 == joinable_thread.size()))
            {
                break;
            }

            continue;
        }

        const int32_t result = hm_object->Find(element->key1, element->key2, &value_ptr);
        BOOST_CHECK_EQUAL(result, ErrorCode::kSuccess);
        BOOST_CHECK_EQUAL(*value_ptr, element->value);
        ++element_index;
    }

    std::cout << "Compare completely, size = <" << element_index << ">" << std::endl;
}
