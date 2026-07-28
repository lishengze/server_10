#include <adk/rw_lock.h>
#include <adk_pack/rw_lock.h>

namespace adk
{
using RWLockImpl = adk_impl::RWLock<int32_t>;

RWLock::RWLock()
{
    impl_ = new RWLockImpl();
}

RWLock::~RWLock()
{
    if (impl_)
    {
        delete (RWLockImpl*)impl_;
    }
}

bool RWLock::try_lock_r()
{
    return ((RWLockImpl*)impl_)->try_lock_r();
}

bool RWLock::lock_r()
{
    return ((RWLockImpl*)impl_)->lock_r();
}

void RWLock::unlock_r()
{
    ((RWLockImpl*)impl_)->unlock_r();
}

void RWLock::wlock_decay_r()
{
    ((RWLockImpl*)impl_)->wlock_decay_r();
}

bool RWLock::is_rlocked() const
{
    return ((RWLockImpl*)impl_)->is_rlocked();
}

bool RWLock::try_lock_w()
{
    return ((RWLockImpl*)impl_)->try_lock_w();
}

bool RWLock::lock_w()
{
    return ((RWLockImpl*)impl_)->lock_w();
}

bool RWLock::is_wlocked() const
{
    return ((RWLockImpl*)impl_)->is_wlocked();
}

bool RWLock::rlock_upgrade_w()
{
    return ((RWLockImpl*)impl_)->rlock_upgrade_w();

}

void RWLock::unlock_w()
{
    ((RWLockImpl*)impl_)->unlock_w();
}

void RWLock::reset()
{
    ((RWLockImpl*)impl_)->reset();
}

void RWLock::release()
{
    ((RWLockImpl*)impl_)->release();
}

bool RWLock::is_release() const
{
    return ((RWLockImpl*)impl_)->is_release();
}


}
