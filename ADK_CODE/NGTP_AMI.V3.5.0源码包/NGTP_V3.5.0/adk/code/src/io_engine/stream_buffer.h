#ifndef ADK_IMPL_IO_ENGINE_STREAM_BUFFER_H_
#define ADK_IMPL_IO_ENGINE_STREAM_BUFFER_H_

#include <adk/lock_free_queue_variant.h>

namespace adk_impl
{

namespace io_engine
{

class RxStreamBuffer
{
public:
    static RxStreamBuffer* Create(uint32_t default_node_size);

    static void Destroy(RxStreamBuffer* stream_buffer);

    inline char* AllocBuffer(uint32_t buffer_size)
    {
        if (tail_ + buffer_size < threshold_)
        {
            return buffer_ + tail_;
        }

        const auto msg_size = data_len();
        assert(msg_size <= (int32_t)threshold_);

        if (buffer_size <= threshold_ - msg_size)
        {
            assert(msg_size > 0);
            memmove(buffer_, buffer_ + head_, msg_size);
            head_ = 0;
            tail_ = msg_size;
            return buffer_ + tail_;
        }

        threshold_ = ADK_ROUND_UP(buffer_size + msg_size, 1024 * 1024);
        char* new_buffer = new char[threshold_];
        if (msg_size > 0)
        {
            memcpy(new_buffer, buffer_ + head_, msg_size);
            head_ = 0;
            tail_ = msg_size;
        }

        delete[] buffer_;

        buffer_ = new_buffer;
        return buffer_ + tail_;
    }

    inline void PostBuffer(uint32_t post_size)
    {
        assert(tail_ + post_size <= threshold_);
        tail_ += post_size;
    }

    inline void AppendBuffer(void* buffer, uint32_t buffer_size)
    {
        auto* const alloc_buffer = AllocBuffer(buffer_size);
        assert(alloc_buffer);

        memcpy(alloc_buffer, buffer, buffer_size);
        PostBuffer(buffer_size);
    }

    inline std::pair<char*, int32_t> WaitBuffer() const
    {
        return std::make_pair(buffer_ + head_, data_len());
    }

    inline void FreeBuffer()
    {
        head_ = 0;
        tail_ = 0;
    }

    inline void FreeBuffer(uint32_t len)
    {
        assert(head_ + len <= tail_);
        if (tail_ == head_ + len)
        {
            head_ = 0;
            tail_ = 0;
        }
        else
        {
            head_ += len;
        }
    }

    uint32_t load_size() const
    {
        const int64_t tail = tail_;
        const int64_t head = head_;
        if (tail > head)
        {
            return tail - head;
        }

        return 0;
    }

    int32_t data_len() const
    {
        assert(tail_ >= head_);
        return tail_ - head_;
    }

    uint32_t free_size() const
    {
        assert(threshold_ >= tail_);
        return (threshold_ - tail_);
    }

private:
    RxStreamBuffer(uint32_t default_node_size);
    ~RxStreamBuffer();

    uint32_t head_;
    uint32_t tail_;
    uint32_t threshold_;
    char*    buffer_;
};

}

}

#endif
