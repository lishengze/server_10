#include <thread>
#include <iostream>
#include <vector>
#include <pthread.h>
#include <adk/lock_free_cont_memory.h>
#include <adk/arch/synchronize.h>

#include <mutex>

using adk_impl::ErrorCode;
using adk_impl::ContEntry;
using adk_impl::ContinueMemory;
using adk_impl::LightWeightSpinLock;

//uint64_t g_test_counter1 = 0;
//uint64_t g_test_counter2 = 0;
//uint64_t g_test_counter3 = 0;
//uint32_t g_allocate_size = 128;
//volatile bool g_is_running = false;

// 用一个自选锁来保护入队
LightWeightSpinLock lock;

// 用一个 map 来存每个线程的出队 index
std::map<pthread_t, uint32_t> last_index;
// 保护这个 map
std::mutex map_lock;

std::mutex print_lock;

// 是否开启打印
bool print = false;

struct Info
{
    pthread_t tid{0};
    uint32_t ind{0};
};


// 生产者线程的数量
constexpr int ProducerNum = 10;

// 每个生产者写入的消息数量
uint32_t total_times = 100000;

void Producer(ContinueMemory* continue_memory)
{
    thread_local uint32_t i = 0;
    struct ContEntry* entry_ptr = nullptr;


    uint64_t backoff = 8;

    uint64_t times = total_times;

    while (times--)
    {
        while (ADK_UNLIKELY(ErrorCode::kSuccess != continue_memory->TryLockAllocEntry(sizeof(Info), &entry_ptr, lock)))
        {
            for (uint32_t i = 0; i < backoff; ++i)
            {
                ADK_PAUSE();
            }
        }


        Info* info = (Info*)(entry_ptr->GetBuffer());
        info->tid = pthread_self();
        info->ind = i++;

        if (print)
        {
            std::lock_guard<std::mutex> lock(print_lock);
            std::cout << "Producer | tid:" << info->tid << ", ind:" << info->ind << std::endl;
        }

        continue_memory->PostEntryThreadSafe(entry_ptr);
    }
}

void Consumer(ContinueMemory* continue_memory)
{
    struct ContEntry* entry_ptr = nullptr;


    uint64_t backoff = 32;
    uint64_t times = total_times * ProducerNum;

    while (times--)
    {
        while (ADK_UNLIKELY(ErrorCode::kSuccess != continue_memory->TryWaitEntry(&entry_ptr)))
        {
            for (uint32_t i = 0; i < backoff; ++i)
            {
                ADK_PAUSE();
            }
        }


        Info* info = (Info*)(entry_ptr->GetBuffer());

        auto tid = info->tid;
        auto ind = info->ind;

        if (print)
        {
            std::lock_guard<std::mutex> lock(print_lock);
            std::cout << "Consumer | tid:" << info->tid << ", ind:" << info->ind << std::endl;
        }

        map_lock.lock();
        // 从 map 中获取当前线程预期的下一条数据的 ind,
        // 与取到的数据做比较, 看是否一致
        if (last_index.find(tid) == last_index.end())
        {
            assert(0 == ind);
            last_index[tid] = 1;
        }
        else
        {
            auto ind2 = last_index[tid];
            assert(ind2 == ind);
            last_index[tid]++;
        }
        map_lock.unlock();
        continue_memory->FreeEntry(entry_ptr);
    }
}

std::vector<std::thread> producers;

int main(int argc, char* argv[])
{

    ContinueMemory* continue_memory = ContinueMemory::Create(16 * 8192, 64);
    assert(continue_memory);

    // 新建一个消费者线程
    std::thread consumer_thrd = std::thread(Consumer, continue_memory);
    
    // 新建两个生产者线程
    for (int i =0; i < ProducerNum; i++)
    {
        producers.emplace_back(std::thread(Producer, continue_memory));
    }

    consumer_thrd.join();
    for (auto& th : producers)
    {
        th.join();
    }

    return 0;
}
