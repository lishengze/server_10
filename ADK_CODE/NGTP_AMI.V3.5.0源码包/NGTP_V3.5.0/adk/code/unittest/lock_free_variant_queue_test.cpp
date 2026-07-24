#define BOOST_TEST_MODULE lock_free_variant_queue
#include <boost/test/included/unit_test.hpp>
#include <boost/thread.hpp>

#include <adk/error_code.h>
#include <adk/lock_free_queue_variant.h>
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
bool is_producer_mp_on[3][10] = {true};
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
    mq->set_release_alert();
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

        if (mq->TryPop(value) == ErrorCode::kSuccess)
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

void CheckMPMC(std::vector<SPSCUnboundedQueue<QDate>*> mq_check_vec, uint64_t total_msgs)
{
    uint64_t counter[3] = {0, 0, 0};
    QDate value;
    uint64_t min_val[3] = {UINT64_MAX, UINT64_MAX ,UINT64_MAX};
    uint64_t min_idx[3] = {UINT64_MAX, UINT64_MAX ,UINT64_MAX};
    QDate* val_ptr;

    const size_t mq_size = mq_check_vec.size();
    uint64_t consumer_mp_cnt[3][10] = {0};

    while (!(counter[0] == total_msgs && counter[1] == total_msgs))
    {
        min_val[0] = UINT64_MAX;
        min_val[1] = UINT64_MAX;
        min_idx[0] = UINT64_MAX;
        min_idx[1] = UINT64_MAX;
        for (size_t i = 0; i < mq_size; i++)
        {
            val_ptr = mq_check_vec[i]->Head();
            if (ADK_UNLIKELY(val_ptr == NULL))
            {
                continue;
            }
            else
            {
                if (val_ptr->counter < min_val[val_ptr->tid])
                {
                    min_val[val_ptr->tid] = val_ptr->counter;
                    min_idx[val_ptr->tid] = i;
                }
            }
        }
        
        if (min_val[0] == counter[0] + 1)
        {
            //校验成功
            if (mq_check_vec[min_idx[0]]->Pop(value) != ErrorCode::kSuccess)
            {
                is_bug_on = true;
                abort();
            }
            ++counter[0];
            ++(consumer_mp_cnt[0][min_idx[0]]);
        }
        else if (min_val[1] == counter[1] + 1)
        {
            if (mq_check_vec[min_idx[1]]->Pop(value) != ErrorCode::kSuccess)
            {
                is_bug_on = true;
                abort();
            }
            ++counter[1];
            ++(consumer_mp_cnt[1][min_idx[1]]);
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


template <typename QueueType, bool kDoDelay>
void MpmcQProducer(QueueType* mq, uint64_t total_msgs, uint64_t idx, uint64_t tid)
{
    QDate qdate = {0};
    qdate.tid = tid;
    while (qdate.counter != total_msgs)
    {
        if (kDoDelay)
        {
            usleep(0);
        }

        ++qdate.counter;

        while (mq->Push(qdate) != ErrorCode::kSuccess)
        {
        }
    }
    sleep(2);
    is_producer_mp_on[tid][idx] = false;
}

template <typename QueueType, bool kDoDelay>
void MpmcQConcumser(QueueType* mq, SPSCUnboundedQueue<QDate>* mq_check, uint64_t idx)
{
    QDate qdate;
    bool& is_producer_0_work = is_producer_mp_on[0][idx];
    bool& is_producer_1_work = is_producer_mp_on[1][idx];
    while (is_producer_0_work||is_producer_1_work)
    {
        if (kDoDelay)
        {
            usleep(0);
        }

        if (mq->TryPop(qdate) == ErrorCode::kSuccess)
        {
            if (mq_check->Push(qdate) != ErrorCode::kSuccess)
            {
                is_bug_on = true;
                abort();
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(variant_queue_spsc)
{
    std::cout << "start SPSCMsgQueue test, element counter: " << kTestCounter << std::endl;

    is_bug_on = false;
    boost::thread p[10];
    boost::thread c[10];
    for (uint32_t index = 0; index < 10; ++index)
    {
        variant::SPSCQueue<uint64_t>* mq_p = variant::SPSCQueue<uint64_t>::Create("test_spsc", 8192);

        p[index] = boost::thread(QProducer<variant::SPSCQueue<uint64_t>, false>, mq_p, kTestCounter);
        c[index] = boost::thread(QConcumser<variant::SPSCQueue<uint64_t>, false>, mq_p, kTestCounter);
    }
    for (uint32_t index = 0; index < 10; ++index)
    {
        p[index].join();
        c[index].join();
    }
    BOOST_REQUIRE(is_bug_on == false);
}

BOOST_AUTO_TEST_CASE(variant_queue_spmc)
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
        variant::SPMCQueue<uint64_t>* mq_p                      = variant::SPMCQueue<uint64_t>::Create("test_spmc", 8192);
        SPSCUnboundedQueue<uint64_t>* mq_check_1                = SPSCUnboundedQueue<uint64_t>::Create("test_spsc");
        SPSCUnboundedQueue<uint64_t>* mq_check_2                = SPSCUnboundedQueue<uint64_t>::Create("test_spsc");
        SPSCUnboundedQueue<uint64_t>* mq_check_3                = SPSCUnboundedQueue<uint64_t>::Create("test_spsc");
        std::vector<SPSCUnboundedQueue<uint64_t>*> mq_check_vec = {mq_check_1, mq_check_2, mq_check_3};
        check_spsc_vec[index]                                   = mq_check_vec;

        p[index]  = boost::thread(SpmcQProducer<variant::SPMCQueue<uint64_t>, false>, mq_p, kTestCounter, index);
        c1[index] = boost::thread(SpmcQConcumser<variant::SPMCQueue<uint64_t>, false>, mq_p, mq_check_1, index);
        c2[index] = boost::thread(SpmcQConcumser<variant::SPMCQueue<uint64_t>, false>, mq_p, mq_check_2, index);
        c3[index] = boost::thread(SpmcQConcumser<variant::SPMCQueue<uint64_t>, false>, mq_p, mq_check_3, index);
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

BOOST_AUTO_TEST_CASE(variant_queue_mpsc)
{
    std::cout << "start MPSCUnboundedQueue test, element counter: " << kTestCounter << std::endl;

    is_bug_on = false;
    boost::thread p[10];
    boost::thread p2[10];
    boost::thread c[10];
    for (uint32_t index = 0; index < 10; ++index)
    {
        variant::MPSCQueue<QDate>* mq_p = variant::MPSCQueue<QDate>::Create("test_mpsc", 8192);
        p[index]        = boost::thread(MpscQProducer<variant::MPSCQueue<QDate>, false>, mq_p, 1, kTestCounter / 2);
        p2[index]       = boost::thread(MpscQProducer<variant::MPSCQueue<QDate>, false>, mq_p, 2, kTestCounter / 2);
        c[index]        = boost::thread(MpscQConcumser<variant::MPSCQueue<QDate>, false>, mq_p, kTestCounter);
    }
    for (uint32_t index = 0; index < 10; ++index)
    {
        p[index].join();
        p2[index].join();
        c[index].join();
    }
    BOOST_REQUIRE(is_bug_on == false);
}

BOOST_AUTO_TEST_CASE(variant_queue_mpmc)
{
    std::cout << "start MPMCMsgQueue test, element counter: " << kTestCounter << std::endl;

    boost::thread p1[10];
    boost::thread p2[10];
    boost::thread c1[10];
    boost::thread c2[10];
    boost::thread c3[10];


    std::vector<std::vector<SPSCUnboundedQueue<QDate>*>> check_spsc_vec(10);
    for (uint32_t index = 0; index < 10; ++index)
    {
        is_producer_mp_on[0][index]                                = true;
        is_producer_mp_on[1][index]                                = true;
        variant::MPMCQueue<QDate>* mq_p                      = variant::MPMCQueue<QDate>::Create("test_mpmc", 8192);
        SPSCUnboundedQueue<QDate>* mq_check_1                = SPSCUnboundedQueue<QDate>::Create("test_spsc");
        SPSCUnboundedQueue<QDate>* mq_check_2                = SPSCUnboundedQueue<QDate>::Create("test_spsc");
        SPSCUnboundedQueue<QDate>* mq_check_3                = SPSCUnboundedQueue<QDate>::Create("test_spsc");
        std::vector<SPSCUnboundedQueue<QDate>*> mq_check_vec = {mq_check_1, mq_check_2, mq_check_3};
        check_spsc_vec[index]                                   = mq_check_vec;

        p1[index]  = boost::thread(MpmcQProducer<variant::MPMCQueue<QDate>, false>, mq_p, kTestCounter / 2, index, 0);
        p2[index]  = boost::thread(MpmcQProducer<variant::MPMCQueue<QDate>, false>, mq_p, kTestCounter / 2, index, 1);
        c1[index] = boost::thread(MpmcQConcumser<variant::MPMCQueue<QDate>, false>, mq_p, mq_check_1, index);
        c2[index] = boost::thread(MpmcQConcumser<variant::MPMCQueue<QDate>, false>, mq_p, mq_check_2, index);
        c3[index] = boost::thread(MpmcQConcumser<variant::MPMCQueue<QDate>, false>, mq_p, mq_check_3, index);
    }
    for (uint32_t index = 0; index < 10; ++index)
    {
        p1[index].join();
        p2[index].join();
        c1[index].join();
        c2[index].join();
        c3[index].join();
        CheckMPMC(check_spsc_vec[index], kTestCounter / 2);
    }
    BOOST_REQUIRE(is_bug_on == false);
}
