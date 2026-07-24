/**
 * @file
 * @brief      backoff policy and event management
 * @author     zhaonan, zhaonan@archforce.com.cn
 * @date       2017-07-14
 */
#ifndef ADK_IMPL_EVENT_H_
#define ADK_IMPL_EVENT_H_

#include "error_code.h"
#include "arch/generic.h"
#include "timeout_counter.h"
#include "arch/synchronize.h"

namespace adk_impl
{

#define ADK_BACKOFF_USER_DATA_LEN   32
#define ADK_BACKOFF_PAUSE_INIT_LOOPS        1
#define ADK_BACKOFF_PAUSE_LIMIT_DEFAULT     4

#define ADK_BACKOFF_DELAY           1
#define ADK_BACKOFF_LIMIT           2

class Backoff
{
public:
    Backoff() 
    {
        reset_ = NULL;    
        run_ = NULL;
        config_ = NULL;
        is_event_ = false ;
    }

    ~Backoff() {}
    /**
     * @brief      reset the backoff policy to initial value
     */
    inline void Reset() { reset_(user_); }

    /**
     * @brief is event or not
     * @ret true or false
     */
    inline bool IsEvent() {return is_event_;}

    /**
     * @brief      run policy to backoff
     */
    inline void Run() { run_(user_); }

    /**
     * @brief      configure the backoff policy
     *
     * @param[in]  type  The config option type
     * @param      val   The config value
     * @param[in]  len   The length of config value
     *
     * @return     On success, ErrorCode::kSuccess is returned
     */
    inline int32_t Config(uint32_t type, void* val, uint32_t len) { return config_(user_, type, val, len); }

protected:
    void (*reset_)(void*);
    void (*run_)(void*);
    int32_t (*config_)(void*, uint32_t, void*, uint32_t);
    char user_[ADK_BACKOFF_USER_DATA_LEN];

    typedef decltype(reset_) GenericBackoffFunction;
    typedef decltype(config_) GenericConfigFunction;
public:
    bool is_event_ ;
};

/**
 * @brief      backoff with exponential number of cpu pause instruction
 */
namespace policy
{
class Pause: public Backoff
{
public:
    struct BackoffInfo
    {
        uint32_t backoff;
        const uint32_t limit;
    };

    static void Init(Backoff& bf_base)
    {
        Pause& bf = *static_cast<Pause*>(&bf_base);
        char* user_ptr = &bf.user_[0];
        BackoffInfo& bfi = *(BackoffInfo*)user_ptr;
        bfi.backoff = ADK_BACKOFF_PAUSE_INIT_LOOPS;
        *(uint32_t*)&bfi.limit = ADK_BACKOFF_PAUSE_LIMIT_DEFAULT;

        bf.reset_ = reinterpret_cast<GenericBackoffFunction>(&Pause::Reset);
        bf.run_ = reinterpret_cast<GenericBackoffFunction>(&Pause::Run);
        bf.config_ = reinterpret_cast<GenericConfigFunction>(&Pause::Config);
    }

    static void Reset(uint32_t& backoff)
    {
        backoff = ADK_BACKOFF_PAUSE_INIT_LOOPS;
    }

    static void Run(BackoffInfo& bfi)
    {
        for (uint32_t i = bfi.backoff; i != 0; --i)
            ADK_PAUSE();
        bfi.backoff <<= 1;
        bfi.backoff = bfi.backoff > bfi.limit ? bfi.limit : bfi.backoff;
    }

    static int32_t Config(BackoffInfo& bfi, uint32_t type, void* val, uint32_t len)
    { 
        if (type != ADK_BACKOFF_LIMIT || len != sizeof(uint32_t))
        {
            return ErrorCode::kFailure;
        }

        *(uint32_t*)&bfi.limit = *(uint32_t*)val;
        return ErrorCode::kSuccess;
    }
};

/**
 * @brief      backoff with delay
 */
class Delay: public Backoff
{
public:
    static void Init(Backoff& bf_base, uint32_t init = 1)
    {
        Delay& bf = *static_cast<Delay*>(&bf_base);
        char* user_ptr = &bf.user_[0];
        *(uint32_t*)user_ptr = init;
        bf.reset_ = reinterpret_cast<GenericBackoffFunction>(&Delay::Reset);
        bf.run_ = reinterpret_cast<GenericBackoffFunction>(&Delay::Run);
        bf.config_ = reinterpret_cast<GenericConfigFunction>(&Delay::Config);
    }

    static void Reset(uint32_t& delay) {}

    static void Run(uint32_t& delay)
    {
        if (delay != 0)
            usleep(delay);
    }

    static int32_t Config(uint32_t& delay, uint32_t type, void* val, uint32_t len) 
    {
        if (type != ADK_BACKOFF_DELAY || len != sizeof(uint32_t))
        {
            return ErrorCode::kFailure;
        }

        delay = *(uint32_t*)val;
        return ErrorCode::kSuccess;
    }
};

class Event : public Backoff
{
public:
    static void Init(Backoff& bf_base,uint32_t init=1)
    {
        bf_base.is_event_ = true ;
        Event& bf = *static_cast<Event*>(&bf_base) ;

        bf.reset_ = reinterpret_cast<GenericBackoffFunction>(&Event::Reset);
        bf.run_ = reinterpret_cast<GenericBackoffFunction>(&Event::Run);
        bf.config_ = reinterpret_cast<GenericConfigFunction>(&Event::Config);
    }
    static void Reset(uint32_t& delay){}
    static void Run(uint32_t& delay){}
    static int32_t Config(uint32_t& delay,uint32_t type,void* val,uint32_t len){ return ErrorCode::kSuccess;}
};


} // policy

struct SimpleEveManStats
{
    uint64_t direct_success;
    uint64_t poll_rounds;
    uint64_t poll_success;
    uint64_t number_waits;
};

/**
 * @brief      facillity event management, use in SPSC scenarios
 * @note       not thread safe
 */
class SimpleEventManager
{
public:
    SimpleEventManager(uint64_t polling_nano = 200000ul, int32_t backoff_limit = 64)
        :   toc_(ADK_TOC_POLLING, polling_nano),
            has_waiters_(false)
    {
        if (adk_impl::IsEnvSetLowUtilization())
        {
            policy::Delay::Init(backoff_);
            backoff_.Config(ADK_BACKOFF_DELAY, &backoff_limit, sizeof(backoff_limit));
        }
        else
        {
            policy::Pause::Init(backoff_);
            backoff_.Config(ADK_BACKOFF_LIMIT, &backoff_limit, sizeof(backoff_limit));
        }

        policy::Pause::Init(backoff_pro_);
        backoff_pro_.Config(ADK_BACKOFF_LIMIT, &backoff_limit, 4); // (1)
        release_alert_ = false;
        direct_success_ = 0;
        poll_rounds_ = 0;
        poll_success_ = 0;
        number_waits_ = 0;

        saved_direct_success_ = 0;
        saved_poll_rounds_ = 0;
        saved_poll_success_ = 0;
        saved_number_waits_ = 0;
        release_alert_pro_ = false;
    }

    ~SimpleEventManager() 
    {}

    template<typename GenEvent>    
    int32_t TryNotify(const GenEvent& gen_event)
    {
        int32_t ec = gen_event();
        if (ec != ErrorCode::kSuccess)
        {
            return ec;
        }

        if (ACCESS_ONCE(has_waiters_))
        {
            has_waiters_ = 0;
            FutexWakePrivate(&has_waiters_);
        }
        return ErrorCode::kSuccess;
    }

    template<typename GenEvent>    
    int32_t Notify(const GenEvent& gen_event)
    {
        int32_t ec = gen_event();
        if (ec != ErrorCode::kSuccess)
        {
            // backoff_pro_.Reset(); see (1)
            do {
                backoff_pro_.Run();
                ec = gen_event();
                if (ec == ErrorCode::kSuccess)
                {
                    goto gen_succ;
                }
            } while (!ACCESS_ONCE(release_alert_pro_));

            return ErrorCode::kWouldblock ;
        }
        gen_succ:

        if (ACCESS_ONCE(has_waiters_))
        {
            has_waiters_ = 0;
            FutexWakePrivate(&has_waiters_);
        }
        return ErrorCode::kSuccess;
    }

    template<typename PollEvent>
    int32_t Wait(const PollEvent& poll_event, uint64_t timeout_ns = -1UL)
    {
        if (poll_event() == ErrorCode::kSuccess)
        {
            ++direct_success_;
            return ErrorCode::kSuccess;
        }

        toc_.Reset();
        do {
            backoff_.Reset();
            do 
            {
                ++poll_rounds_;
                if (poll_event() == ErrorCode::kSuccess)
                {
                    ++poll_success_;
                    return ErrorCode::kSuccess;
                }
                backoff_.Run();
            } while ((poll_rounds_ & 7ul) != 0);

            toc_.Run();
        } while(!toc_.IsTimeout());

        toc_.Reset(timeout_ns);
        has_waiters_ = 1;
        ADK_RMB();
        do {
            if (poll_event() == ErrorCode::kSuccess)
            {
                has_waiters_ = 0;
                return ErrorCode::kSuccess;
            }

            if (ACCESS_ONCE(release_alert_))
            {
                has_waiters_ = 0;
                return ErrorCode::kWouldblock;
            }

            ++number_waits_;
            FutexTimedWaitPrivate(&has_waiters_, 1, 100000ul);

            toc_.Run();
        } while (!toc_.IsTimeout());

        has_waiters_ = 0;
        return ErrorCode::kWouldblock;
    }

    void GetStats(SimpleEveManStats& stats)
    {
        uint64_t temp_direct_success = direct_success_;
        uint64_t temp_poll_success = poll_success_;
        uint64_t temp_number_waits = number_waits_;
        uint64_t temp_poll_rounds = poll_rounds_;

        stats.direct_success = temp_direct_success - saved_direct_success_;
        stats.poll_rounds = temp_poll_rounds - saved_poll_rounds_;
        stats.poll_success = temp_poll_success - saved_poll_success_;
        stats.number_waits = temp_number_waits - saved_number_waits_;

        saved_direct_success_ = temp_direct_success;
        saved_poll_rounds_ = temp_poll_rounds;
        saved_poll_success_ = temp_poll_success;
        saved_number_waits_ = temp_number_waits;
    }

    void ReleaseWaitThread()
    {
        ADK_BARRIER();
        release_alert_ = true;
        release_alert_pro_ = true;
    }

    template<typename PollEvent, typename IdleHandler>
    int32_t Wait(const PollEvent& poll_event, 
                 const IdleHandler& on_idle,
                 uint64_t timeout_ns)
    {
        if (poll_event() == ErrorCode::kSuccess)
        {
            ++direct_success_;
            return ErrorCode::kSuccess;
        }

        on_idle();

        return Wait(poll_event, timeout_ns);
    }

private:
    TimeoutCounter  toc_;
    Backoff         backoff_;
    int32_t         has_waiters_;
    bool            release_alert_;
    uint64_t        direct_success_;
    uint64_t        poll_rounds_;
    uint64_t        poll_success_;
    uint64_t        number_waits_;

    uint64_t        saved_direct_success_;
    uint64_t        saved_poll_rounds_;
    uint64_t        saved_poll_success_;
    uint64_t        saved_number_waits_;
    Backoff         backoff_pro_;
    bool            release_alert_pro_;
};


} // adk

#endif // ADK_EVENT_H_
