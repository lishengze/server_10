#ifndef ADK_IMPL_LOCK_FREE_STREAM_BUFFER_H_
#define ADK_IMPL_LOCK_FREE_STREAM_BUFFER_H_

#include "lock_free_queue_variant.h"

namespace adk_impl
{

class StreamBuffer
{
public:
    static StreamBuffer* Create(uint32_t buffer_size = 8 * 1024 * 1024,
                                uint32_t prefix_size = 1 * 1024 * 1024);

    static void Destroy(StreamBuffer* stream_buffer);

    inline int32_t Push(const char* data, uint32_t data_len)
    {
        if (ADK_UNLIKELY(tail_ + data_len > tail_threshold_))
        {
            tail_threshold_ = ACCESS_ONCE(head_) + buffer_size_;
            if (ADK_UNLIKELY(tail_ + data_len > tail_threshold_))
            {
                return ErrorCode::kFailure;
            }
        }

        const uint32_t offset = (tail_ & buffer_mask_);
        if (offset + data_len <= buffer_size_)
        {
            memcpy(buffer_ + offset, data, data_len);
        }
        else
        {
            const uint32_t copy_len1 = buffer_size_ - offset;
            memcpy(buffer_ + offset, data, copy_len1);
            const uint32_t copy_len2 = data_len - copy_len1;
            memcpy(buffer_, data + copy_len1, copy_len2);
        }

        ADK_BARRIER();
        tail_ += data_len;

        assert(head_ <= tail_);
        return ErrorCode::kSuccess;
    }

    inline std::pair<char*, uint32_t> WaitBuffer()
    {
        const auto current_tail = ACCESS_ONCE(tail_);
        const uint32_t data_len = (uint32_t)(current_tail - head_);

        const uint32_t offset = head_ & buffer_mask_;
        if (offset + data_len <= buffer_size_)
        {
            return std::make_pair(buffer_ + offset, data_len);
        }

        const auto left_len = buffer_size_ - offset;
        if (ADK_UNLIKELY(waiting_more_ && (left_len <= prefix_size_)))
        {
            char* buffer = buffer_ - left_len;
            memcpy(buffer, buffer_ + offset, left_len);
            return std::make_pair(buffer, data_len);
        }

        waiting_more_ = true;
        return std::make_pair(buffer_ + offset, left_len);
    }

    inline void FreeBuffer(uint32_t len)
    {
        ADK_BARRIER();
        head_ += len;
        assert(head_ <= tail_);
        waiting_more_ = false;
    }

    inline void Reset()
    {
        head_ = 0;
        tail_ = 0;
        tail_threshold_ = head_ + buffer_size_;
    }

private:
    StreamBuffer() = default;
    ~StreamBuffer() = default;

    void Init(uint32_t buffer_size, uint32_t prefix_size);

    void Exit();

    char* memory_;
    uint32_t buffer_size_;
    uint32_t prefix_size_;

    char* buffer_;
    uint32_t buffer_mask_;

    uint64_t head_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    bool     waiting_more_;

    uint64_t tail_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    uint64_t tail_threshold_;
};

class UnboundedStreamBuffer
{
public:
    struct StreamNode
    {
        StreamNode* next;
        int64_t     tail;
        int64_t     tail_threshold;
        char        node_memory[];

        inline char* stream_buffer(uint32_t offset) const
        {
            return ((char*)this) + ADK_OFFSET_OF(StreamNode, node_memory) + offset;
        }

        inline char* tail_buffer() const
        {
            return stream_buffer(tail);
        }

        inline uint32_t free_size() const
        {
            assert(tail_threshold >= tail);
            return tail_threshold - tail;
        }
    };

    using cache_type = variant::SPSCQueue<StreamNode*>;

    static UnboundedStreamBuffer* Create(uint32_t block_size = 8 * 1024 * 1024,
                                         uint32_t prefix_size = 1 * 1024 * 1024, 
                                         uint32_t unused_cache_size = 8,
                                         uint32_t node_limit_nr = 1024);

    static void Destroy(UnboundedStreamBuffer* unbounded_buffer);

    inline char* AllocateBuffer(uint32_t allocate_len)
    {
        assert(tail_ptr_);
        if (ADK_UNLIKELY(tail_ptr_->tail + allocate_len > tail_ptr_->tail_threshold))
        {
            if (ADK_UNLIKELY(allocate_len > (uint32_t)block_size_))
            {
                return nullptr;
            }

            if (ADK_UNLIKELY(ReachLimit()))
            {
                return nullptr;
            }

            AppendTail();
        }

        return tail_ptr_->tail_buffer();
    }

    inline std::pair<char*, uint32_t> AllocateBuffer()
    {
        return std::make_pair(tail_ptr_->tail_buffer(), tail_ptr_->free_size());
    }

    inline void PostBuffer(uint32_t data_len)
    {
        assert(tail_ptr_);

        ADK_BARRIER();
        tail_ptr_->tail += data_len;

        assert(tail_ptr_->tail <= tail_ptr_->tail_threshold);

        if (ADK_UNLIKELY(tail_ptr_->tail == tail_ptr_->tail_threshold))
        {
            AppendTail();
        }
    }

    inline uint32_t Push(const char* data, uint32_t data_len)
    {
        assert(tail_ptr_);

        const char* to_copy_data = data;

        do 
        {
            const auto node_free_size = tail_ptr_->free_size();
            const uint32_t left_data_len = data_len - (to_copy_data - data);
            if (ADK_UNLIKELY(left_data_len > node_free_size))
            {
                memcpy(tail_ptr_->tail_buffer(), to_copy_data, node_free_size);

                ADK_BARRIER();
                tail_ptr_->tail += node_free_size;
                to_copy_data += node_free_size;

                if (ADK_UNLIKELY(ReachLimit()))
                {
                    return to_copy_data - data;
                }

                AppendTail();
            }
            else
            {
                memcpy(tail_ptr_->tail_buffer(), to_copy_data, left_data_len);

                ADK_BARRIER();
                tail_ptr_->tail += left_data_len;
                break;
            }
        } while (true);

        return data_len;
    }

    inline void PushHole(uint32_t hole_size)
    {
        assert(tail_ptr_);
        tail_ptr_->tail_threshold = tail_ptr_->tail - hole_size;
        AppendTail();
    }

    inline std::pair<char*, uint32_t> WaitBuffer()
    {
        assert(head_ptr_);
        if (ADK_UNLIKELY(waiting_more_))
        {
            if (nullptr == head_ptr_->next)
            {
                head_threshold_ = ACCESS_ONCE(head_ptr_->tail);
            }
            else
            {
                const auto head_threshold = ACCESS_ONCE(head_ptr_->tail);
                if (head_threshold_ != head_threshold)
                {
                    assert(head_threshold > head_threshold_);
                    head_threshold_ = head_threshold;
                }
                else
                {
                    auto* const next_head_ptr = head_ptr_->next;
                    if (ADK_UNLIKELY(head_threshold > head_ptr_->tail_threshold))
                    {
                        head_ = prefix_size_;
                    }
                    else
                    {
                        const auto left_len = head_threshold - head_;
                        assert(left_len <= prefix_size_);

                        const auto next_head = prefix_size_ - left_len;
                        memcpy(next_head_ptr->stream_buffer(next_head),
                               head_ptr_->stream_buffer(head_),
                               left_len);
                        head_ = next_head;
                    }

                    head_threshold_ = ACCESS_ONCE(next_head_ptr->tail);

                    RecycleNode(head_ptr_);
                    head_ptr_ = next_head_ptr;
                }
            }
        }
        else
        {
            waiting_more_ = true;
            if (ADK_UNLIKELY(head_ == head_threshold_))
            {
                if (nullptr != head_ptr_->next)
                {
                    ADK_BARRIER();
                    head_threshold_ = ACCESS_ONCE(head_ptr_->tail);
                    if (head_ == head_threshold_)
                    {
                        auto* const next_head_ptr = head_ptr_->next;
                        head_ = prefix_size_;
                        head_threshold_ = ACCESS_ONCE(next_head_ptr->tail);
                        RecycleNode(head_ptr_);
                        head_ptr_ = next_head_ptr;
                    }
                }
                else
                {
                    head_threshold_ = ACCESS_ONCE(head_ptr_->tail);
                }
            }
        }

        return head_buffer();
    }

    inline void FreeBuffer(uint32_t len)
    {
        ADK_BARRIER();
        head_ += len;
        assert(head_ <= head_threshold_);
        waiting_more_ = false;
    }

private:
    UnboundedStreamBuffer() = default;
    ~UnboundedStreamBuffer() = default;

    int32_t Init(uint32_t block_size, 
                 uint32_t prefix_size, 
                 uint32_t unused_cache_size, 
                 uint32_t node_limit_nr);

    void Exit();

    bool ReachLimit()
    {
        if (ADK_UNLIKELY(allocate_nr_ > allocate_nr_threshold_ + node_limit_nr_))
        {
            allocate_nr_threshold_ = ACCESS_ONCE(free_nr_);
            if (ADK_UNLIKELY(allocate_nr_ > allocate_nr_threshold_ + node_limit_nr_))
            {
                return true;
            }
        }

        return false;
    }

    void AppendTail()
    {
        StreamNode* const new_node = AllocateCachedNode();
        assert(new_node);
        tail_ptr_->next = new_node;

        ADK_BARRIER();
        tail_ptr_ = new_node;
    }

    inline std::pair<char*, uint32_t> head_buffer() const
    {
        assert(head_threshold_ >= head_);
        return std::make_pair(head_ptr_->stream_buffer(head_), head_threshold_ - head_);
    }

    StreamNode* AllocateCachedNode()
    {
        StreamNode* stream_node = cache_node_;
        if (nullptr != stream_node)
        {
            cache_node_ = nullptr;
        }
        else
        {
            if (ErrorCode::kSuccess != unused_cache_nodes_->Pop(stream_node))
            {
                stream_node = NewStreamNode();
            }
        }

        ResetStreamNode(stream_node);
        ADK_BARRIER();
        return stream_node;
    }

    void RecycleNode(StreamNode* const stream_node)
    {
        assert(stream_node);

        if (nullptr == cache_node_)
        {
            cache_node_ = stream_node;
        }
        else
        {
            if (ErrorCode::kSuccess != unused_cache_nodes_->Push(stream_node))
            {
                DeleteStreamNode(stream_node);
            }
        }
    }

    StreamNode* NewStreamNode()
    {
        const uint32_t node_buffer_size = ADK_OFFSET_OF(StreamNode, node_memory) + prefix_size_ + block_size_;
        ++allocate_nr_;
        return (StreamNode*)(new char[node_buffer_size]);
    }

    void DeleteStreamNode(StreamNode* stream_node)
    {
        assert(stream_node);
        delete[] (char*)stream_node;
        ++free_nr_;
    }

    inline void ResetStreamNode(StreamNode* stream_node)
    {
        assert(stream_node);
        stream_node->next = nullptr;
        stream_node->tail = prefix_size_;
        stream_node->tail_threshold = prefix_size_ + block_size_;
    }

    int32_t     block_size_;
    int32_t     prefix_size_;
    cache_type* unused_cache_nodes_;

    StreamNode* cache_node_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    StreamNode* tail_ptr_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    int64_t     allocate_nr_;
    int64_t     allocate_nr_threshold_;
    uint32_t    node_limit_nr_;
    StreamNode* head_ptr_ __attribute__((aligned(ADK_CACHE_LINE_SIZE)));
    int64_t     free_nr_;
    int64_t     head_;
    int64_t     head_threshold_;
    bool        waiting_more_;
};

}

#endif
