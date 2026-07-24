#define BOOST_TEST_MODULE lock_free_cont_memory_mp
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk/error_code.h>
#include <adk/lock_free_cont_memory.h>
#include <adk/arch/synchronize.h>
#include <adk/util.h>

#include <map>
#include <set>
#include <string>
#include <vector>
#include <sys/syscall.h>
#define gettid() syscall(__NR_gettid)

volatile bool is_bug_on = false;
using namespace adk;

using adk::ErrorCode;
using adk::ContEntry;
using adk::ContinueMemory;
using adk::LightWeightSpinLock;
LightWeightSpinLock lock;

// using QueueType = SPSCUnboundedQueue<uint64_t>;
constexpr uint64_t kTestCounter = 1000000ull * 6;
constexpr uint64_t kTestCounterWithDelay = 1000000 * 600;
constexpr int kMaxQueueThreadNum = 8;

struct QDate
{
    int64_t tid;
    uint64_t counter;
};


/**
 * 队列消费端
 * QueueType代表队列类型，kDoDelay为true代表开启延时
*/
template<typename QueueType, bool kDoDelay>
void MpscQConcumser(QueueType* mq, uint64_t total_msgs)
{
    uint64_t counter = 0;
    QDate data;
    struct ContEntry* entry_ptr = nullptr;
    // 创建一个数组用于保存来自不同线程的发送端当前消息序号
    uint64_t producer_sqns[kMaxQueueThreadNum + 1] = {0};

    while (counter != total_msgs)
    {
        if (kDoDelay)
        {
            usleep(0);
        }

        
        if (ADK_UNLIKELY(ErrorCode::kSuccess == mq->TryWaitEntry(&entry_ptr)))
        {
            QDate* data = (QDate*)(entry_ptr->GetBuffer());

            uint64_t &sqn = producer_sqns[data->tid];
            if (data->counter != ++(sqn))
            {
                std::cout << "data.counter: " << data->counter << " sqn: " << sqn << std::endl;
                abort();// 当发送来的消息与数组中保存的序号不符时代表存在数据丢失
                is_bug_on = true;
            }
            ++counter;
            mq->FreeEntry(entry_ptr);
        }
    }
}


/**
 * 队列发送端
 * QueueType代表队列类型，tid代表需要传入的线程id（手动编号），kDoDelay为true代表开启延时
*/
template<typename QueueType, bool kDoDelay>
void MpscQProducer(QueueType* mq, int64_t tid, uint64_t total_msgs)
{
    struct ContEntry* entry_ptr = nullptr;
    uint64_t counter = 0;
    uint64_t backoff = 8;


    while (!is_bug_on && counter != total_msgs)
    {
        if (kDoDelay)
        {
            usleep(0);
        }
        while (ADK_UNLIKELY(ErrorCode::kSuccess != mq->TryLockAllocEntry(sizeof(QDate), &entry_ptr, lock)))
        {
            for (uint32_t i = 0; i < backoff; ++i)
            {
                ADK_PAUSE();
            }
        }
        ++counter;
        QDate* data = (QDate*)(entry_ptr->GetBuffer());

        data->counter = counter;
        data->tid = tid;
        mq->PostEntryThreadSafe(entry_ptr);
    }
}

/**
 * @brief MPSCUnboundedQueue 队列多线程生产 单线程消费测试样例
 * 
 */
BOOST_AUTO_TEST_CASE(mpscqueue_test)
{
    std::cout << "start ContinueMemoryTest test, element counter: " << kTestCounter << std::endl;

    is_bug_on = false;
    boost::thread p[10];// 发送端线程组1
    boost::thread p2[10];// 发送端线程组2
    boost::thread c[10];
    for (uint32_t index = 0; index < 10; ++index)
    {
        ContinueMemory* mq_p = ContinueMemory::Create(16 * 8192, 64);
        assert(mq_p);

        p[index] = boost::thread(MpscQProducer<ContinueMemory, false>, mq_p, 1, kTestCounter/2);
        p2[index] = boost::thread(MpscQProducer<ContinueMemory, false>, mq_p, 2, kTestCounter/2);
        c[index] = boost::thread(MpscQConcumser<ContinueMemory, false>, mq_p, kTestCounter);
    }
    for (uint32_t index = 0; index < 10; ++index)
    {
        p[index].join();
        p2[index].join();
    	c[index].join();
    }
    BOOST_REQUIRE(is_bug_on == false);
}

