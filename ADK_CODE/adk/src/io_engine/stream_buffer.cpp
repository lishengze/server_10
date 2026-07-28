#include "stream_buffer.h"

namespace adk_impl
{

namespace io_engine
{

RxStreamBuffer* RxStreamBuffer::Create(uint32_t default_node_size)
{
    auto* const stream_buffer = (RxStreamBuffer*)aligned_malloc(ADK_CACHE_LINE_SIZE,
                                                                sizeof(RxStreamBuffer));
    if (ADK_UNLIKELY(nullptr == stream_buffer))
    {
        return nullptr;
    }

    new ((void*)stream_buffer) RxStreamBuffer(default_node_size);

    return stream_buffer;
}

void RxStreamBuffer::Destroy(RxStreamBuffer* stream_buffer)
{
    if (nullptr != stream_buffer)
    {
        stream_buffer->~RxStreamBuffer();
        aligned_free(stream_buffer);
    }
}

RxStreamBuffer::RxStreamBuffer(uint32_t default_node_size)
{
    head_ = 0;
    tail_ = 0;
    threshold_ = ADK_ROUND_UP(default_node_size, 1024 * 1024);
    buffer_ = new char[threshold_];
}

RxStreamBuffer::~RxStreamBuffer()
{
    if (nullptr != buffer_)
    {
        delete[] buffer_;
    }
}

}

}