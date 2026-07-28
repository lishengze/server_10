#include <adk/signal.h>
#include <adk/error_code.h>
#include <adk/arch/synchronize.h>

#include <mutex>
#include <thread>
#include <chrono>
#include <condition_variable>

namespace adk_impl
{

class SignalBase : public ISignal
{
public:
    SignalBase()
    {
        release_alert_ = false;
    }

    inline void set_release_alert()
    {
        release_alert_ = true;
    }

protected:
    volatile bool release_alert_;
};

class SignalPoll final : public SignalBase
{
public:
    SignalPoll()
    {
        nr_signals_ = 0;
        nr_consumed_signals_ = 0;
    }

    void Signal() override
    {
        ++nr_signals_;
    }

    int32_t Wait() override
    {
        while (nr_signals_ <= nr_consumed_signals_)
        {
            if (release_alert_)
            {
                release_alert_ = false;
                return ErrorCode::kWouldblock;
            }

            std::this_thread::yield();
        }

        ++nr_consumed_signals_;
        return ErrorCode::kSuccess;
    }

private:
    volatile uint64_t nr_signals_;
    volatile uint64_t nr_consumed_signals_;
};

class SignalCV final : public SignalBase
{
public:
    SignalCV()
    {
        is_waiting_ = false;
        is_ready_ = false;
    }

    void Signal() override
    {
        std::unique_lock<std::mutex> _(mutex_);
        is_ready_ = true;
        if (!is_waiting_)
            return;
        signal_cv_.notify_all();
    }

    int32_t Wait() override
    {
        std::unique_lock<std::mutex> _(mutex_);
        is_waiting_ = true;

        while (!is_ready_)
        {
            signal_cv_.wait_for(_, std::chrono::milliseconds(1));
            if (ADK_UNLIKELY(release_alert_))
            {
                release_alert_ = false;
                is_waiting_ = false;
                return ErrorCode::kWouldblock;
            }
        }

        is_ready_ = false;
        is_waiting_ = false;
        return ErrorCode::kSuccess;
    }

private:
    std::mutex              mutex_;
    std::condition_variable signal_cv_;
    volatile bool           is_waiting_;
    volatile bool           is_ready_;
};

class SignalFutex final : public SignalBase
{
public:
    SignalFutex()
    {
        wait_futex_ = 0;
    }

    void Signal() override
    {
        while (ADK_UNLIKELY(1 != wait_futex_))
        {
            if (ADK_UNLIKELY(release_alert_))
            {
                return;
            }

            std::this_thread::yield();
        }

        FutexWakePrivate(&wait_futex_);
    }

    int32_t Wait() override
    {
        wait_futex_ = 1;
        while (0 != FutexTimedWaitPrivate(&wait_futex_, 1, 1000000))
        {
            if (release_alert_)
            {
                release_alert_ = false;
                return ErrorCode::kWouldblock;
            }
            ADK_PAUSE();
        }

        wait_futex_ = 0;
        return ErrorCode::kSuccess;
    }
private:
    int32_t wait_futex_;
};

ISignal* ISignal::CreateInstance(SignalType signal_type)
{
    switch (signal_type)
    {
    case SignalType::kUnsafePolling:
        return new SignalPoll;
    case SignalType::kConditionVar:
        return new SignalCV;
    case SignalType::kFutex:
        return new SignalFutex;
    default:
        ;
    }
    return nullptr;
}

void ISignal::ReleaseThread()
{
    ((SignalBase*)this)->set_release_alert();
}

}