#pragma once

#include <string>
#include <atomic>

#include <adk/shm_ptr.h>
#include <adk/constant.h>
#include <adk/arch/generic.h>
#include <adk/lock_free_queue_variant.h>

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/unordered_map.hpp>     //boost::unordered_map

#include <functional>                  //std::equal_to
#include <boost/functional/hash.hpp>   //boost::hash

namespace sharding
{

class ShmSeqLock
{
private:
    ShmSeqLock() = default;

    constexpr static size_t header_size()
    {
        return ADK_ROUND_UP(sizeof(ShmSeqLock), ADK_CACHE_LINE_SIZE);
    }

    static size_t queue_size_align(uint32_t cache_size)
    {
        return ADK_ROUND_TO_POWER_OF_2(cache_size);
    }

public:

    static size_t CalcMemSize(uint32_t cache_size)
    {
        assert(cache_size >= 2);
        size_t total_size = header_size() + sizeof(int64_t) * queue_size_align(cache_size);
        return total_size;
    }

    // 创建接口可以由框架提供，便于自动维护 
    static ShmSeqLock* Create(const std::string& name, uint32_t cache_size, void* addr)
    {
        ShmSeqLock* seq_lock = new (addr) ShmSeqLock();
        adk::NameCopy(seq_lock->lock_name, name);

        seq_lock->queue_size_ = queue_size_align(cache_size);
        seq_lock->queue_mask_ = seq_lock->queue_size_ - 1;
        seq_lock->entry_bits_ = 3;  // sizeof(int64_t) == 2^3
        seq_lock->entries_    = (int64_t*)(adk::ptr_add(addr, header_size()));

        for (uint32_t i = 0; i < seq_lock->queue_size_; ++i)
        {
            int64_t* entry = &(seq_lock->entries_[i]);
            *entry         = 0;
        }

        return seq_lock;
    }

    /**
     * @brief 加锁接口
     * 
     * @param total_seq ami 全局序
     * @param index 分片索引，用于记录当前持有锁的分片信息
     */
    void Lock(int64_t total_seq, int32_t index)
    {
        int64_t expect_value = static_cast<int64_t>(total_seq) - 1;
        int64_t lock_value = -(total_seq);
        while (true)
        {
            int64_t curr_lock = expect_value;
            if (lock_sqn_.compare_exchange_strong(curr_lock, lock_value))
            {
                break;
            }
            // else: curr_lock will be lock_sqn_ current value

            if (curr_lock < 0)
            {
                // 被其他线程锁住状态
                for (int i = 0; i < 16; ++i)
                {
                    ADK_PAUSE();
                }
                continue;
            }
            else
            {
                if (total_seq <= curr_lock)
                {
                    // 同一个消息序号多次加锁
                    // 需要加锁的广播消息，agent会阻塞递交
                    expect_value = curr_lock;
                    continue;
                }
                // 可以使用 curr_lock 值加锁，每次重试更新 curr_lock,
                // 将 total_seq <= curr_lock 合并处理，但是处理结果的语义可能会有的题

                TryRelease(curr_lock);
            }
        }

        curr_lock_index_ = index;
        prev_lock_sqn_ = expect_value;
    }


    /**
     * @brief 释放锁
     * 
     * @param total_seq ami 全局序
     * 
     * @note 可以单独调用 UnLock，表示该 total_seq 的消息无须加锁
     *        无须加锁的消息序号可以不用等待前序消息解锁完成。
     */
    void UnLock(int64_t total_seq, int32_t index)
    {
        int64_t lock_value = -(total_seq);
        total_seq = std::max(total_seq, prev_lock_sqn_);
        if (!lock_sqn_.compare_exchange_strong(lock_value, total_seq))
        {
            // 当前线程并没有持有锁
            UnlockCache(total_seq);
            if (lock_value >= 0 && lock_value < total_seq)
            {
                TryRelease(lock_value);
            }
        }
        else
        {
            // 当前线程释放锁
            TryRelease(total_seq);
        }
    }

    /**
     * @brief 获取当前锁的状态信息
     * 
     * @param is_locked 
     * @param index 
     */
    void GetLockStatus(bool& is_locked, int32_t& index)
    {
    }

// private:
    char lock_name[ADK_MAX_NAME_LEN];

    std::atomic<int64_t> lock_sqn_{0};
    int64_t prev_lock_sqn_ = 0;
    int32_t curr_lock_index_ = 0;

    uint32_t    queue_mask_;
    uint32_t    queue_size_;
    //设定entry_size为2的N次方
    uint32_t            entry_bits_;

    int64_t* entries_;  // 0: empty stat; N: unlock N


    inline uint64_t GetInQueueIndex(int64_t index)
    {
        return index & queue_mask_;
    }

    inline int64_t* GetIndexEntry(int64_t index)
    {
        return adk::ptr_add(entries_, GetInQueueIndex(index) << entry_bits_);
    }

    void UnlockCache(int64_t total_seq)
    {
        // 2^3 = 8 sizeof(int64_t)
        int64_t* entry_ptr = GetIndexEntry(total_seq);

        while (!__sync_bool_compare_and_swap(entry_ptr, 0, total_seq))
        {
            int64_t cur_val = *entry_ptr;
            if (cur_val > 0)
            {
                // cur_val == total_seq : duplicate Unlock
                // cur_val > total_seq : Unlock(n); Unlock(n-1)
                // cur_val < total_seq : Unlock(n); Unlock(n+cache_size)
                if (cur_val < total_seq)
                {
                    if (!__sync_bool_compare_and_swap(entry_ptr, cur_val, total_seq))
                    {
                        // another thread lock or unlock
                        continue;
                    }
                }

                return;
            }
            assert(cur_val <= 0);   // locked or init val
            ADK_PAUSE();
        }
    }

    // check total_seq is unlocked on cache
    void TryRelease(int64_t curr_lock)
    {
        // int64_t curr_lock = lock_sqn_.load(std::memory_order_relaxed);
        assert(curr_lock >= 0);

        do
        {
            int64_t* entry_ptr = GetIndexEntry(curr_lock + 1);
            int64_t unlock_val = *entry_ptr;
            if (unlock_val == curr_lock + 1)
            {
                if (lock_sqn_.compare_exchange_strong(curr_lock, curr_lock + 1))
                {
                    ++curr_lock;
                    *entry_ptr = 0;
                    continue;
                }
            }
            break;
        } while (true);
    }

};

}   // end of namespace sharding