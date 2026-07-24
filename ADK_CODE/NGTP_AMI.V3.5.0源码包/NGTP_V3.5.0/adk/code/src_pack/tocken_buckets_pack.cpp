#include <adk/token_buckets.h>
#include <adk_pack/token_buckets.h>

namespace adk
{

int32_t TokenBucket::Acquire(uint32_t bytes)
{
    return ((adk_impl::TokenBucket*)(this))->Acquire(bytes);
}

int32_t TokenBucket::TryAcquire(uint32_t bytes)
{
    return ((adk_impl::TokenBucket*)(this))->TryAcquire(bytes);
}

void TokenBucket::Release()
{
    ((adk_impl::TokenBucket*)(this))->Release();
}

void TokenBucket::SetCapacity(uint32_t capacity)
{
    ((adk_impl::TokenBucket*)(this))->SetCapacity(capacity);
}

uint64_t TokenBucket::GetCurrentTime() const
{
    return ((adk_impl::TokenBucket*)(this))->GetCurrentTime();
}

TokenBucket* RateControl::GetPrivateInstance(double rate_limit_perus, bool high_performance)
{
    return (TokenBucket*)(adk_impl::RateControl::GetInstance<adk_impl::rate_unit::Microsecond>(rate_limit_perus, high_performance));
}

int32_t ConstRateCtrl::Acquire(uint32_t bytes)
{
    return ((adk_impl::ConstRateCtrl*)(this))->Acquire(bytes);
}

int32_t ConstRateCtrl::TryAcquire(uint32_t bytes)
{
    return ((adk_impl::ConstRateCtrl*)(this))->TryAcquire(bytes);
}

void ConstRateCtrl::Release()
{
    ((adk_impl::ConstRateCtrl*)(this))->Release();
}

uint64_t ConstRateCtrl::GetCurrentTime() const
{
    return ((adk_impl::ConstRateCtrl*)(this))->GetCurrentTime();
}

ConstRateCtrl* RateControl::GetPrivateInstance(uint32_t rate_limit, uint32_t timespan)
{
    return (ConstRateCtrl*)(adk_impl::RateControl::GetInstance<adk_impl::rate_unit::Microsecond>(rate_limit, timespan));
}

}