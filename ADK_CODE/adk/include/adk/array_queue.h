#ifndef ADK_ARRAY_QUEUE_H_
#define ADK_ARRAY_QUEUE_H_

#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>
#include <typeinfo>
#include <vector>

#include "arch/generic.h"
#include "arch/synchronize.h"
#include "lock_free_msg_queue.h"
#include "shm_anon.h"
#include "shm.h"

namespace adk_impl
{
struct Item
{
    MPSCQueue* queue;
    bool is_sp_channel;
};

struct Slot
{
    uint32_t index;
    uint32_t buget;
    uint32_t heat;
};

#define ARRAY_QUEUE_TYPE_SP 0x00001ul
#define ARRAY_QUEUE_TYPE_MP 0x00002ul
#define ARRAY_QUEUE_TYPE_MASK (~(ARRAY_QUEUE_TYPE_SP | ARRAY_QUEUE_TYPE_MP))

#define ARRAY_QUEUE_MAKE_TYPE_SP(c_ptr) ((decltype(c_ptr))((uint64_t)c_ptr | ARRAY_QUEUE_TYPE_SP))
#define ARRAY_QUEUE_MAKE_TYPE_MP(c_ptr) ((decltype(c_ptr))((uint64_t)c_ptr | ARRAY_QUEUE_TYPE_MP))
#define ARRAY_QUEUE_GET_PTR(c_ptr) ((decltype(c_ptr))((uint64_t)c_ptr & ARRAY_QUEUE_TYPE_MASK))

#define ARRAY_QUEUE_IS_SP(c_ptr) ((decltype(c_ptr))((uint64_t)c_ptr & ARRAY_QUEUE_TYPE_SP))
#define ARRAY_QUEUE_IS_MP(c_ptr) ((decltype(c_ptr))((uint64_t)c_ptr & ARRAY_QUEUE_TYPE_MP))

struct ArrayQueueHeader
{
    uint32_t nr_queue_used = 0;
    uint32_t nr_threads = 0;
    Spinlock spinlock;
};

// T: 队列中的元素类型
// N: 线程数量（数组容量N+N/4），前N个线程独享SPSC队列，后面的线程round robin多个MPSC队列
// U: 唯一性标识模板参数，当前两个模板参数一样且需要多个该类型实例时可由此参数区分
template <typename T, size_t N = 8, size_t U = 0>
class ArrayQueue
{
public:
    // 单例模式，获取实例对象指针
    static inline ArrayQueue<T, N, U>* GetInstance(bool is_shm = false, const std::string shm_name = "")
    {
        // SPSC与MPSC队列8/2分，MPSC队列数向上取整，为0则取1
        uint32_t nr_mp_queue = N / 4 == 0 ? 1 : std::ceil(N / 4.0);
        if (!is_shm)
        {
            // 内存构造对象
            static ArrayQueueHeader* header = new ArrayQueueHeader();
            SpinlockInit(header->spinlock);
            static ArrayQueue<T, N, U>* aq = new ArrayQueue<T, N, U>(nr_mp_queue, header);
            return aq;
        }
        else
        {
            // 共享内存模式
            uint32_t capacity   = N + nr_mp_queue;
            // | ArrayQueue | nr_queue_used_ | ArrayQueue::prio_ | padding | ArrayQueue::queues_ |
            uint32_t shm_size   = sizeof(ArrayQueueHeader);

            void* ptr = nullptr;
            if (shm_name.empty())
            {
                // 单例，但类型不同，对象要指向不同的共享内存块，名称需要不一样
                // anon_shm_name相同，则得到的是同一块匿名共享内存区域，这部分由AnonShmFactory保证
                std::string anon_shm_name = "ArrayQueueShm_" + std::string(typeid(T).name()) + std::to_string(N) + std::to_string(U);
                ptr = AnonShmFactory::CreateShm(anon_shm_name, shm_size);
            }
            else
            {
                // 命名共享内存，可以被Attach
                shm_unlink(shm_name.c_str());
                ptr = ShmFactory::Create(shm_name, shm_size);
            }

            assert(ptr != nullptr);
            static ArrayQueueHeader* head = new (ptr)ArrayQueueHeader();
            SpinlockInit(head->spinlock);
            static ArrayQueue<T, N, U>* aq = new ArrayQueue<T, N, U>(nr_mp_queue, head);
            return aq;
        }
    }

    // Attach 方法实际只得到了 ArrayQueue 类结构，存储的 SPSC/MPSC 队列需要用户自行 attach
    // ArrayQueue 类结构内存区域并不在共享内存上，只有 nr_queue_used_ 指针指向共享内存
    static inline ArrayQueue<T, N, U>* Attach(const std::string& shm_name)
    {
        void* ptr = ShmFactory::Attach(shm_name);
        if (ptr == nullptr)
        {
            return nullptr;
        }

        uint32_t nr_mp_queue = N / 4 == 0 ? 1 : std::ceil(N / 4.0);
        // | ArrayQueue | nr_queue_used_ | ArrayQueue::prio_ | padding | ArrayQueue::queues_ |
        // nr_queue_used在pop时需要用到，Attach仅需获取共享内存中的nr_queue_used
        static ArrayQueue<T, N, U>* aq = new ArrayQueue<T, N, U>(nr_mp_queue, (ArrayQueueHeader*)ptr);
        return aq;
    }


    static inline ArrayQueue<T, N, U>* Attach(const std::string& shm_name, std::function<MPSCQueue*(uint32_t)> attachQueue)
    {
        void* ptr = ShmFactory::Attach(shm_name);
        if (ptr == nullptr)
        {
            return nullptr;
        }

        uint32_t nr_mp_queue = N / 4 == 0 ? 1 : std::ceil(N / 4.0);
        // | ArrayQueue | nr_queue_used_ | ArrayQueue::prio_ | padding | ArrayQueue::queues_ |
        // nr_queue_used在pop时需要用到，Attach仅需获取共享内存中的nr_queue_used
        static ArrayQueue<T, N, U>* aq = new ArrayQueue<T, N, U>(nr_mp_queue, (ArrayQueueHeader*)ptr);
        aq->Init(attachQueue);
        return aq;
    }

    ~ArrayQueue() = default;

    // 初始化，需指定创建队列的函数
    int32_t Init(std::function<MPSCQueue*(uint32_t)> createQueue)
    {
        for (uint32_t i = 0; i < N + nr_mp_queue_; ++i)
        {
            // 初始优先级按队列顺序排列
            prio_[i].index           = i;
            prio_[i].buget           = 0;
            prio_[i].heat            = 0;
            queues_[i].is_sp_channel = true;
            queues_[i].queue         = createQueue(i);

            if (queues_[i].queue == nullptr)
            {
                return ErrorCode::kNoMemory;
            }

            if (i >= N)
            {
                queues_[i].is_sp_channel = false;
            }
        }
        return ErrorCode::kSuccess;
    }

    inline int32_t Push(const T& payload)
    {
    push_begin:
        auto* item = ACCESS_ONCE(queue_);
        auto* ptr  = ARRAY_QUEUE_GET_PTR(item);
        if (ADK_LIKELY(ARRAY_QUEUE_IS_SP(item)))
        {
            adk::SPSCQueue<T>* const spsc_queue = reinterpret_cast<adk::SPSCQueue<T>*>(ptr->queue);
            return spsc_queue->Push(payload);
        }
        else if (ARRAY_QUEUE_IS_MP(item))
        {
            MPSCQueue* const mpsc_queue = ptr->queue;
            return mpsc_queue->Push(payload);
        }

        assert(ptr == nullptr);
        allocQueue();
        goto push_begin;
    }

    inline int32_t Pop(T& payload)
    {
        // ArrayQueue整体为一个MPSC，Pop侧只能单线程操作
        // 当前遍历到的队列索引，索引实际为优先级数组prio_的下标
        // prio_[curr].index 为实际队列数组的下标
        static uint32_t curr   = 0;
        static uint32_t nr_pop = 0;
        int32_t ret            = queues_[prio_[curr].index].queue->Pop(payload);
        uint32_t nr_queue_used = header_->nr_queue_used;
        if (ret != ErrorCode::kSuccess)
        {
            for (uint32_t i = 1; i < nr_queue_used; ++i)
            {
                // 数组完全遍历一遍仍失败表示队列数组为空
                curr = (curr + 1) == nr_queue_used ? 0 : curr + 1;
                ret  = queues_[prio_[curr].index].queue->Pop(payload);
                if (ret == ErrorCode::kSuccess)
                {
                    break;
                }
            }
        }

        if (ret == ErrorCode::kSuccess)
        {
            // nr_pop表示当前累计的pop数量，记录历史，达到100w时计算队列优先级，并重置优先级判断相关数据
            // buget是为了防止饥饿现象，一个队列连续pop出64个元素后强制跳转到下一队列
            // heat用于计算优先级，100w以内heat越多表示该队列越活跃，可能有数据概率越高，优先级越高
            ++nr_pop;
            ++prio_[curr].buget;
            ++prio_[curr].heat;
            if (prio_[curr].buget == 64)
            {
                prio_[curr].buget = 0;
                curr              = (curr + 1) >= nr_queue_used ? 0 : curr + 1;
            }
        }
        else
        {
            // Pop failed, array_queue is empty
            if (nr_pop >= 1000000)
            {
                std::sort(prio_, prio_ + N + nr_mp_queue_, [](Slot a, Slot b) {
                    return a.heat > b.heat;
                });
                for (uint32_t i = 0; i < nr_queue_used; ++i)
                {
                    prio_[i].heat  = 0;
                    prio_[i].buget = 0;
                }
                nr_pop = 0;
                curr   = 0;
            }
        }

        return ret;
    }

    inline int32_t AllocEntry(struct Entry** entry_pptr)
    {
    begin:
        auto* item = ACCESS_ONCE(queue_);
        auto* ptr  = ARRAY_QUEUE_GET_PTR(item);
        if (ADK_LIKELY(ARRAY_QUEUE_IS_SP(item)))
        {
            adk::SPSCQueue<T>* const spsc_queue = reinterpret_cast<adk::SPSCQueue<T>*>(ptr->queue);
            return spsc_queue->AllocEntry(entry_pptr);
        }
        else if (ARRAY_QUEUE_IS_MP(item))
        {
            MPSCQueue* const mpsc_queue = ptr->queue;
            return mpsc_queue->AllocEntry(entry_pptr);
        }

        assert(ptr == nullptr);
        allocQueue();
        goto begin;
    }

    inline int32_t PostEntry(struct Entry* entry)
    {
    begin:
        auto* item = ACCESS_ONCE(queue_);
        auto* ptr  = ARRAY_QUEUE_GET_PTR(item);
        if (ADK_LIKELY(ARRAY_QUEUE_IS_SP(item)))
        {
            adk::SPSCQueue<T>* const spsc_queue = reinterpret_cast<adk::SPSCQueue<T>*>(ptr->queue);
            return spsc_queue->PostEntry(entry);
        }
        else if (ARRAY_QUEUE_IS_MP(item))
        {
            MPSCQueue* const mpsc_queue = ptr->queue;
            return mpsc_queue->PostEntry(entry);
        }

        assert(ptr == nullptr);
        allocQueue();
        goto begin;
    }

    inline int32_t WaitEntry(struct Entry** entry_pptr)
    {
    begin:
        auto* item = ACCESS_ONCE(queue_);
        auto* ptr  = ARRAY_QUEUE_GET_PTR(item);
        if (ADK_LIKELY(ARRAY_QUEUE_IS_SP(item)))
        {
            adk::SPSCQueue<T>* const spsc_queue = reinterpret_cast<adk::SPSCQueue<T>*>(ptr->queue);
            return spsc_queue->WaitEntry(entry_pptr);
        }
        else if (ARRAY_QUEUE_IS_MP(item))
        {
            MPSCQueue* const mpsc_queue = ptr->queue;
            return mpsc_queue->WaitEntry(entry_pptr);
        }

        assert(ptr == nullptr);
        allocQueue();
        goto begin;
    }

    inline int32_t FreeEntry(struct Entry* entry)
    {
    begin:
        auto* item = ACCESS_ONCE(queue_);
        auto* ptr  = ARRAY_QUEUE_GET_PTR(item);
        if (ADK_LIKELY(ARRAY_QUEUE_IS_SP(item)))
        {
            adk::SPSCQueue<T>* const spsc_queue = reinterpret_cast<adk::SPSCQueue<T>*>(ptr->queue);
            return spsc_queue->FreeEntry(entry);
        }
        else if (ARRAY_QUEUE_IS_MP(item))
        {
            MPSCQueue* const mpsc_queue = ptr->queue;
            return mpsc_queue->FreeEntry(entry);
        }

        assert(ptr == nullptr);
        allocQueue();
        goto begin;
    }

    MPSCQueue* GetQueue(uint32_t index) const
    {
        uint32_t offset = index % (N + nr_mp_queue_);
        return queues_[offset].queue;
    }

    inline uint32_t GetNrQueueUsed() const
    {
        return header_->nr_queue_used;
    }

    inline uint32_t GetCapacity() const
    {
        return N + nr_mp_queue_;
    }


private:
    ArrayQueueHeader* header_;
    Slot* prio_;   // 消费优先级队列，消费端使用
    Item* queues_; 
    uint32_t nr_mp_queue_;

    static __thread Item* queue_;

    // 内存模式构造函数，数组在堆上申请
    ArrayQueue(uint32_t nr_mp_queue, ArrayQueueHeader* header) : 
        nr_mp_queue_(nr_mp_queue),
        header_(header)
    {
        prio_   = new Slot[N + nr_mp_queue_];
        queues_ = new Item[N + nr_mp_queue_];
    }

    void allocQueue()
    {
        SpinlockLock(header_->spinlock);
        // 每次进入此函数表示线程数量增加
        ++header_->nr_threads;
        if (header_->nr_threads <= N)
        {
            // 线程数不大于N，每个线程独享一个SPSC队列
            queue_ = ARRAY_QUEUE_MAKE_TYPE_SP(&queues_[header_->nr_queue_used]);
            ++header_->nr_queue_used;
        }
        else
        {
            // 线程数大于N，N/4个MPSC队列进行round robin
            uint32_t rb_offset = (header_->nr_threads - 1 - N) % nr_mp_queue_;
            queue_             = ARRAY_QUEUE_MAKE_TYPE_MP(&queues_[N + rb_offset]);
            if (header_->nr_threads <= N + nr_mp_queue_)
            {
                // 线程数不大于队列总数量
                ++header_->nr_queue_used;
            }
            else
            {
                assert(header_->nr_queue_used == N + nr_mp_queue_);
            }
        }
        SpinlockUnlock(header_->spinlock);
    }
};

template <typename T, size_t N, size_t U>
__thread Item* ArrayQueue<T, N, U>::queue_ = nullptr;
}

#endif