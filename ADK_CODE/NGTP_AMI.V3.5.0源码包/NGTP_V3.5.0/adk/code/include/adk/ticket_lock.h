#ifndef ADK_IMPL_TICKET_LOCK_H_
#define ADK_IMPL_TICKET_LOCK_H_

#include "arch/generic.h"

#include <atomic>

namespace adk_impl
{

class TicketLock
{
public:
    TicketLock()
        :   lock_var_cur_(0),
            lock_var_next_(1)
    {}

    inline bool lock()
    {
        uint32_t my_ticket = (++lock_var_cur_);

        while (true)
        {
            if (ADK_LIKELY(my_ticket == lock_var_next_))
                return true;
            ADK_PAUSE();
        }

        return true;
    }

    inline void unlock()
    {
        ADK_BARRIER();
        ++lock_var_next_;
    }

    std::atomic<uint32_t> lock_var_cur_;
    uint32_t              lock_var_next_;
};

}

#endif
