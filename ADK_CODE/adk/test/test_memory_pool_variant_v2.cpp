
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <vector>
#include <utility>
#include <iostream>

#include <time.h>
#include <unistd.h>
#include <adk/memory_pool_variant.h>

enum MemBlockSize
{
    kMemBlockSize1K = 1024,
    kMemBlockSize2K = 2048,
    kMemBlockSize8K = 8192,
};

enum MemBlockNum
{
    kMemBlockNum1K = 8192,
    kMemBlockNum2K = 4096,
    kMemBlockNum8K = 1024,
};

const std::map<uint32_t, std::pair<uint32_t, std::string>> kMemoryPoolProperty = 
{
    { kMemBlockSize1K, std::make_pair(kMemBlockNum1K, "MemoryPool1K") },
    { kMemBlockSize2K, std::make_pair(kMemBlockNum2K, "MemoryPool2K") },
    { kMemBlockSize8K, std::make_pair(kMemBlockNum8K, "MemoryPool8K") }
};

using MemoryPool = adk::variant::MemoryPool<adk::variant::MPMCQueue>;

struct ActorMeta;

std::mutex g_observe_lock;
std::vector<ActorMeta*> g_observe_vec;

struct ActorMeta
{
    std::string actor_name;
    uint64_t counter;
    uint64_t time_diff_new[4];
    uint64_t time_diff_delete[4];
    uint64_t counter_s;
    uint64_t time_diff_new_s[4];
    uint64_t time_diff_delete_s[4];

    ActorMeta(const std::string& name)
    {
        actor_name = name;
        counter = 0;
        memset(&time_diff_new, 0, sizeof(time_diff_new));
        memset(&time_diff_delete, 0, sizeof(time_diff_delete));

        counter_s = 0;
        memset(&time_diff_new_s, 0, sizeof(time_diff_new_s));
        memset(&time_diff_delete_s, 0, sizeof(time_diff_delete_s));

        std::lock_guard<std::mutex> _(g_observe_lock);
        g_observe_vec.push_back(this);
    }

    ~ActorMeta()
    {
        std::lock_guard<std::mutex> _(g_observe_lock);
        const auto iter = std::find(g_observe_vec.begin(), g_observe_vec.end(), this);
        assert(g_observe_vec.end() != iter);
        g_observe_vec.erase(iter);
    }
};

void Actor1(MemoryPool* memory_pool, const std::string& actor_name)
{
    ActorMeta meta(actor_name);
    uint16_t test_size_array[4] = { 512, 1024, 2048, 4096 };

    do
    {
        struct timespec tp1;
        struct timespec tp2;
        for (int32_t index = 0; index < 4; ++index)
        {
            clock_gettime(CLOCK_REALTIME, &tp1);    
            auto* const memory = memory_pool->NewMemory(test_size_array[index]);
            clock_gettime(CLOCK_REALTIME, &tp2);
            meta.time_diff_new[index] += ((tp2.tv_sec - tp1.tv_sec) * 1000000000 + (tp2.tv_nsec - tp1.tv_nsec));
            for (int32_t temp = 0; temp < 128; ++temp)
            {
                ADK_PAUSE();
            }

            clock_gettime(CLOCK_REALTIME, &tp1);
            memory_pool->DeleteMemory(memory);
            clock_gettime(CLOCK_REALTIME, &tp2);
            meta.time_diff_delete[index] += ((tp2.tv_sec - tp1.tv_sec) * 1000000000 + (tp2.tv_nsec - tp1.tv_nsec));
        }
        
        ++meta.counter;
     } while (true);
}

void Actor2(MemoryPool* memory_pool, const std::string& actor_name)
{
    ActorMeta meta(actor_name);
    uint16_t test_size_array[4] = { 512, 1024, 2048, 4096 };

    do
    {
        struct timespec tp1;
        struct timespec tp2;
        for (int32_t index = 0; index < 4; ++index)
        {
            clock_gettime(CLOCK_REALTIME, &tp1);
            auto* const memory = memory_pool->NewMemory(test_size_array[index]);
            memset(memory, 0x12345678, test_size_array[index]);
            clock_gettime(CLOCK_REALTIME, &tp2);
            meta.time_diff_new[index] += ((tp2.tv_sec - tp1.tv_sec) * 1000000000 + (tp2.tv_nsec - tp1.tv_nsec));
            for (int32_t temp = 0; temp < 128; ++temp)
            {
                ADK_PAUSE();
            }

            clock_gettime(CLOCK_REALTIME, &tp1);
            memory_pool->DeleteMemory(memory);
            clock_gettime(CLOCK_REALTIME, &tp2);
            meta.time_diff_delete[index] += ((tp2.tv_sec - tp1.tv_sec) * 1000000000 + (tp2.tv_nsec - tp1.tv_nsec));
        }

        ++meta.counter;
     } while (true);
}

int main()
{
    MemoryPool* memory_pool = MemoryPool::Create(kMemoryPoolProperty);
    if (NULL == memory_pool)
    {
        std::cout << "Create memory pool failed" << std::endl;
    }

    std::thread actor1_thread = std::thread(Actor1, memory_pool, "Actor1");
    //std::thread actor2_thread = std::thread(Actor2, memory_pool, "Actor2");

    uint64_t time_diff_new[4];
    uint64_t time_diff_delete[4];
    uint16_t test_size_array[4] = { 512, 1024, 2048, 4096 };

    do
    {
        {    
        std::lock_guard<std::mutex> _(g_observe_lock);
        for (auto& node : g_observe_vec)
        {
             const auto counter = ACCESS_ONCE(node->counter);
             for (int32_t index = 0; index < 4; ++index)
             {
                 time_diff_new[index] = *(volatile uint64_t*)(&(node->time_diff_new[index]));
                 time_diff_delete[index] = *(volatile uint64_t*)(&(node->time_diff_delete[index]));
             }

             const auto counter_diff = counter - node->counter_s;
             node->counter_s = node->counter;

             std::cout << node->actor_name << ", nr = " << counter_diff;
             for (int32_t index = 0; index < 4; ++index)
             {
                 std::cout << ", block_size = " << test_size_array[index] << " <new = " << (node->time_diff_new[index] - node->time_diff_new_s[index]) / counter_diff
                           << ", delete = " << (node->time_diff_delete[index] - node->time_diff_delete_s[index]) / counter_diff << ">";
                 node->time_diff_new_s[index] = time_diff_new[index];
                 node->time_diff_delete_s[index] = time_diff_delete[index];
             }

             std::cout << std::endl;
         }
         }
         sleep(1);    
     } while (true);

    return 0;
}
