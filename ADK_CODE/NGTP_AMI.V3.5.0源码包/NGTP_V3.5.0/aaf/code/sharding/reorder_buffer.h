#pragma once

#include <array>
#include <stdint.h>
#include <set>

namespace sharding
{

#define REORDER_BUFFER_SIZE 8192

struct ReorderBuffer
{
    ReorderBuffer()
    {
        array_.fill(0);
    }

    template <bool handle_overflow = true>
    bool PutSqn(uint64_t sqn)
    {
        if (sqn < head_)
        {
            // sqn 被丢弃
            return true;
        }

        if (head_ + REORDER_BUFFER_SIZE > sqn)
        {
            //  head_ =< sqn < head_ + REORDER_BUFFER_SIZE
            array_[sqn % REORDER_BUFFER_SIZE] = sqn;
            return true;
        }
        else
        {
            // 溢出
            if (handle_overflow) // 内部调用时为 false
            {
                overflow_map_.insert(sqn);
            }
            return false;
        }
    }

    void PushHead()
    {
        while (true)
        {
            if (head_ == array_[head_ % REORDER_BUFFER_SIZE])
            {
                ++head_;
                continue;
            }
            break;
        }   
    }

    uint64_t GetNextSqn()
    {
        PushHead();
        if (overflow_map_.empty())
        {
            // 快速路径，从这里返回
            return head_ - 1; // head_ 为下一个期望连续的sqn，因此 head_ - 1 为连续的最大sqn
        }
        
        auto it = overflow_map_.begin();
        for (; it != overflow_map_.end();)
        {
            if (!PutSqn<false>(*it)) // 内部调用不处理溢出
            {
                break;
            }
            else
            {
                it = overflow_map_.erase(it);
            }
        }

        // 上面 overflow 可能有新的sqn进入array，重新尝试 push head
        PushHead();
        return head_ - 1;
    }

    // 期望的下一个连续sqn的值
    uint64_t head_ = 0;
    std::array<uint64_t, REORDER_BUFFER_SIZE> array_;
    std::set<uint64_t> overflow_map_;
};

}
