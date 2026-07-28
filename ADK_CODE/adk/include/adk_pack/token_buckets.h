/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/
#ifndef ADK_TOKEN_BUCKET_H_
#define ADK_TOKEN_BUCKET_H_

#include "error_code.h"

#include <stdint.h>

namespace adk
{

namespace rate_unit
{
    struct Second {};
    struct Millisecond {};
    struct Microsecond {};
}

class TokenBucket
{
public:
    int32_t Acquire(uint32_t bytes);

    int32_t TryAcquire(uint32_t bytes);

    void Release();

    void SetCapacity(uint32_t capacity);

    uint64_t GetCurrentTime() const;
};

class ConstRateCtrl
{
public:
    int32_t Acquire(uint32_t bytes);

    int32_t TryAcquire(uint32_t bytes);

    void Release();

    uint64_t GetCurrentTime() const;
};

class RateControl
{
public:
    template<typename Precision = rate_unit::Microsecond>
    static TokenBucket* GetInstance(double rate_limit, bool high_performance = false);

    template <typename Precision = rate_unit::Second>
    static ConstRateCtrl *GetInstance(uint32_t rate_limit, uint32_t timespan);

private:
    static TokenBucket* GetPrivateInstance(double rate_limit_perus, bool high_performance);

    static ConstRateCtrl* GetPrivateInstance(uint32_t rate_limit, uint32_t timespan);
};

template<>
inline TokenBucket* RateControl::GetInstance<rate_unit::Microsecond>(double rate_limit, bool high_performance)
{
    return RateControl::GetPrivateInstance(rate_limit, high_performance);
}

template<>
inline TokenBucket* RateControl::GetInstance<rate_unit::Millisecond>(double rate_limit, bool high_performance)
{
    return RateControl::GetPrivateInstance(rate_limit / 1000, high_performance);
}

template<>
inline TokenBucket* RateControl::GetInstance<rate_unit::Second>(double rate_limit, bool high_performance)
{
    return RateControl::GetPrivateInstance(rate_limit / 1000000, high_performance);
}

template<>
inline ConstRateCtrl* RateControl::GetInstance<rate_unit::Microsecond>(uint32_t rate_limit, uint32_t timespan)
{
    return RateControl::GetPrivateInstance(rate_limit, timespan);
}


template<>
inline ConstRateCtrl* RateControl::GetInstance<rate_unit::Millisecond>(uint32_t rate_limit, uint32_t timespan)
{
    return RateControl::GetPrivateInstance(rate_limit, timespan * 1000);
}

template<>
inline ConstRateCtrl* RateControl::GetInstance<rate_unit::Second>(uint32_t rate_limit, uint32_t timespan)
{
    return RateControl::GetPrivateInstance(rate_limit, timespan * 1000000);
}

}
#endif