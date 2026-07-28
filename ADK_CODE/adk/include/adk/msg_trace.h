#ifndef ADK_IMPL_MSG_TRACE_H_
#define ADK_IMPL_MSG_TRACE_H_

#include "id.h"
#include "shm_ptr.h"
#include "memory_buffer.h"

namespace adk_impl
{

class MsgTrace
{
public:
    enum Const
    {
        kOrigID,
        kParentID,
        kMsgID,
        kMaxIDs,
    };

    static void SaveIDs(BufferIDs* buf_ids)
    {
        prev_buf_ids_[kOrigID] = buf_ids->orig_id.get_value();
        prev_buf_ids_[kParentID] = buf_ids->parent_id.get_value();
        prev_buf_ids_[kMsgID] = buf_ids->msg_id.get_value();
    }

    static void ConnectIDs(BufferIDs* cur_buf_ids)
    {
        cur_buf_ids->orig_id.set_value(prev_buf_ids_[kOrigID]);
        cur_buf_ids->parent_id.set_value(prev_buf_ids_[kMsgID]);
    }

    static inline void InitID(GID id)
    {
        ts_id_ = id.get_value();
    }

    static inline uint64_t NewID()
    {
        return (++ts_id_);
    }

private:
    static __thread uint64_t ts_id_;
    static __thread uint64_t prev_buf_ids_[kMaxIDs];

    MsgTrace()
    {}

    ~MsgTrace()
    {}
};

}
#endif // ADK_MSG_TRACE_H_
