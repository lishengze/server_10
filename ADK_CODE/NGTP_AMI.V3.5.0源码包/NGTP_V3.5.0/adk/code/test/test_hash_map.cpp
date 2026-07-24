#include <adk/hash_map.h>

#include <adk/util.h>
#include <iostream>
#include <boost/unordered/unordered_map.hpp>

using std::cout;
using std::endl;

using namespace adk;

volatile bool g_is_run = false;
const uint32_t TEST_TIME = 1000000;
const uint32_t THREAD_NUM = 4;

void InsertThread(uint32_t thread_index, HashMap<string,uint8_t,string>* hash_map)
{
    char str_temp[20] = {0};
    string key, value;

    uint32_t round_counter = 0;

    while(!g_is_run);

    cout << "insert_thread = " << thread_index
         << ", begin running" << endl;

    boost::function<bool(const string&)> call_back = [](const string&)->bool {
        return false;
    };

    while(true)
    {
        for (uint32_t index=0; index<TEST_TIME; ++index)
        {
            sprintf(str_temp, "%19d", index<<thread_index);

            key = string("key_") + str_temp;
            value = string("value_") + str_temp;
            if (kSuccess != hash_map->Insert(key, (uint8_t)0, value, &call_back))
            {
                cout << "Insert failed in insert_thread = " << thread_index
                    << ", key = " << key
                    << ", value = " << value
                    << endl;
                break;
            }
        }

        cout << "insert_thread = " << thread_index
             << ", test round = " << round_counter ++
             << " completed!"<< endl;
    } 
}

void FindThread(uint32_t thread_index, HashMap<string,uint8_t,string>* hash_map)
{
    char str_temp[20] = {0};
    string key, value;
    string* get_value;

    uint32_t round_counter = 0;

    while(!g_is_run);

    cout << "find_thread = " << thread_index
         << ", begin running" << endl;

    while(true)
    {
        for (uint32_t index=0; index<TEST_TIME; ++index)
        {
            sprintf(str_temp, "%19d", index<<thread_index);

            key = string("key_") + str_temp;
            value = string("value_") + str_temp;
            if (kSuccess != hash_map->Find(key, (uint8_t)0, &get_value))
            {
                // cout << "Find failed in find_thread = " << thread_index
                //     << ", key = " << key << endl;
            }
            else
            {
                if(*get_value != value)
                {
                    cout << "check failed in find_thread = " << thread_index
                    << ", key = " << key
                    << ", get_value = " << get_value->c_str()
                    << ", value = " << value
                    << endl;
                }
            }
        }

        cout << "find_thread = " << thread_index
             << ", test round = " << round_counter ++
             << " completed!"<< endl;
    }
}

#define TEST_HASH_MAP 1

int main()
{
    HashMap<string,uint8_t,string>* hash_map = HashMap<string,uint8_t,string>::Create(TEST_TIME<<1, 4, 8);
    if (NULL == hash_map)
    {
        cout << "create hash map failed" << endl;
    }

    //HashMap<boost::array<char,6>,uint8_t,string>* hash_map1 = HashMap<boost::array<char,6>,uint8_t,string>::Create(TEST_TIME<<1, 4, 8);
    //HashMap<boost::array<char,6>,boost::array<char,10>,string>* hash_map2 = HashMap<boost::array<char,6>,boost::array<char,10>,string>::Create(TEST_TIME<<1, 4, 8);
#if 1
    boost::thread insert_id[THREAD_NUM];
    boost::thread find_id[THREAD_NUM];

    for (uint32_t thread_index=0; thread_index<THREAD_NUM; ++thread_index)
    {
        insert_id[thread_index] = boost::thread(boost::bind(InsertThread, thread_index, hash_map));
        find_id[thread_index] = boost::thread(boost::bind(FindThread, thread_index, hash_map));
    }

    sleep(1);
    g_is_run = true;

    cout << "Start test ..." << endl;

    find_id[0].join();

    return 0;
#endif
    boost::unordered_map<string,string> _unordered_map(TEST_TIME<<1);

    char str_input[20] = {0};
    string* key_buffer = new string[TEST_TIME];
    string* value_buffer = new string[TEST_TIME];
    for (uint32_t index=0; index<TEST_TIME; ++index)
    {
        sprintf(str_input, "%19d", index);
        key_buffer[index] = str_input;
        value_buffer[index] = str_input;
    }

    struct timespec time_cur_begin;
    struct timespec time_cur_end;
    clock_gettime(CLOCK_REALTIME, &time_cur_begin);
    for (uint32_t index=0; index<TEST_TIME; ++index)
    {
#if TEST_HASH_MAP     
        if (kSuccess != (*hash_map).Insert(key_buffer[index], (uint8_t)0, value_buffer[index]))
        {
            cout << "Insert failed!"
                 << ", key = " << key_buffer[index]
                 << ", value = " << value_buffer[index]
                 << endl;
        } 
#else
        _unordered_map[key_buffer[index]] = value_buffer[index];
#endif        
    }

    cout << "collision time = " << hash_map->collision_time_ << endl;
    for (uint16_t index=0; index<10; ++index)
    {
        uint32_t usage = hash_map->usage_counter_[index];
        if (0 == usage)
        {
            break;
        }

        cout << "deepth = " << index
             << ", usage = " << usage << endl;
    }

    clock_gettime(CLOCK_REALTIME, &time_cur_end);
    cout << "Insert complete! Cost time = " << time_diff(time_cur_end, time_cur_begin) << endl;

    clock_gettime(CLOCK_REALTIME, &time_cur_begin);
    for (uint32_t index=0; index<TEST_TIME; ++index)
    {
#if TEST_HASH_MAP     
        if (kSuccess != (*hash_map).Insert(key_buffer[index],(uint8_t)0, value_buffer[index]))
        {
            cout << "Insert failed!"
                 << ", key = " << key_buffer[index]
                 << ", value = " << value_buffer[index]
                 << endl;
        } 
#else
        _unordered_map[key_buffer[index]] = value_buffer[index];
#endif        
    }

    cout << "collision time = " << hash_map->collision_time_ << endl;
    for (uint16_t index=0; index<10; ++index)
    {
        uint32_t usage = hash_map->usage_counter_[index];
        if (0 == usage)
        {
            break;
        }

        cout << "deepth = " << index
             << ", usage = " << usage << endl;
    }

    clock_gettime(CLOCK_REALTIME, &time_cur_end);
    cout << "Insert complete! Cost time = " << time_diff(time_cur_end, time_cur_begin) << endl;


    clock_gettime(CLOCK_REALTIME, &time_cur_begin);
    string *getValue;
    for (uint32_t index=0; index<TEST_TIME; ++index)
    {
#if TEST_HASH_MAP
        if (kSuccess != hash_map->Find(key_buffer[index], (uint8_t)0, &getValue))
        {
            cout << "Find key = " << key_buffer[index]
                 << ", failed!" << endl;
            continue;
        }
#else
        getValue = &(_unordered_map[key_buffer[index]]);
#endif
        if (value_buffer[index] != *getValue)
        {
            cout << "Find key = " << key_buffer[index]
                 << ", getValue = " << getValue->c_str()
                 << ", store_value = " << value_buffer[index]
                 << endl;
        }
    }
    clock_gettime(CLOCK_REALTIME, &time_cur_end);

    cout << "Find complete! Cost time = " << time_diff(time_cur_end, time_cur_begin) << endl;

    return 0;
}