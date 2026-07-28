#include <stdlib.h>
#include <malloc.h>

#include "adk/spsc_byte_buffer.h"
#include "adk/arch/generic.h"
#include "adk/error_code.h"

namespace adk_impl
{
namespace bytebuffer
{

struct ByteBufferImpl
{
    volatile int32_t used ADK_CACHE_LINE_ALIGN;
    char* buffer = nullptr;
    ADK_EMPTY_CACHE_LINE;
};

struct ByteBufferPool
{
    Producer* p;
    Consumer* c;
    int32_t buffer_size = 0;
    ByteBufferImpl bb[2];

    volatile int32_t producer_index ADK_CACHE_LINE_ALIGN;
    ADK_EMPTY_CACHE_LINE;

    volatile int32_t consumer_index ADK_CACHE_LINE_ALIGN;
    ADK_EMPTY_CACHE_LINE;
};

int32_t ByteBuffer::Init(int32_t buffer_size, int32_t extra_size)
{
    auto* bp = (ByteBufferPool*)memalign(ADK_CACHE_LINE_SIZE, sizeof(ByteBufferPool));
    new (bp) ByteBufferPool();

    int32_t new_size = (extra_size == 0) ? (buffer_size * 2) : (buffer_size + extra_size);
    bp->bb[0].buffer = (char*)memalign(ADK_CACHE_LINE_SIZE, new_size);
    bp->bb[1].buffer = (char*)memalign(ADK_CACHE_LINE_SIZE, new_size);
    bp->buffer_size = buffer_size;
    bp->producer_index = 0;
    bp->consumer_index = 0;
    bp->p = nullptr;
    bp->c = nullptr;

    private_data_ = bp;
    return ErrorCode::kSuccess;
}

Producer* ByteBuffer::GetBufferProducer()
{
    ByteBufferPool* bp = (ByteBufferPool*)private_data_;
    if (bp->p != nullptr)
        return bp->p;

    char* paddr = (char*)memalign(ADK_CACHE_LINE_SIZE, sizeof(Producer));
    new (paddr) Producer();
    bp->p = (Producer*)paddr;
    bp->p->buffer_ = bp->bb[0].buffer;
    bp->p->private_data_ = private_data_;
    bp->p->total_ = bp->buffer_size;
    bp->p->used_ = &(bp->bb[0].used);
    bp->p->visible_bytes_ = 0;
    return bp->p;
}

Consumer* ByteBuffer::GetBufferConsumer()
{
    ByteBufferPool* bp = (ByteBufferPool*)private_data_;
    if (bp->c != nullptr)
        return bp->c;

    char* caddr = (char*)memalign(ADK_CACHE_LINE_SIZE, sizeof(Consumer));
    new (caddr) Consumer();
    bp->c = (Consumer*)caddr;
    bp->c->buffer_ = bp->bb[0].buffer;
    bp->c->private_data_ = private_data_;
    bp->c->total_ = 0;
    bp->c->used_ = &(bp->bb[0].used);
    return bp->c;
}

void Producer::FlushData()
{
    *used_ = visible_bytes_;
}

int32_t Producer::RenewBuffer(bool is_non_block)
{
    ByteBufferPool* bp = (ByteBufferPool*)private_data_;
    int32_t temp = bp->producer_index + 1;
    while (temp - bp->consumer_index > 1)
    {
        if (is_non_block)
            return ErrorCode::kFailure;

        ADK_PAUSE();
    }

    auto& bb = bp->bb[temp & 0x1];
    int32_t left_size = position_ - visible_bytes_;
    memcpy(bb.buffer, buffer_ + visible_bytes_, left_size);

    buffer_ = bb.buffer;
    position_ = left_size;
    total_ = bp->buffer_size;
    used_ = &(bb.used);
    *used_ = 0;
    visible_bytes_ = 0;
    ADK_BARRIER();
    ++(bp->producer_index);
    return ErrorCode::kSuccess;
}

int32_t Consumer::UpdateBuffer()
{
    ByteBufferPool* bp = (ByteBufferPool*)private_data_;

    int32_t local_producer_index = bp->producer_index;
    int32_t local_consumer_index = bp->consumer_index;
    
    if (local_producer_index == local_consumer_index)
    {
        total_ = *used_;
        return total_ - position_;
    }
    else
    {
        assert(local_producer_index > local_consumer_index);

        total_ = *used_;
        int32_t ret = total_ - position_;
        if (ret == 0)
        {
            ++local_consumer_index;
            auto& bb = bp->bb[local_consumer_index & 0x1];
            buffer_ = bb.buffer;
            position_ = 0;
            used_ = &(bb.used);
            ADK_BARRIER();
            ++(bp->consumer_index);

            total_ = *(used_);
            return total_ - position_;
        }
        return ret;
    }
}

} // bytebuffer
} // adk
