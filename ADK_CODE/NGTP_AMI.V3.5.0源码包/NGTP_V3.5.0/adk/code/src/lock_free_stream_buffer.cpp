#include <adk/lock_free_stream_buffer.h>

namespace adk_impl
{

StreamBuffer* StreamBuffer::Create(uint32_t buffer_size, uint32_t prefix_size)
{
    if (ADK_UNLIKELY(buffer_size < prefix_size))
    {
        return nullptr;
    }

    void* buffer_memory = aligned_malloc(ADK_CACHE_LINE_SIZE, sizeof(StreamBuffer));
    new (buffer_memory) StreamBuffer();

    StreamBuffer* stream_buffer = (StreamBuffer*)buffer_memory;
    stream_buffer->Init(buffer_size, prefix_size);
    return stream_buffer;
}

void StreamBuffer::Destroy(StreamBuffer* stream_buffer)
{
    assert(stream_buffer);
    stream_buffer->Exit();
    stream_buffer->~StreamBuffer();
    aligned_free(stream_buffer);
}

void StreamBuffer::Init(uint32_t buffer_size, uint32_t prefix_size)
{
    buffer_size_ = ADK_ROUND_TO_POWER_OF_2(buffer_size);
    prefix_size_ = ADK_ROUND_UP(prefix_size, ADK_CACHE_LINE_SIZE);

    memory_ = new char[prefix_size_ + buffer_size_];

    buffer_ = memory_ + prefix_size_;
    buffer_mask_ = buffer_size_ - 1;

    Reset();
}

void StreamBuffer::Exit()
{
    if (nullptr != memory_)
    {
        delete[] memory_;
        memory_ = nullptr;
    }

    buffer_ = nullptr;
}

UnboundedStreamBuffer* UnboundedStreamBuffer::Create(uint32_t block_size, 
                                                     uint32_t prefix_size, 
                                                     uint32_t unused_cache_size,
                                                     uint32_t node_limit_nr)
{
    if (ADK_UNLIKELY(block_size < prefix_size))
    {
        return nullptr;
    }

    void* buffer_memory = aligned_malloc(ADK_CACHE_LINE_SIZE, sizeof(UnboundedStreamBuffer));
    new (buffer_memory) UnboundedStreamBuffer();

    UnboundedStreamBuffer* unbounded_buffer = (UnboundedStreamBuffer*)buffer_memory;

    if (ADK_UNLIKELY(ErrorCode::kSuccess != unbounded_buffer->Init(block_size, prefix_size, unused_cache_size, node_limit_nr)))
    {
        unbounded_buffer->Exit();
        unbounded_buffer->~UnboundedStreamBuffer();
        aligned_free(unbounded_buffer);
        return nullptr;
    }

    return unbounded_buffer;
}

void UnboundedStreamBuffer::Destroy(UnboundedStreamBuffer* unbounded_buffer)
{
    assert(unbounded_buffer);
    unbounded_buffer->Exit();
    unbounded_buffer->~UnboundedStreamBuffer();
    aligned_free(unbounded_buffer);
}

int32_t UnboundedStreamBuffer::Init(uint32_t block_size, 
                                    uint32_t prefix_size, 
                                    uint32_t unused_cache_size, 
                                    uint32_t node_limit_nr)
{
    block_size_ = ADK_ROUND_UP(block_size, ADK_CACHE_LINE_SIZE);
    prefix_size_ = ADK_ROUND_UP(prefix_size, ADK_CACHE_LINE_SIZE);

    unused_cache_size = std::max<uint32_t>(unused_cache_size, 1);

    unused_cache_nodes_ = cache_type::Create("unused cache", unused_cache_size);
    assert(unused_cache_nodes_);

    allocate_nr_ = 0;
    allocate_nr_threshold_ = 0;

    free_nr_ = 0;

    if (ADK_UNLIKELY(node_limit_nr < (unused_cache_size + 2)))
    {
        return ErrorCode::kFailure;
    }

    node_limit_nr_ = node_limit_nr;

    cache_node_ = NewStreamNode();

    assert(cache_node_);
    memset(cache_node_->stream_buffer(0), 0, prefix_size_ + block_size_);

    tail_ptr_ = NewStreamNode();

    assert(tail_ptr_);
    memset(tail_ptr_->stream_buffer(0), 0, prefix_size_ + block_size_);

    ResetStreamNode(tail_ptr_);

    head_ptr_ = tail_ptr_;
    head_ = prefix_size_;
    head_threshold_ = head_;

    return ErrorCode::kSuccess;
}

void UnboundedStreamBuffer::Exit()
{
    while (nullptr != head_ptr_)
    {
        auto* const head_next = head_ptr_->next;
        head_ptr_ = head_next;
        DeleteStreamNode(head_next);
    }

    if (nullptr != cache_node_)
    {
        DeleteStreamNode(cache_node_);
        cache_node_ = nullptr;
    }

    if (nullptr != unused_cache_nodes_)
    {
        StreamNode* stream_node = nullptr;
        while (ErrorCode::kSuccess == unused_cache_nodes_->Pop(stream_node))
        {
            DeleteStreamNode(stream_node);
        }

        cache_type::Delete(unused_cache_nodes_);
        unused_cache_nodes_ = nullptr;
    }
}

}