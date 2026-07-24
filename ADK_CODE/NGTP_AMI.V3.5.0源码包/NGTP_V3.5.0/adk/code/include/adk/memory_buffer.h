#ifndef ADK_IMPL_MEMORY_BUFFER_H_
#define ADK_IMPL_MEMORY_BUFFER_H_

#include "id.h"
#include "shm_ptr.h"
#include "arch/generic.h"

namespace adk_impl
{

#define ADK_MEMORY_BUFFER_EMERGENT          0X0001
#define ADK_MEMORY_BUFFER_SYS               0X0100

#define IS_EMERGENT_BUFFER(buf)             (((buf)->mem_buf_flag & ADK_MEMORY_BUFFER_EMERGENT))
#define IS_SYSMEM_BUFFER(buf)               (((buf)->mem_buf_flag & ADK_MEMORY_BUFFER_SYS))

struct BufferIDs
{
    GID                             orig_id;
    GID                             parent_id;
    GID                             msg_id;
    inline void Reset()
    {}
};

struct MemoryBuffer
{
    // Note: the following fields should never change after init
    ShmPointer                      shm_ptr;                    // FIXME: shm_ptr value never change
    uint32_t                        mem_buf_size;               // Note: the declare order should not be changed

    // Note: the following fields could be write
    uint32_t                        mem_buf_unused;
    uint32_t                        mem_buf_flag;
    struct BufferIDs                buf_ids;
    char   data[];

    void set_mem_buf_size(uint32_t size)
    {
        mem_buf_size = size;
    }

    inline void reset(uint32_t extra_header_len = 0)
    {
        mem_buf_unused = mem_buf_size - (ADK_OFFSET_OF(MemoryBuffer, data) + extra_header_len);
        mem_buf_flag = 0;
        buf_ids.Reset();
    }

    inline void ResetAsPrivBuff(uint32_t size)
    {
        set_mem_buf_size(size);
        reset();
    }

    inline void Pull(uint32_t size)
    {
        mem_buf_unused -= size;
    }
};
}

#endif // ADK_MEMORY_BUFFER_H_
