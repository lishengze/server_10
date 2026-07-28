/*
 * @Author: zhangtian zhangtian@af.local
 * @Date: 2022-08-23 14:18:01
 * @LastEditors: zhangtian zhangtian@af.local
 * @LastEditTime: 2022-08-25 15:00:02
 * @FilePath: /workspace/adk/code/test/test_lock_free_unbounded_queue_v3.cpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include <boost/thread.hpp>

#include <adk/error_code.h>
#include <adk/lock_free_unbounded_queue.h>
#include <adk/lock_free_unbounded_queue_variant.h>
#include <adk/util.h>

#include <map>
#include <set>
#include <string>
#include <vector>
#include <sys/syscall.h>
#define gettid() syscall(__NR_gettid)

volatile bool is_bug_on = false;
using namespace adk;

// using QueueType = SPSCUnboundedQueue<uint64_t>;
constexpr uint64_t kTestCounter = 10000000ull * 600;
constexpr uint64_t kTestCounterWithDelay = 1000000 * 600;
constexpr int kMaxQueueThreadNum = 8;

struct QDate
{
    int64_t tid;
    uint64_t counter;
};


/**
 * spsc队列消费端
 * QueueType代表队列类型，kDoDelay为true代表开启延时
*/
template<typename QueueType, bool kDoDelay>
void QConcumser(QueueType* mq, uint64_t total_msgs)
{
    uint64_t value;
    uint64_t counter = 0;
    while (counter != total_msgs)
    {
        if (kDoDelay)
        {
            usleep(0);
        }

        if (mq->Pop(value) == ErrorCode::kSuccess)
        {
            if (value != counter + 1)
            {
                std::cout << "value: " << value << " counter + 1: " << counter + 1 << std::endl;
                abort();// 当发送来的消息与线程中的计数不符时代表存在数据丢失
                is_bug_on = true;
            }

            counter = value;
        }
    }
}

/**
 * spsc队列发送端
 * QueueType代表队列类型，kDoDelay为true代表开启延时
*/
template<typename QueueType, bool kDoDelay>
void QProducer(QueueType* mq, uint64_t total_msgs)
{
    uint64_t counter = 0;
    while (counter != total_msgs)
    {
        if (kDoDelay)
        {
            usleep(0);
        }

        ++counter;
        while (mq->Push(counter) != ErrorCode::kSuccess)
        {
            abort();
            is_bug_on = true;
        }
    }
}

/**
 * spsc队列消费端
 * QueueType代表队列类型，kDoDelay为true代表开启延时
*/
template<typename QueueType, bool kDoDelay>
void MpscQConcumser(QueueType* mq, uint64_t total_msgs)
{
    uint64_t counter = 0;
    QDate data;
    std::map<pthread_t, uint64_t> tid_msgs;

    uint64_t producer_sqns[kMaxQueueThreadNum + 1] = {0};

    while (counter != total_msgs)
    {
        if (kDoDelay)
        {
            usleep(0);
        }

        if (mq->Pop(data) == ErrorCode::kSuccess)
        {
            uint64_t &sqn = producer_sqns[data.tid];
            if (data.counter != ++(sqn))
            {
                std::cout << "data.counter: " << data.counter << " sqn: " << sqn << std::endl;
                abort();// 当发送来的消息与数组中保存的序号不符时代表存在数据丢失
                is_bug_on = true;
            }
            ++counter;
        }
    }
}


/**
 * mpsc队列发送端
 * QueueType代表队列类型，tid代表需要传入的线程id（手动编号），kDoDelay为true代表开启延时
*/
template<typename QueueType, bool kDoDelay>
void MpscQProducer(QueueType* mq, int64_t tid, uint64_t total_msgs)
{
    struct QDate data;
    uint64_t counter = 0;

    while (counter != total_msgs)
    {
        if (kDoDelay)
        {
            usleep(0);
        }
        ++counter;

        data.counter = counter;
        data.tid = tid;
        while (mq->Push(data) != ErrorCode::kSuccess)
        {
            abort();
            is_bug_on = true;
        }
    }
}

void SPSCUnboundedQueueTest()
{
    std::cout << "start SPSCUnboundedQueue test, element counter: " << kTestCounter << std::endl;

    is_bug_on = false;
    boost::thread p[10];
    boost::thread c[10];
    // 多线程测试，同时创建10个线程跑同样的逻辑
    for (uint32_t index = 0; index < 10; ++index)
    {
        SPSCUnboundedQueue<uint64_t>* mq_p = SPSCUnboundedQueue<uint64_t>::Create("test_spsc");

        p[index] = boost::thread(QProducer<SPSCUnboundedQueue<uint64_t>, false>, mq_p, kTestCounter);
        c[index] = boost::thread(QConcumser<SPSCUnboundedQueue<uint64_t>, false>, mq_p, kTestCounter);
    }
    for (uint32_t index = 0; index < 10; ++index)
    {
        p[index].join();
	    c[index].join();
    }
    if (is_bug_on == true)
    {
        abort();
    }
}

void VariantSPSCUnboundedQueueTest()
{
    std::cout << "start VariantSPSCUnboundedQueue test, element counter: " << kTestCounter << std::endl;

    is_bug_on = false;
    boost::thread p[10];
    boost::thread c[10];
    for (uint32_t index = 0; index < 10; ++index)
    {
        adk::variant::SPSCUnboundedQueue<uint64_t>* mq_p = adk::variant::SPSCUnboundedQueue<uint64_t>::Create("test_spsc");

        p[index] = boost::thread(QProducer<adk::variant::SPSCUnboundedQueue<uint64_t>, false>, mq_p, kTestCounter);
        c[index] = boost::thread(QConcumser<adk::variant::SPSCUnboundedQueue<uint64_t>, false>, mq_p, kTestCounter);
    }
    for (uint32_t index = 0; index < 10; ++index)
    {
        p[index].join();
	    c[index].join();
    }
    if (is_bug_on == true)
    {
        abort();
    }
}

void MPSCUnboundedQueueTest()
{
    std::cout << "start MPSCUnboundedQueue test, element counter: " << kTestCounter << std::endl;

    is_bug_on = false;
    boost::thread p[10];
    boost::thread p2[10];
    boost::thread c[10];
    for (uint32_t index = 0; index < 10; ++index)
    {
        adk::variant::MPSCUnboundedQueue<QDate>* mq_p = adk::variant::MPSCUnboundedQueue<QDate>::Create("test_spsc");

        p[index] = boost::thread(MpscQProducer<adk::variant::MPSCUnboundedQueue<QDate>, false>, mq_p, 1, kTestCounter/2);
        p2[index] = boost::thread(MpscQProducer<adk::variant::MPSCUnboundedQueue<QDate>, false>, mq_p, 2, kTestCounter/2);
        c[index] = boost::thread(MpscQConcumser<adk::variant::MPSCUnboundedQueue<QDate>, false>, mq_p, kTestCounter);
    }
    for (uint32_t index = 0; index < 10; ++index)
    {
        p[index].join();
        p2[index].join();
    	c[index].join();
    }
    if (is_bug_on == true)
    {
        abort();
    }
}

int main(int argc, char const *argv[])
{
    std::string test_name(argv[1]);
    if (test_name == "spsc")
    {
        SPSCUnboundedQueueTest();
    }
    else if (test_name == "vaspsc")
    {
        VariantSPSCUnboundedQueueTest();
    }
    else if (test_name == "mpsc")
    {
        MPSCUnboundedQueueTest();
    }
    return 0;
}