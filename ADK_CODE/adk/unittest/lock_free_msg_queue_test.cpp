#define BOOST_TEST_MODULE lock_free_msg_queue
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk/error_code.h>
#include <adk/lock_free_msg_queue.h>
#include <adk/lock_free_unbounded_queue.h>
#include <adk/lock_free_unbounded_queue_variant.h>
#include <adk/util.h>

#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

volatile bool is_bug_on     = false;
volatile bool is_process_on = true;
bool is_producer_on[10]     = {true};
using namespace adk;

// using QueueType = SPSCUnboundedQueue<uint64_t>;
constexpr uint64_t kTestCounter          = 10000000ull * 6;
constexpr uint64_t kTestCounterWithDelay = 1000000 * 600;
constexpr int kMaxQueueThreadNum         = 8;

struct QDate
{
    int64_t tid;
    uint64_t counter;
};

template <typename QueueType, bool kDoDelay>
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
                is_bug_on = true;
                abort();
            }

            counter = value;
        }
    }
}

template <typename QueueType, bool kDoDelay>
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
        }
    }
}

template <typename QueueType, bool kDoDelay>
void SpmcQProducer(QueueType* mq, uint64_t total_msgs, uint64_t idx)
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
        }
    }
    sleep(2);
    is_producer_on[idx] = false;
}

template <typename QueueType, bool kDoDelay>
void SpmcQConcumser(QueueType* mq, SPSCUnboundedQueue<uint64_t>* mq_check, uint64_t idx)
{
    uint64_t value;
    bool& is_producer_work = is_producer_on[idx];
    while (is_producer_work)
    {
        if (kDoDelay)
        {
            usleep(0);
        }

        if (mq->Pop(value) == ErrorCode::kSuccess)
        {
            if (mq_check->Push(value) != ErrorCode::kSuccess)
            {
                is_bug_on = true;
                abort();
            }
        }
    }
}

void CheckSPMC(std::vector<SPSCUnboundedQueue<uint64_t>*> mq_check_vec, uint64_t total_msgs)
{
    uint64_t counter = 0;
    uint64_t value;
    uint64_t min_val;
    uint64_t min_idx;
    uint64_t* val_ptr;

    const size_t mq_size = mq_check_vec.size();
    uint64_t consumer_cnt[10] = {0};

    while (counter != total_msgs)
    {
        min_val = UINT64_MAX;
        min_idx = UINT64_MAX;
        for (size_t i = 0; i < mq_size; i++)
        {
            val_ptr = mq_check_vec[i]->Head();
            if (ADK_UNLIKELY(val_ptr == NULL))
            {
                continue;
            }
            else
            {
                if (*val_ptr < min_val)
                {
                    min_val = *val_ptr;
                    min_idx = i;
                }
            }
        }
        
        if (min_val == ++counter)
        {
            //校验成功
            if (mq_check_vec[min_idx]->Pop(value) != ErrorCode::kSuccess)
            {
                is_bug_on = true;
                abort();
            }
            ++(consumer_cnt[min_idx]);
        }
        else
        {
            is_bug_on = true;
            abort();
        }
    }
}

template <typename QueueType, bool kDoDelay>
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
            uint64_t& sqn = producer_sqns[data.tid];
            if (data.counter != ++(sqn))
            {
                is_bug_on = true;
                abort();
            }
            ++counter;
        }
    }
}

template <typename QueueType, bool kDoDelay>
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
        data.tid     = tid;
        while (mq->Push(data) != ErrorCode::kSuccess)
        {
        }
    }
}

BOOST_AUTO_TEST_CASE(msg_queue_spsc)
{
    std::cout << "start SPSCMsgQueue test, element counter: " << kTestCounter << std::endl;

    is_bug_on = false;
    boost::thread p[10];
    boost::thread c[10];
    for (uint32_t index = 0; index < 10; ++index)
    {
        SPSCQueue<uint64_t>* mq_p = SPSCQueue<uint64_t>::Create("test_spsc");

        p[index] = boost::thread(QProducer<SPSCQueue<uint64_t>, false>, mq_p, kTestCounter);
        c[index] = boost::thread(QConcumser<SPSCQueue<uint64_t>, false>, mq_p, kTestCounter);
    }
    for (uint32_t index = 0; index < 10; ++index)
    {
        p[index].join();
        c[index].join();
    }
    BOOST_REQUIRE(is_bug_on == false);
}

BOOST_AUTO_TEST_CASE(msg_queue_spmc)
{
    std::cout << "start SPMCMsgQueue test, element counter: " << kTestCounter << std::endl;

    is_bug_on = false;
    boost::thread p[10];
    boost::thread c1[10];
    boost::thread c2[10];
    boost::thread c3[10];


    std::vector<std::vector<SPSCUnboundedQueue<uint64_t>*>> check_spsc_vec(10);
    for (uint32_t index = 0; index < 10; ++index)
    {
        is_producer_on[index]                                   = true;
        SPMCQueue* mq_p                                         = SPMCQueue::Create("test_spmc", sizeof(uint64_t), 8192);
        SPSCUnboundedQueue<uint64_t>* mq_check_1                = SPSCUnboundedQueue<uint64_t>::Create("test_spsc");
        SPSCUnboundedQueue<uint64_t>* mq_check_2                = SPSCUnboundedQueue<uint64_t>::Create("test_spsc");
        SPSCUnboundedQueue<uint64_t>* mq_check_3                = SPSCUnboundedQueue<uint64_t>::Create("test_spsc");
        std::vector<SPSCUnboundedQueue<uint64_t>*> mq_check_vec = {mq_check_1, mq_check_2, mq_check_3};
        check_spsc_vec[index]                                   = mq_check_vec;

        p[index]  = boost::thread(SpmcQProducer<SPMCQueue, false>, mq_p, kTestCounter, index);
        c1[index] = boost::thread(SpmcQConcumser<SPMCQueue, false>, mq_p, mq_check_1, index);
        c2[index] = boost::thread(SpmcQConcumser<SPMCQueue, false>, mq_p, mq_check_2, index);
        c3[index] = boost::thread(SpmcQConcumser<SPMCQueue, false>, mq_p, mq_check_3, index);
    }
    for (uint32_t index = 0; index < 10; ++index)
    {
        p[index].join();
        c1[index].join();
        c2[index].join();
        c3[index].join();
        CheckSPMC(check_spsc_vec[index], kTestCounter);
    }
    BOOST_REQUIRE(is_bug_on == false);
}

BOOST_AUTO_TEST_CASE(msg_queue_mpsc)
{
    std::cout << "start MPSCUnboundedQueue test, element counter: " << kTestCounter << std::endl;

    is_bug_on = false;
    boost::thread p[10];
    boost::thread p2[10];
    boost::thread c[10];
    for (uint32_t index = 0; index < 10; ++index)
    {
        MPSCQueue* mq_p = MPSCQueue::Create<QDate>("test_mpsc", 1024);
        p[index]        = boost::thread(MpscQProducer<MPSCQueue, false>, mq_p, 1, kTestCounter / 2);
        p2[index]       = boost::thread(MpscQProducer<MPSCQueue, false>, mq_p, 2, kTestCounter / 2);
        c[index]        = boost::thread(MpscQConcumser<MPSCQueue, false>, mq_p, kTestCounter);
    }
    for (uint32_t index = 0; index < 10; ++index)
    {
        p[index].join();
        p2[index].join();
        c[index].join();
    }
    BOOST_REQUIRE(is_bug_on == false);
}