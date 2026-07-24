#include "test_thread.h"

#include <adk/util.h>

namespace adk
{
static int32_t g_oob_msg_type_current = ADK_MESSAGE_BASE_TYPE + 1;
static int32_t g_msg_type_current = ADK_MESSAGE_BASE_TYPE + 1;

int32_t AllocMessageType(bool is_oob)
{
    int32_t ret = atomic_inc(is_oob ? g_oob_msg_type_current : g_msg_type_current) 
                  * (is_oob ? -1 : 1);
    assert(ret < ADK_MAX_THREAD_MESSAGES);
    assert(ret > -(ADK_MAX_THREAD_OOB_MESSAGES));
    return ret;
}
} // adk

