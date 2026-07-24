/**
 * @file
 * @brief      encapsulate underlying synchronize primitives
 * @author     zhaonan, zhaonan@archforce.com.cn
 * @date       2016-12-10
 */

#ifndef ADK_IMPL_SYNCHRONIZE_H_
#define ADK_IMPL_SYNCHRONIZE_H_

#include "generic.h"

#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h> 
#ifdef __GNUC__
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#endif

#include <atomic>

namespace adk_impl
{
/**
 * @brief      定义同步原语的作用域
 */
enum class SyncDomain : int
{
    kInterThread = 0,       ///< 同步原语，仅在线程间使用
    kInterProcess = 1,      ///< 同步原语，可在进程间使用
};

#ifdef __GNUC__
typedef ::pthread_spinlock_t                  Spinlock;
typedef ::sem_t                               Semaphore;
typedef ::pthread_mutex_t                     Mutex;
#elif defined(_MSC_VER)
typedef ::HANDLE                              Semaphore;
#endif

#define ADK_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief      初始化信号量
 *
 * @param      sem     信号量
 * @param[in]  domain  信号量的作用域
 * @param[in]  value   信号量的初始值
 *
 * @return     成功时返回0
 */
inline int SemaphoreInit(Semaphore& sem, SyncDomain domain, int value)
{
#ifdef __GNUC__
    if (sem_init(&sem, static_cast<int>(domain), value) == 0)
        return 0;
    return errno;
#elif defined(_MSC_VER)
    if ((sem = CreateSemaphore(NULL, value, value + 1, NULL)) != 0)
        return 0;
    return -1;
#endif
}

/**
 * @brief      销毁信号量
 *
 * @param      sem   信号量
 *
 * @return     成功时返回0
 */
inline int SemaphoreDestroy(Semaphore& sem)
{
#ifdef __GNUC__
    if (sem_destroy(&sem) == 0)
        return 0;
    return errno;
#elif defined(_MSC_VER)
    if (CloseHandle(sem) != 0)
        return 0;
    return -1;
#endif
}

/**
 * @brief      等待信号并指定超时时间，在超时之前若没有信号发生，则返回错误
 *
 * @param      sem          信号量
 * @param[in]  timewait_ns  超时时间
 *
 * @return     成功时返回0，超时时返回ETIMEDOUT
 */
int SemaphoreWaitUntil(Semaphore& sem, int64_t timewait_ns);
inline int SemaphoreWait(Semaphore& sem, int64_t timewait_ns)
{
#ifdef __GNUC__
    if (sem_trywait(&sem) == 0)
        return 0;

    if (timewait_ns != 0)
    {
        return SemaphoreWaitUntil(sem, timewait_ns);
    }
    return errno;
#elif defined(_MSC_VER)
    if (WAIT_OBJECT_0 == WaitForSingleObject(sem, static_cast<DWORD>(timewait_ns / 1000000)))
    {
        return 0;
    }
    return -1;
#endif
}

/**
 * @brief      释放信号
 *
 * @param      sem   信号量
 *
 * @return     成功时返回0
 */
inline int SemaphorePost(Semaphore& sem)
{
#ifdef __GNUC__
    if (sem_post(&sem) == 0)
        return 0;
    return errno;
#elif defined(_MSC_VER)
    if (ReleaseSemaphore(sem, 1, NULL) != 0)
        return 0;
    return -1;
#endif
}

#ifdef __GNUC__

/**
 * @brief      初始化自旋锁
 *
 * @param      lock  自旋锁
 *
 * @return     成功时返回0，失败时返回相应的错误码
 */
inline int SpinlockInit(Spinlock& lock)
{
    return pthread_spin_init(&lock, PTHREAD_PROCESS_SHARED);
}

/**
 * @brief      尝试对自旋锁加锁一次
 *
 * @param      lock  自旋锁
 *
 * @return     加锁成功返回0
 */
inline int SpinlockTryLock(Spinlock& lock)
{
    return pthread_spin_trylock(&lock);
}

/**
 * @brief      对自旋锁锁加，直到加锁成功
 *
 * @param      lock  自旋锁
 *
 * @return     加锁成功返回0
 */
inline int SpinlockLock(Spinlock& lock)
{
    return pthread_spin_lock(&lock);
}

/**
 * @brief      对自旋锁解锁，只有对该自旋锁加锁成功的对象才能对其解锁
 *
 * @param      lock  自旋锁
 *
 * @return     加锁成功返回0
 */
inline int SpinlockUnlock(Spinlock& lock)
{
    return pthread_spin_unlock(&lock);
}

/**
 * @brief      打开一个命名信号量，分别打开具有同样命名的信号量，该接口会返回同一信号量对象
 *
 * @param[in]  name   信号量名
 * @param[in]  value  信号量的初始值
 * @param[out] sem    信号量对象
 *
 * @return     成功时返回0
 */
inline int SemaphoreOpen(const char* name, int value, Semaphore** sem)
{
    if ((*sem = sem_open(name, O_CREAT|O_RDWR, 0660, value)) != SEM_FAILED)
        return 0;
    return errno;
}

/**
 * @brief      关闭命名信号量
 *
 * @param      sem   信号量对象
 *
 * @return     成功时返回0
 */
inline int SemaphoreClose(Semaphore* sem)
{
    if (sem_close(sem) == 0)
        return 0;
    return errno;
}

/**
 * @brief      销毁命名信号量
 *
 * @param[in]  name  信号量名
 *
 * @return     成功时返回0
 */
inline int SemaphoreUnlink(const char* name)
{
    if (sem_unlink(name) == 0)
        return 0;
    return errno;
}

/**
 * @brief      获取信号量的当前值，即还未消费的累计信号发生次数
 *
 * @param[in]      sem    信号量
 * @param[out]     value  信号量值
 *
 * @return     成功时返回0
 */
inline int SemaphoreValue(Semaphore& sem, int* value)
{
    if (sem_getvalue(&sem, value) == 0)
    {
        return 0;
    }
    return (errno == EAGAIN) ? ETIMEDOUT : errno;
}

/**
 * @brief      初始化互斥量
 *
 * @param      mutex   互斥量
 * @param[in]  domian  互斥量的作用域
 *
 * @return     成功时返回0
 */
inline int MutexInit(Mutex& mutex, SyncDomain domian)
{
    int ec;
    pthread_mutexattr_t attr;
    if ((ec = pthread_mutexattr_init(&attr)) != 0)
        return ec;

    if (domian == SyncDomain::kInterProcess)
    {
        if ((ec = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED)) != 0)
            return ec;
    }
    return pthread_mutex_init(&mutex, &attr);
}

/**
 * @brief      销毁互斥量
 *
 * @param      mutex  互斥量
 *
 * @return     成功时返回0
 */
inline int MutexDestroy(Mutex& mutex)
{
    return pthread_mutex_destroy(&mutex);
}

/**
 * @brief      对互斥量加锁并指定超时时间，在超时之前若未加锁成功则返回错误
 *
 * @param      mutex        互斥量
 * @param[in]  timewait_ns  超时时间
 *
 * @return     成功则返回0，超时则返回ETIMEDOUT
 */
int MutexLockUntil(Mutex& mutex, int64_t timewait_ns);
inline int MutexLock(Mutex& mutex, int64_t timewait_ns)
{
    int ec;
    if ((ec = pthread_mutex_trylock(&mutex)) == 0)
        return 0;

    if (timewait_ns != 0 && ec == EBUSY)
    {
        return MutexLockUntil(mutex, timewait_ns);
    }
    return (ec == EBUSY) ? ETIMEDOUT : ec;
}

/**
 * @brief      对互斥量解锁，只有对该互斥量加锁成功的对象才能够解锁
 *
 * @param      mutex  互斥量
 *
 * @return     成功则返回0
 */
inline int  MutexUnlock(Mutex& mutex)
{
    return pthread_mutex_unlock(&mutex);
}

extern const int g_futex_support_private;

inline int FutexWaitPrivate(int* addr, int expect, const timespec* timeout = NULL)
{
    return syscall(SYS_futex, addr, FUTEX_WAIT | g_futex_support_private, expect, timeout, NULL, 0);
}

inline int FutexWait(int* addr, int expect, const timespec* timeout = NULL)
{
    return syscall(SYS_futex, addr, FUTEX_WAIT, expect, timeout, NULL, 0);
}

inline int FutexTimedWaitPrivate(int* addr, int expect, uint64_t nanoseconds)
{
    #define ADK_FUTEX_NANO_PER_SEC 1000000000UL
    timespec timeout;
    if (ADK_LIKELY(nanoseconds <= 4000000000UL))
    {
        timeout.tv_sec = 0;
        while (nanoseconds >= ADK_FUTEX_NANO_PER_SEC)
        {
            /* The following asm() prevents the compiler from
               optimising this loop into a modulo operation */
            asm volatile ("" : "+rm" (nanoseconds));

            nanoseconds -= ADK_FUTEX_NANO_PER_SEC;
            ++timeout.tv_sec;
        }
        timeout.tv_nsec = nanoseconds;
    }
    else
    {
        timeout.tv_sec = nanoseconds / ADK_FUTEX_NANO_PER_SEC;
        timeout.tv_nsec = nanoseconds - (timeout.tv_sec * ADK_FUTEX_NANO_PER_SEC);
    }
    return FutexWaitPrivate(addr, expect, &timeout);
    #undef ADK_FUTEX_NANO_PER_SEC
}

inline int FutexWakePrivate(int* addr, int nr_wake = 1)
{
    return syscall(SYS_futex, addr, FUTEX_WAKE | g_futex_support_private, nr_wake, NULL, NULL, 0);
}

inline int FutexWake(int* addr, int nr_wake = 1)
{
    return syscall(SYS_futex, addr, FUTEX_WAKE, nr_wake, NULL, NULL, 0);
}

#define ADK_LW_SPIN_IS_LOCK     1
#define ADK_LW_SPIN_IS_UNLOCK   0


class SpinLockPolicyPause
{
public:
    void backoff(uint32_t counter)
    {
        if (counter >= 1024)
        {
            usleep(0);
        }
        else
        {
            ADK_PAUSE();
        }
    }
};

template<typename Policy>
class LightWeightSpinLockImpl
{
public:
    LightWeightSpinLockImpl()
        : l_(ATOMIC_FLAG_INIT)
    {
        static_assert(sizeof(l_) == sizeof(bool), 
                      "atomic_flag on platform is not support");
    }

    inline void lock()
    {
        if (ADK_LIKELY(try_lock()))
            return;

        uint32_t counter = 0;
        Policy policy;
        do 
        {   
            ++counter;
            policy.backoff(counter);
            
            if (*(volatile bool*)&l_ != false)
                continue;

            if (ADK_LIKELY(try_lock()))
                return;
        } while (true);
    }

    inline bool try_lock()
    {
        return !l_.test_and_set(std::memory_order_acquire);
    }

    inline void unlock()
    {
        l_.clear(std::memory_order_release);
    }

    std::atomic_flag l_;
};

typedef adk_impl::LightWeightSpinLockImpl<SpinLockPolicyPause> LightWeightSpinLock;

#endif

} // adk


#endif // ADK_SYNCHRONIZE_H_
