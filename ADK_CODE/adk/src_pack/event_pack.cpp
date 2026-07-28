#include <adk/event.h>
#include <adk_pack/event.h>

namespace adk
{

using BackoffImpl = adk_impl::Backoff;

Backoff::Backoff()
{
    backoff_impl_ = (void*)(new BackoffImpl);
}

Backoff::~Backoff()
{
    assert(backoff_impl_);
    delete reinterpret_cast<BackoffImpl*>(backoff_impl_);
}

void Backoff::Reset()
{
    reinterpret_cast<BackoffImpl*>(backoff_impl_)->Reset();
}

bool Backoff::IsEvent()
{
    return reinterpret_cast<BackoffImpl*>(backoff_impl_)->IsEvent();
}

void Backoff::Run()
{
    reinterpret_cast<BackoffImpl*>(backoff_impl_)->Run();
}

int32_t Backoff::Config(uint32_t type, void* val, uint32_t len)
{
    return reinterpret_cast<BackoffImpl*>(backoff_impl_)->Config(type, val, len);
}

namespace policy
{

using PauseImpl = adk_impl::policy::Pause;

void Pause::Init(Backoff& bf_base)
{
    PauseImpl::Init(*((BackoffImpl*)bf_base.backoff_impl_));
}

void Pause::Reset(uint32_t& backoff)
{
    PauseImpl::Reset(backoff);
}

void Pause::Run(BackoffInfo& bfi)
{
    PauseImpl::Run((PauseImpl::BackoffInfo&)bfi);
}

int32_t Pause::Config(BackoffInfo& bfi, uint32_t type, void* val, uint32_t len)
{
    return PauseImpl::Config((PauseImpl::BackoffInfo&)bfi, type, val, len);
}

using DelayImpl = adk_impl::policy::Delay;
void Delay::Init(Backoff& bf_base, uint32_t init)
{
    DelayImpl::Init(*((BackoffImpl*)bf_base.backoff_impl_), init);
}

void Delay::Reset(uint32_t& delay)
{
    DelayImpl::Reset(delay);
}

void Delay::Run(uint32_t& delay)
{
    DelayImpl::Run(delay);
}

int32_t Delay::Config(uint32_t& delay, uint32_t type, void* val, uint32_t len)
{
    return DelayImpl::Config(delay, type, val, len);
}

using EventImpl = adk_impl::policy::Event;

void Event::Init(Backoff& bf_base, uint32_t init)
{
    EventImpl::Init(*((BackoffImpl*)bf_base.backoff_impl_), init);
}

void Event::Reset(uint32_t& delay)
{
    EventImpl::Reset(delay);
}

void Event::Run(uint32_t& delay)
{
    EventImpl::Run(delay);
}

int32_t Event::Config(uint32_t& delay, uint32_t type, void* val, uint32_t len)
{
    return EventImpl::Config(delay, type, val, len);
}

}

using SimpleEventManagerImpl = adk_impl::SimpleEventManager;

SimpleEventManager::SimpleEventManager(uint64_t polling_nano, int32_t backoff_limit)
{
    simple_event_manager_impl_ = (void*)(new SimpleEventManagerImpl(polling_nano, backoff_limit));
}

SimpleEventManager::~SimpleEventManager()
{
    assert(simple_event_manager_impl_);
    delete reinterpret_cast<SimpleEventManagerImpl*>(simple_event_manager_impl_);
}

void SimpleEventManager::GetStats(SimpleEveManStats& stats)
{
    reinterpret_cast<SimpleEventManagerImpl*>(simple_event_manager_impl_)->GetStats((adk_impl::SimpleEveManStats&)stats);
}

void SimpleEventManager::ReleaseWaitThread()
{
    reinterpret_cast<SimpleEventManagerImpl*>(simple_event_manager_impl_)->ReleaseWaitThread();
}

int32_t SimpleEventManager::TryNotifyImpl(const std::function<int32_t(void)>& gen_event)
{
    return reinterpret_cast<SimpleEventManagerImpl*>(simple_event_manager_impl_)->TryNotify(gen_event);
}

int32_t SimpleEventManager::NotifyImpl(const std::function<int32_t(void)>& gen_event)
{
    return reinterpret_cast<SimpleEventManagerImpl*>(simple_event_manager_impl_)->Notify(gen_event);
}

int32_t SimpleEventManager::WaitImpl(const std::function<int32_t(void)>& poll_event, uint64_t timeout_ns)
{
    return reinterpret_cast<SimpleEventManagerImpl*>(simple_event_manager_impl_)->Wait(poll_event, timeout_ns);
}

}