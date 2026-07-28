#ifndef ADK_IMPL_SEQ_LOCK_H_
#define ADK_IMPL_SEQ_LOCK_H_

#include "arch/generic.h"

#include <boost/array.hpp>

namespace adk_impl
{

class SeqLock
{
public:
    SeqLock() : sequence_(0)
    {}

    inline int64_t ReadBegin()
    {
        int64_t start;
        while (ADK_UNLIKELY((start = ACCESS_ONCE(sequence_)) & 1)) 
        {
            CpuRelax();
        }
        return start;
    }

    inline int64_t TryReadBegin(uint32_t retry_time)
    {
        uint32_t retry_index = 0;
        int64_t start;
        while (ADK_UNLIKELY((start = ACCESS_ONCE(sequence_)) & 1)) 
        {
            CpuRelax();
            if (++retry_index >= retry_time)
            {
                return -1;
            }
        }
        return start;
    }

    template<bool kFence = false>
    inline bool IsLocked() const
    {
        if (kFence)
        {
            ADK_MB();
        }
        else
        {
            ADK_BARRIER();
        }
        return (ACCESS_ONCE(sequence_) & 1);
    }

    inline bool ReadResult(int64_t start)
    {
        ADK_BARRIER();
        return (start == sequence_);
    }

    template<bool kFence = false>
    inline void WriteBegin()
    {
        ++sequence_;
        if (kFence)
        {
            ADK_MB();
        }
        else
        {
            ADK_BARRIER();
        }
    }

    inline void WriteEnd()
    {
        ADK_BARRIER();
        ++sequence_;
    }

    inline void RollBack()
    {
        sequence_ = ((sequence_ >> 1) << 1);
    }

    void reset()
    {
        sequence_ = 0;
    }

    int64_t lock_hist_nr() const
    {
        return (sequence_ >> 1);
    }

protected:
    inline static void CpuRelax()
    {
        for (uint32_t index=0; index<64; ++index)
        {
            ADK_PAUSE();
        }
    }

    int64_t sequence_;
};

class MWSeqLock : public SeqLock
{
public:
    inline void WriteBegin()
    {
        int64_t start;
        do
        {
            start = ReadBegin();
        } while(!__sync_bool_compare_and_swap(&sequence_, start, start + 1));
    }
};

}
#endif