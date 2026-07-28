#ifndef ADK_IMPL_SCOPED_LOCK_H_
#define ADK_IMPL_SCOPED_LOCK_H_

#include "arch/synchronize.h"

namespace adk_impl
{

class ScopedLock
{
public:
    ScopedLock(Mutex& lock)
    {
        lock_ = &lock;
        MutexLock(lock, -1);
    }

    ScopedLock()
        :   lock_(NULL)
    {}

    ~ScopedLock()
    {
        if (lock_ != NULL)
            MutexUnlock(*lock_);
    }

    void AquireLock(Mutex& lock)
    {
        lock_ = &lock;
        MutexLock(lock, -1);
    }

    void ReleaseLock()
    {
        MutexUnlock(*lock_);
        lock_ = NULL;
    }
private:
    Mutex*  lock_;
};

class ScopedSpinlock
{
public:
    ScopedSpinlock(Spinlock& lock)
        :   lock_(lock)
    {
        SpinlockLock(lock_);
    }

    ~ScopedSpinlock()
    {
        SpinlockUnlock(lock_);
    }
    
private:
    Spinlock&  lock_;
};

} // adk
#endif // ADK_SCOPED_LOCK_H_
