#ifndef ADK_IMPL_SIGNAL_H_
#define ADK_IMPL_SIGNAL_H_

#include <string>

namespace adk_impl
{

enum SignalType
{
    kUnsafePolling = 0,
    kConditionVar,
    kFutex
};

class ISignal
{
public:
    static ISignal* CreateInstance(SignalType signal_type);

    virtual void Signal() = 0;

    virtual int32_t Wait() = 0;

    void ReleaseThread();
};

}

#endif
