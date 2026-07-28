#include <time.h>

#include <adk/arch/synchronize.h>

namespace adk_impl
{

#define NS_PER_SEC      (1000*1000*1000)

const int ADK_FUTEX_PRIVATE_FLAG = 128;
static int CheckFutexSupportPrivate()
{
    static int dummy = 0;
    if (!syscall(SYS_futex, &dummy, (FUTEX_WAKE | ADK_FUTEX_PRIVATE_FLAG), 1, NULL, NULL, 0))
    {
        return ADK_FUTEX_PRIVATE_FLAG;
    }
    return 0;
}
const int g_futex_support_private = CheckFutexSupportPrivate();

inline void CalcAbsTimePoint(struct timespec& ts, int64_t timewait_ns)
{
    clock_gettime(CLOCK_REALTIME, &ts);
    timewait_ns += ts.tv_nsec;
    ts.tv_sec += timewait_ns / NS_PER_SEC;
    ts.tv_nsec = timewait_ns % NS_PER_SEC;

}

int SemaphoreWaitUntil(Semaphore& sem, int64_t timewait_ns)
{
    while (1)
    {
        if (timewait_ns < 0)
        {
            if (sem_wait(&sem) == 0)
                return 0;
            goto check_errno;
        }
    
        struct timespec ts;
        CalcAbsTimePoint(ts, timewait_ns);                      // FIXME: using toc to support MONOTONIC CLOCK
        if (sem_timedwait(&sem, &ts) == 0)
            return 0;

        check_errno:
        if (errno != EINTR)
            return errno;
    }
}

int MutexLockUntil(Mutex& mutex, int64_t timewait_ns)
{
    if (timewait_ns < 0)
        return pthread_mutex_lock(&mutex);

    struct timespec ts;
    CalcAbsTimePoint(ts, timewait_ns);
    return pthread_mutex_timedlock(&mutex, &ts);
}
} // adk

