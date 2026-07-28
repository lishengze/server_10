#ifndef ADK_RW_LOCK_H_
#define ADK_RW_LOCK_H_
#include <stdint.h>

namespace adk
{
class RWLock
{
public:
    RWLock();

    ~RWLock();

    /**
     * @brief 尝试获取读锁(非阻塞)
     * 
     * @return 是否成功获取读锁
     */
    bool try_lock_r();

    /**
     * @brief 获取读锁(阻塞)
     * 
     * @return 是否成功获取读锁
     */
    bool lock_r();

    /**
     * @brief 释放读锁 
     */
    void unlock_r();

    /**
     * @brief 判断当前读写锁是否处于读锁状态 
     */
    bool is_rlocked() const;

    /**
     * @brief 将读锁升级为写锁
     * 
     * @return 是否升级写锁成功 
     */
    bool rlock_upgrade_w();

    /**
     * @brief 尝试获取写锁(非阻塞)
     * 
     * @return 是否成功获取写锁
     */
    bool try_lock_w();

    /**
     * @brief 获取写锁(阻塞)
     * 
     * @return 是否成功获取写锁
     */
    bool lock_w();

    /**
     * @brief 释放写锁 
     */
    void unlock_w();

    /**
     * @brief 判断当前读写锁是否处于写锁状态 
     */
    bool is_wlocked() const;

    /**
     * @brief 将写锁降级为读锁
     */
    void wlock_decay_r();

    void reset();

    void release();

    bool is_release() const;

private:
    void* impl_;
};

}

#endif
