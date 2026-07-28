#ifndef ADK_IMPL_ID_H_
#define ADK_IMPL_ID_H_

#include <stdint.h>

namespace adk_impl
{

#define ADK_ID_COUNTER_BITS     42          // 4T messages
#define ADK_STAGE_ID_BITS       6           // 64 stages
#define ADK_APP_ID_BITS         16          // 65536 context instance

#define ADK_APP_ID_SHIFT        (ADK_ID_COUNTER_BITS + ADK_STAGE_ID_BITS)
#define ADK_STAGE_ID_SHIFT      ADK_ID_COUNTER_BITS

#define ADK_APP_ID_MASK         (((1UL << ADK_APP_ID_BITS) - 1UL) << ADK_APP_ID_SHIFT)
#define ADK_STAGE_ID_MASK       (((1UL << ADK_STAGE_ID_BITS) - 1UL) << ADK_STAGE_ID_SHIFT)
#define ADK_ID_COUNTER_MASK     ((1UL << ADK_ID_COUNTER_BITS) - 1UL)

struct GID
{
    uint64_t value;

    inline uint64_t get_value()
    {
        return value;
    }

    inline void set_value(uint64_t other_value)
    {
        value = other_value;
    }

    inline void Reset()
    {
        value = 0;
    }

    inline uint64_t app_id()
    {
        return (value & ADK_APP_ID_MASK) >> ADK_APP_ID_SHIFT;
    }

    inline uint64_t stage_id()
    {
        return (value & ADK_STAGE_ID_MASK) >> ADK_STAGE_ID_SHIFT;
    }

    inline uint64_t counter()
    {
        return value & ADK_ID_COUNTER_MASK;
    }

    // Note: this is a static method, call this method in process initialization procedure once!
    static inline void set_app_id(uint64_t app_id)
    {
        g_app_id = 0;
        g_app_id = app_id << ADK_APP_ID_SHIFT;
    }

    inline void set_stage_id(uint64_t id) 
    {
        value = (value & (~(ADK_STAGE_ID_MASK | ADK_APP_ID_MASK))) | ((id << ADK_STAGE_ID_SHIFT) | g_app_id); 
    }

    static uint64_t g_app_id;
};

} // adk

#endif // ADK_ID_H_
