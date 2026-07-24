#include <adk/msg_trace.h>

namespace adk_impl
{

__thread uint64_t MsgTrace::ts_id_ = 0;
__thread uint64_t MsgTrace::prev_buf_ids_[kMaxIDs] = {0, 0, 0};

} // adk
