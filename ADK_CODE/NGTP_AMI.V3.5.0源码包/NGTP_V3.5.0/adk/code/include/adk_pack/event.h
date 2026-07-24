/** 
*  Copyright (c) 2018 Archforce Financial Technology.  All rights reserved. 
*  Redistribution and use in source and binary forms, with or without  modification, are not permitted.   
*  For more information about Archforce, welcome to archforce.cn.
*/

#ifndef ADK_EVENT_H_
#define ADK_EVENT_H_

#include <stdint.h>
#include <functional>

namespace adk
{

#define ADK_BACKOFF_USER_DATA_LEN   32
#define ADK_BACKOFF_PAUSE_INIT_LOOPS        1
#define ADK_BACKOFF_PAUSE_LIMIT_DEFAULT     4

#define ADK_BACKOFF_DELAY           1
#define ADK_BACKOFF_LIMIT           2

class Backoff
{
public:
    Backoff();

    ~Backoff();

    /**
     * @brief      reset the backoff policy to initial value
     */
    void Reset();

    /**
     * @brief is event or not
     * @ret true or false
     */
    bool IsEvent();

    /**
     * @brief      run policy to backoff
     */
    void Run();

    /**
     * @brief      configure the backoff policy
     *
     * @param[in]  type  The config option type
     * @param      val   The config value
     * @param[in]  len   The length of config value
     *
     * @return     On success, ErrorCode::kSuccess is returned
     */
    int32_t Config(uint32_t type, void* val, uint32_t len);

    void* backoff_impl_;
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

    static void Init(Backoff& bf_base);

    static void Reset(uint32_t& backoff);

    static void Run(BackoffInfo& bfi);

    static int32_t Config(BackoffInfo& bfi, uint32_t type, void* val, uint32_t len);
};

/**
 * @brief      backoff with delay
 */
class Delay: public Backoff
{
public:
    static void Init(Backoff& bf_base, uint32_t init = 1);

    static void Reset(uint32_t& delay);

    static void Run(uint32_t& delay);

    static int32_t Config(uint32_t& delay, uint32_t type, void* val, uint32_t len);
};

class Event : public Backoff
{
public:
    static void Init(Backoff& bf_base, uint32_t init = 1);

    static void Reset(uint32_t& delay);
    
    static void Run(uint32_t& delay);
    
    static int32_t Config(uint32_t& delay, uint32_t type, void* val, uint32_t len);
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
    SimpleEventManager(uint64_t polling_nano = 200000ul, int32_t backoff_limit = 64);

    ~SimpleEventManager();

    template<typename GenEvent>
    int32_t TryNotify(const GenEvent& gen_event)
    {
        return TryNotifyImpl(gen_event);
    }

    template<typename GenEvent>
    int32_t Notify(const GenEvent& gen_event)
    {
        return NotifyImpl(gen_event);
    }

    template<typename PollEvent>
    int32_t Wait(const PollEvent& poll_event, uint64_t timeout_ns = -1UL)
    {
        return WaitImpl(poll_event, timeout_ns);
    }

    void GetStats(SimpleEveManStats& stats);

    void ReleaseWaitThread();

private:
    int32_t TryNotifyImpl(const std::function<int32_t(void)>& gen_event);

    int32_t NotifyImpl(const std::function<int32_t(void)>& gen_event);

    int32_t WaitImpl(const std::function<int32_t(void)>& poll_event, uint64_t timeout_ns);

    void* simple_event_manager_impl_;
};


} // adk

#endif // ADK_EVENT_H_
