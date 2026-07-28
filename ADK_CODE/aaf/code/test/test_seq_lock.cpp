
#include <adk/util.h>
#include <adk/random.h>

#include <time.h>
#include <map>
#include <vector>
#include <string>
#include <cstdlib>
#include <thread>
#include <iostream>
#include <iomanip>

#include "../sharding/seq_lock.h"


using sharding::ShmSeqLock;

void WorkerThread(ShmSeqLock* seq_lock, int32_t index, uint64_t step)
{
    uint64_t total_seq = index;
    while (true)
    {
        seq_lock->Lock(total_seq, index);
        
        // for (int i = 0; i < 4; ++i)
        // {
        //     ADK_PAUSE();
        // }
        seq_lock->UnLock(total_seq, index);
        total_seq += step;
        ADK_BARRIER();

        // if (adk_impl::Random(1, 1000) == 1)
        // {
        //     usleep(0);
        // }
        // else
        {
            for (int i = 0; i < 16; ++i)
            {
                ADK_PAUSE();
            }
        }
        // seq_lock->UnLock(total_seq);
        // total_seq += step;
    }
}

void Test1(int thread_num)
{
    uint32_t total_size = sharding::ShmSeqLock::CalcMemSize(65536);
    char* addr = new char[total_size];
    auto* seq_lock = sharding::ShmSeqLock::Create("seq_lock", 65536, addr);
    assert(seq_lock);

    std::vector<std::thread> work_thread_vec;
    const int32_t kThreadNum = thread_num;
    std::cout << "<<<<<<<<<   thread num: " << kThreadNum << std::endl;

    for (int32_t i = 1; i <= kThreadNum; ++i)
    {
        work_thread_vec.emplace_back(std::thread(&WorkerThread, seq_lock, i, kThreadNum));
    }

    int64_t prev_sqn = 0;
    while(true)
    {
        sleep(1);
        int64_t curr_sqn = seq_lock->lock_sqn_.load();
        std::cout << "lock sqn=" << curr_sqn << "  rate: " << std::setprecision(4)
                  << (float)((std::abs(curr_sqn) - prev_sqn)) / 10000.0 << "W tps"
                  << std::endl;
        prev_sqn = std::abs(curr_sqn);
    }

}

void Test2(sharding::ShmSeqLock* seq_lock)
{
    seq_lock->Lock(1, 0);
    seq_lock->UnLock(1, 0);

    // seq_lock->Lock(2, 0);
    seq_lock->UnLock(2, 0);

    seq_lock->Lock(3, 0);
    seq_lock->UnLock(3, 0);
    seq_lock->UnLock(3, 1);

    seq_lock->Lock(4, 0);
    seq_lock->UnLock(4, 0);

    seq_lock->UnLock(6, 0);
    seq_lock->UnLock(6, 0);
    seq_lock->UnLock(6, 0);
    seq_lock->UnLock(6, 0);
    seq_lock->UnLock(7, 0);
    seq_lock->UnLock(8, 0);
    seq_lock->UnLock(8, 0);

    seq_lock->Lock(5, 0);
    seq_lock->UnLock(5, 0);

    // dup lock
    seq_lock->Lock(9, 1);
    seq_lock->UnLock(9, 1);
    seq_lock->Lock(9, 2);
    seq_lock->UnLock(9, 2);
    seq_lock->Lock(10, 0);
    seq_lock->UnLock(10, 0);
    seq_lock->Lock(9, 3);
    seq_lock->UnLock(9, 3);

    seq_lock->Lock(11, 0);
    seq_lock->UnLock(11, 0);
    seq_lock->Lock(12, 0);
    seq_lock->UnLock(12, 0);

    std::cout << "lock sqn=" << seq_lock->lock_sqn_.load() << std::endl;
}

struct ShareData
{
    int32_t sharding_index = 0;
    uint64_t counter       = 0;
};

ShareData g_share_data;

void WorkerThread2(ShmSeqLock* seq_lock, int32_t index, uint64_t step)
{
    uint64_t total_seq = 1;
    while (true)
    {
        uint64_t expect_cnt = index + (step * (total_seq - 1));

        seq_lock->Lock(total_seq, index);
        if (g_share_data.sharding_index != 0)
        {
            abort();
        }
        g_share_data.sharding_index = index;
        ++g_share_data.counter;
        // assert(g_share_data.counter == expect_cnt);

        if (adk_impl::Random(1, 100) == 1)
        {
            usleep(0);
        }
        else
        {
            for (int i = 0; i < 32; ++i)
            {
                ADK_PAUSE();
            }
        }

        if (g_share_data.sharding_index != index)
        {
            abort();
        }
        // assert(g_share_data.counter == expect_cnt);
        g_share_data.sharding_index = 0;
        seq_lock->UnLock(total_seq, index);
        ADK_BARRIER();

        if (adk_impl::Random(1, 20) == 1)
        {
            usleep(0);
        }
        else
        {
            for (int i = 0; i < 64; ++i)
            {
                ADK_PAUSE();
            }
        }
        seq_lock->UnLock(total_seq, index);
        ++total_seq;
    }
}

void TestBroadCast(int thread_num)
{
    uint32_t total_size = sharding::ShmSeqLock::CalcMemSize(65536);
    char* addr = new char[total_size];
    auto* seq_lock = sharding::ShmSeqLock::Create("seq_lock", 65536, addr);
    assert(seq_lock);

    std::vector<std::thread> work_thread_vec;
    const int32_t kThreadNum = thread_num;
    std::cout << "<<<<<<<<< TestBroadCast  thread num: " << kThreadNum << std::endl;

    for (int32_t i = 1; i <= kThreadNum; ++i)
    {
        work_thread_vec.emplace_back(std::thread(&WorkerThread2, seq_lock, i, kThreadNum));
    }

    int64_t prev_sqn = 0;
    while(true)
    {
        sleep(1);
        int64_t curr_sqn = g_share_data.counter;
        std::cout << "lock sqn=" << curr_sqn << "  rate: " << std::setprecision(4)
                  << (float)((std::abs(curr_sqn) - prev_sqn)) / 10000.0 << "W tps"
                  << std::endl;
        prev_sqn = std::abs(curr_sqn);
    }
}

int main(int argc, char const *argv[])
{
    int thread_num = 16;
    if (argc > 1)
    {
        thread_num = atoi(argv[1]);
        // TestBroadCast(thread_num);
        Test1(thread_num);
    }
    
    uint32_t total_size = sharding::ShmSeqLock::CalcMemSize(16);
    char* addr = new char[total_size];
    auto* seq_lock = sharding::ShmSeqLock::Create("seq_lock", 16, addr);
    assert(seq_lock);

    // Test2(seq_lock);

    seq_lock->UnLock(2, 1);
    for (int i = 1; i <= 32; ++i)
    {
        seq_lock->UnLock(i, 1);
    }
    std::cout << "lock sqn=" << seq_lock->lock_sqn_.load() << std::endl;

    seq_lock->Lock(33, 1);
    seq_lock->UnLock(33, 1);
    std::cout << "lock sqn=" << seq_lock->lock_sqn_.load() << std::endl;

    // abort();
    return 0;
}