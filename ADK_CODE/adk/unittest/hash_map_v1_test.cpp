#define BOOST_TEST_MODULE hash_map_test
#include <boost/test/included/unit_test.hpp>

#include <stdlib.h>

#include <mutex>
#include <utility>
#include <iostream>

#include <boost/bind.hpp>
#include <boost/array.hpp>
#include <boost/thread.hpp>
#include <boost/unordered_map.hpp>

#include <adk/hash_map.h>
#include <adk/error_code.h>
#include <adk/lock_free_unbounded_queue_variant.h>

using std::pair;
using std::mutex;

using boost::array;
using boost::unordered_map;

using adk::HashMap;
using adk::ErrorCode;
using adk::variant::MPSCUnboundedQueue;

template<typename _Type>
struct RandomFactory
{
    static _Type GetRandomValue()
    {
        static_assert(std::is_enum<_Type>::value, "Not support");
    }
};

template<typename _Vty>
static bool IsConver(const _Vty& value)
{
    return false;
}

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
inline int64_t RandomFactory<int64_t>::GetRandomValue()
{
    return random();
}

struct InsertTrdInfo
{
    int32_t trd_index;
    int32_t intensity;
    mutex* std_hm_lock;
    volatile bool* is_block;
    boost::thread thread_handle;
};

template<typename _Kty1, typename _Kty2, typename _Vty>
void InsertThread1(InsertTrdInfo* trd_info, HashMap<_Kty1, _Kty2, _Vty>* hm_object, unordered_map<pair<_Kty1, _Kty2>, _Vty>* std_hm)
{
    std::cout << "Insert thread with thread index<" << trd_info->trd_index << "> start to run" << std::endl;

    while (*(trd_info->is_block));

    boost::function<bool(const _Vty&)> is_cover = boost::bind(IsConver<_Vty>, _1);
    for (int32_t index=0; index<trd_info->intensity; ++index)
    {
        const _Kty1 key1 = RandomFactory<_Kty1>::GetRandomValue();
        const _Kty2 key2 = RandomFactory<_Kty2>::GetRandomValue();
        const _Vty  value = RandomFactory<_Vty>::GetRandomValue();

        hm_object->Insert(key1, key2, value, &is_cover);

        {
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
    _Vty  value;
};

template<typename _Kty1, typename _Kty2, typename _Vty>
void InsertThread2(InsertTrdInfo* trd_info, HashMap<_Kty1, _Kty2, _Vty>* hm_object, MPSCUnboundedQueue<KeyValuePair<_Kty1, _Kty2, _Vty>>* unbounded_queue)
{
    std::cout << "Insert thread with thread index<" << trd_info->trd_index << "> start to run" << std::endl;
    
    while (*(trd_info->is_block));

    bool is_called = false;
    boost::function<bool(const _Vty&)> is_cover = boost::bind(IsConver<_Vty>, _1, &is_called);
    for (int32_t index = 0; index < trd_info->intensity; ++index)
    {
        const _Kty1 key1 = RandomFactory<_Kty1>::GetRandomValue();
        const _Kty2 key2 = RandomFactory<_Kty2>::GetRandomValue();
        const _Vty  value = RandomFactory<_Vty>::GetRandomValue();

        is_called = false;
        hm_object->Insert(key1, key2, value, &is_cover);
        
        if (!is_called)
        {
            const int32_t result = unbounded_queue->Push({ key1, key2, value });
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
    for (int32_t index=0; index < kInsertThreadNum; ++index)
    {
        InsertTrdInfo& trd_info = insert_trd_info[index];
        trd_info.trd_index = index;
        trd_info.is_block = &is_block;
        trd_info.std_hm_lock = &std_hm_lock;
        trd_info.intensity = kTestIntensity;
        trd_info.thread_handle = boost::thread(boost::bind(InsertThread1<array<char, 6>, array<char, 10>, int64_t>, &trd_info, hm_object, &std_hm));
    }

    is_block = false;

    for (int32_t index=0; index < kInsertThreadNum; ++index)
    {
        InsertTrdInfo& trd_info = insert_trd_info[index];
        trd_info.thread_handle.join();
    }

    std::cout << "Start to compare the elements" << std::endl;

    int64_t element_index = 0;
    
    int64_t* value_ptr = nullptr;
    for (auto iter = std_hm.begin(); iter != std_hm.end(); ++iter, ++element_index)
    {        
        const int32_t result = hm_object->Find(iter->first.first, iter->first.second, &value_ptr);
        BOOST_CHECK_EQUAL(result, ErrorCode::kSuccess);
        BOOST_CHECK_EQUAL(*value_ptr, iter->second);
    }

    std::cout << "Compare completely, size = <" << element_index << ">" << std::endl;
}

BOOST_AUTO_TEST_CASE(mulit_thread_insert_and_read)
{
    constexpr int32_t kTestIntensity = 1000000;
    constexpr int32_t kInsertThreadNum = 8;
    constexpr int32_t kMapReserveSize = kTestIntensity * kInsertThreadNum;

    HashMap<array<char, 6>, array<char, 10>, int64_t>* hm_object = HashMap<array<char, 6>, array<char, 10>, int64_t>::Create(kMapReserveSize);
    BOOST_CHECK(hm_object);

    MPSCUnboundedQueue<KeyValuePair<array<char, 6>, array<char, 10>, int64_t>>* unbounded_queue =
        MPSCUnboundedQueue<KeyValuePair<array<char, 6>, array<char, 10>, int64_t>>::Create("mulit_thread_insert_and_read");
    BOOST_CHECK(unbounded_queue);

    bool is_block = true;
    InsertTrdInfo insert_trd_info[kInsertThreadNum];

    vector<boost::thread*> joinable_thread;
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
    adk::variant::VariantEntry* entry_ptr = nullptr;
    while (true)
    {
        if (ErrorCode::kSuccess != unbounded_queue->WaitEntry(&entry_ptr))
        {
            for (auto iter = joinable_thread.begin(); iter != joinable_thread.end(); )
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

            if (ADK_UNLIKELY(0 == joinable_thread.size()))
            {
                break;
            }

            continue;
        }

        char* const buffer = entry_ptr->buffer;
        KeyValuePair<array<char, 6>, array<char, 10>, int64_t>* const paired = (KeyValuePair<array<char, 6>, array<char, 10>, int64_t>*)buffer;

        const int32_t result = hm_object->Find(paired->key1, paired->key2, &value_ptr);
        BOOST_CHECK_EQUAL(result, ErrorCode::kSuccess);
        BOOST_CHECK_EQUAL(*value_ptr, paired->value);
        unbounded_queue->FreeEntry(entry_ptr);
        ++element_index;
    }

    std::cout << "Compare completely, size = <" << element_index << ">" << std::endl;
}

