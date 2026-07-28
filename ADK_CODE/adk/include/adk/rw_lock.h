#ifndef ADK_IMPL_RW_LOCK_H_
#define ADK_IMPL_RW_LOCK_H_

#include <adk/arch/generic.h>

namespace adk_impl
{

template<typename ImplType = int16_t>
class RWLock
{
public:
    RWLock()
    {
        release_ = false;
        lock_impl_ = 0;
    }

    static constexpr ImplType kWLocked = static_cast<ImplType>(-1);

    static_assert(kWLocked < 0, "ImplType must be signed integer type");

    inline bool try_lock_r()
    {
        const auto lock = ACCESS_ONCE(lock_impl_);
        if (lock >= 0)
        {
            return __sync_bool_compare_and_swap(&lock_impl_, lock, lock + 1);
        }

        return false;
    }

    inline bool lock_r()
    {
        while (!try_lock_r())
        {
            if (ADK_UNLIKELY(ACCESS_ONCE(release_)))
            {
                return false;
            }

            // 若是写锁状态则sleep一下再重试; 否则(读锁状态)立即重试. 
            if (is_wlocked())
            {
                usleep(0);
            }
        }

        return true;
    }

    inline void unlock_r()
    {
        ADK_UNUSED const auto lock_prev = __sync_fetch_and_sub(&lock_impl_, 1);
        assert(lock_prev > 0);
    }

    inline bool try_lock_w(ImplType exp_lock = 0)
    {
        return __sync_bool_compare_and_swap(&lock_impl_, exp_lock, -1);
    }

    inline bool lock_w()
    {
        while (!try_lock_w())
        {
            if (ADK_UNLIKELY(ACCESS_ONCE(release_)))
            {
                return false;
            }

            usleep(0);
        }

        return true;
    }

    inline bool is_rlocked() const
    {
        return ACCESS_ONCE(lock_impl_) > 0;
    }

    inline bool is_wlocked() const
    {
        return kWLocked == ACCESS_ONCE(lock_impl_);
    }

    inline bool rlock_upgrade_w()
    {
        // 先尝试获取写锁, 若失败了则先释放自己的读锁再获取写锁, 避免死锁;
        if (try_lock_w(1))
        {
            return true;
        }

        unlock_r();
        return lock_w();
    }

    /**
     * write lock directly change to read lock
     */
    inline void wlock_decay_r()
    {
        assert(kWLocked == ACCESS_ONCE(lock_impl_));
        lock_impl_ = 1;
    }

    inline void unlock_w()
    {
        ADK_BARRIER();
        assert(kWLocked == ACCESS_ONCE(lock_impl_));
        lock_impl_ = 0;
    }

    void reset()
    {
        release_ = false;
        lock_impl_ = 0;
    }

    void release()
    {
        release_ = true;
    }

    inline bool is_release() const
    {
        return release_;
    }

private:
    bool     release_;

    // 引用计数(-1: 写锁状态; 0: 无锁; >=1: 读锁的数量)
    ImplType lock_impl_;
};

}

#endif
